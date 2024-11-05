#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define FS_BLKSZ      4096
#define FS_NAMELEN    32

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

// Disk layout:
// [ boot block | inodes | data blocks ]

typedef struct dentry_t{
    char file_name[FS_NAMELEN];
    uint32_t inode;
    uint8_t reserved[28];
}__attribute((packed)) dentry_t; 

typedef struct boot_block_t{
    uint32_t num_dentry;
    uint32_t num_inodes;
    uint32_t num_data;
    uint8_t reserved[52];
    dentry_t dir_entries[63];
}__attribute((packed)) boot_block_t;

typedef struct inode_t{
    uint32_t byte_len;
    uint32_t data_block_num[1023];
}__attribute((packed)) inode_t;

typedef struct data_block_t{
    uint8_t data[FS_BLKSZ];
}__attribute((packed)) data_block_t;

void die(const char *);

// convert to riscv byte order
unsigned short
xshort(unsigned short x)
{
  unsigned short y;
  unsigned char *a = (unsigned char*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

unsigned int
xint(unsigned int x)
{
  unsigned int y;
  unsigned char *a = (unsigned char*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: ./mkfs [filesystem_image] [file1] [file2] ...\n");
    exit(1);
  }

  boot_block_t boot_block = {0};

  printf("Making fs\n");

  int fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0)
    die(argv[1]);

  int number_inodes = 0;
  int i;
  for(i = 2; i < argc; i++){ //Add all dentries
    // get rid of "../user/bin/" or "user/bin/"
    char *shortname;
    if(strncmp(argv[i], "../user/bin/", 12) == 0)
      shortname = argv[i] + 12;
    else if(strncmp(argv[i], "user/bin/", 9) == 0)
      shortname = argv[i] + 9;
    else
      shortname = argv[i];

    assert(index(shortname, '/') == 0);

    printf("File name is %s\n", shortname);
    printf("Dentry index is %d\n", number_inodes);
    printf("Inode number is %d\n", number_inodes);
    strncpy(boot_block.dir_entries[number_inodes].file_name, shortname, FS_NAMELEN);
    boot_block.dir_entries[number_inodes].inode = number_inodes;

    number_inodes += 1;
  }

  int data_block_idx = 0;
  int inode_idx = 0;
  inode_t inode_array[number_inodes];

  for(i = 2; i < argc; i++){ //Add all inodes
    FILE* fp;
    if((fp = fopen(argv[i], "r")) == NULL)
      die(argv[i]);

    fseek(fp, 0L, SEEK_END);
    int num_bytes = ftell(fp);
    int num_data_blocks_for_file = (num_bytes / FS_BLKSZ) + 1;
    // we can do this since the inode index is the same as the dentry index
    printf("Number of bytes for file %s: %d\n",boot_block.dir_entries[inode_idx].file_name, num_bytes); 

    int j;
    for (j = 0; j < num_data_blocks_for_file; ++j){
      inode_array[inode_idx].data_block_num[j] = data_block_idx;
      data_block_idx += 1; 
    }

    inode_array[inode_idx].byte_len = num_bytes;
    inode_idx += 1;

    fclose(fp);
  }

  boot_block.num_dentry = number_inodes;
  boot_block.num_inodes = number_inodes;
  boot_block.num_data = data_block_idx;

  printf("Total number of dentries: %d\n", boot_block.num_dentry);
  printf("Total number of inodes: %d\n", boot_block.num_inodes);
  printf("Total number of data blocks: %d\n", boot_block.num_data);

  write(fsfd, &boot_block, sizeof(boot_block_t)); 

  for (i = 0; i < number_inodes; ++i) {
    write(fsfd, &inode_array[i], sizeof(inode_t));
    printf("Wrote Inode %d, Program: %s\n", i, boot_block.dir_entries[i].file_name);
  }

  for(i = 2; i < argc; i++){ //Add all data blocks
    int fd;
    if((fd = open(argv[i], 0)) < 0)
      die(argv[i]);

    char buf[FS_BLKSZ] = {0};
    while(read(fd, buf, sizeof(buf)) > 0)
      write(fsfd, buf, FS_BLKSZ);
  }

  printf("Wrote filesystem image to %s\n", argv[1]);

  close(fsfd);
}

void
die(const char *s)
{
  perror(s);
  exit(1);
}
