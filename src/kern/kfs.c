// fs.c - Implementation of the file system interface
#include "file_struct.h"
#include "fs.h"
#include <string.h>
#include <stdlib.h>
#include "heap.h"
// #include "../util/mkfs.c"
// Constants
#define MAX_FILES 32
#define FS_BLKSZ 4096
#define FS_NAMELEN 32
#define FILE_IN_USE 1
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
// // IOCTL commands
// #define IOCTL_GETLEN   1
// #define IOCTL_GETPOS   2
// #define IOCTL_SETPOS   3
// #define IOCTL_GETBLKSZ 4

// Global variables
char fs_initialized = 0;
static struct io_intf *fs_blk_io = NULL;
static boot_block_t boot_block;
static inode_t *inodes = NULL;
static struct file_t file_array[MAX_FILES];

// Forward declarations
static const struct io_ops file_ops;
static struct file_t *get_file_t(struct io_intf *io);

// Initialize the file system
void fs_init(void) {
    memset(file_array, 0, sizeof(file_array));
    fs_initialized = 1;
}

// Mount the file system
int fs_mount(struct io_intf *blkio) {
    fs_blk_io = blkio;

    // Seek to the beginning
    unsigned long pos = 0;
    int ret = fs_blk_io->ops->ctl(fs_blk_io, IOCTL_SETPOS, &pos);
    if (ret < 0) {
        return ret;
    }

    // Read the boot block
    long nread = fs_blk_io->ops->read(fs_blk_io, &boot_block, sizeof(boot_block_t));
    if (nread != sizeof(boot_block_t)) {
        return -1; // Error
    }

    // Read the inodes
    unsigned long inodes_pos = FS_BLKSZ; // Start of inodes
    ret = fs_blk_io->ops->ctl(fs_blk_io, IOCTL_SETPOS, &inodes_pos);
    if (ret < 0) {
        return ret;
    }

    inodes = (inode_t *)kmalloc(sizeof(inode_t) * boot_block.num_inodes);
    if (!inodes) {
        return -1; // Memory allocation failed
    }

    long inodes_size = sizeof(inode_t) * boot_block.num_inodes;
    nread = fs_blk_io->ops->read(fs_blk_io, inodes, inodes_size);
    if (nread != inodes_size) {
        return -1; // Error
    }

    return 0;
}

// Open a file
int fs_open(const char *name, struct io_intf **ioptr) {
    int i;

    // Find the file in the directory entries
    int found = -1;
    for (i = 0; i < boot_block.num_dentry; i++) {
        if (strncmp(boot_block.dir_entries[i].file_name, name, FS_NAMELEN) == 0) {
            found = i;
            break;
        }
    }

    if (found == -1) {
        // File not found
        return -1;
    }

    // Find an unused file_t in file_array
    for (i = 0; i < MAX_FILES; i++) {
        if (!(file_array[i].flags & FILE_IN_USE)) {
            // Unused
            break;
        }
    }

    if (i == MAX_FILES) {
        // No available file slots
        return -1;
    }

    // Set up the file_t struct
    struct file_t *f = &file_array[i];
    f->flags = FILE_IN_USE;
    f->file_position = 0;
    f->inode_number = boot_block.dir_entries[found].inode;

    // Validate inode number
    if (f->inode_number >= boot_block.num_inodes) {
        // Invalid inode number
        f->flags = 0;
        return -1;
    }

    inode_t *inode = &inodes[f->inode_number];
    f->file_size = inode->byte_len;

    // Set up io_intf
    f->intf.ops = &file_ops;

    *ioptr = &f->intf;

    return 0;
}

// Close a file
void fs_close(struct io_intf *io) {
    struct file_t *f = get_file_t(io);
    if (f) {
        f->flags = 0; // Mark as unused
    }
}

// Read from a file
long fs_read(struct io_intf *io, void *buf, unsigned long n) {
    struct file_t *f = get_file_t(io);
    if (!f) {
        return -1; // Invalid file
    }

    // Compute how many bytes we can read
    unsigned long bytes_left = f->file_size - f->file_position;
    if (bytes_left == 0) {
        return 0; // EOF
    }

    unsigned long to_read = (n < bytes_left) ? n : bytes_left;

    // Read data from the data blocks
    unsigned long offset = f->file_position;//indicate which bolck in the inode should be read
    unsigned long total_read = 0;
    char *buf_ptr = (char *)buf;

    inode_t *inode = &inodes[f->inode_number];

    while (to_read > 0) {
        // Determine which data block and offset within the block
        unsigned long block_offset = offset % FS_BLKSZ;
        unsigned long block_num = offset / FS_BLKSZ;

        if (block_num >= 1023) {
            // Exceeded maximum number of data blocks per file
            return -1;
        }

        uint32_t data_block_num = inode->data_block_num[block_num];
        if (data_block_num >= boot_block.num_data) {
            // Invalid data block number
            return -1;
        }

        // Calculate absolute position of the data block
        unsigned long data_block_pos = FS_BLKSZ * (1 + boot_block.num_inodes + data_block_num);

        // Read data from the data block
        unsigned char data_block[FS_BLKSZ];

        // Seek to the data block
        unsigned long pos = data_block_pos;
        int ret = fs_blk_io->ops->ctl(fs_blk_io, IOCTL_SETPOS, &pos);
        if (ret < 0) {
            return -1;
        }

        // Read the data block
        long nread = fs_blk_io->ops->read(fs_blk_io, data_block, FS_BLKSZ);
        if (nread != FS_BLKSZ) {
            return -1;
        }

        // Copy data to buf
        unsigned long bytes_in_block = FS_BLKSZ - block_offset;
        unsigned long bytes_to_copy = (to_read < bytes_in_block) ? to_read : bytes_in_block;

        memcpy(buf_ptr + total_read, data_block + block_offset, bytes_to_copy);

        total_read += bytes_to_copy;
        to_read -= bytes_to_copy;
        offset += bytes_to_copy;
    }

    // Update file position
    f->file_position += total_read;

    return total_read;
}

// Write to a file
long fs_write(struct io_intf *io, const void *buf, unsigned long n) {
    struct file_t *f = get_file_t(io);
    if (!f) {
        return -1; // Invalid file
    }

    // Compute how many bytes we can write
    unsigned long bytes_left = f->file_size - f->file_position;
    if (bytes_left == 0) {
        return 0; // EOF, can't write more
    }

    unsigned long to_write = (n < bytes_left) ? n : bytes_left;

    // Write data to the data blocks
    unsigned long offset = f->file_position;
    unsigned long total_written = 0;
    const char *buf_ptr = (const char *)buf;

    inode_t *inode = &inodes[f->inode_number];

    while (to_write > 0) {
        // Determine which data block and offset within the block
        unsigned long block_offset = offset % FS_BLKSZ;
        unsigned long block_num = offset / FS_BLKSZ;

        if (block_num >= 1023) {
            // Exceeded maximum number of data blocks per file
            return -1;
        }

        uint32_t data_block_num = inode->data_block_num[block_num];
        if (data_block_num >= boot_block.num_data) {
            // Invalid data block number
            return -1;
        }

        // Calculate absolute position of the data block
        unsigned long data_block_pos = FS_BLKSZ * (1 + boot_block.num_inodes + data_block_num);

        // Read the existing data block into memory
        unsigned char data_block[FS_BLKSZ];

        // Seek to the data block
        unsigned long pos = data_block_pos;
        int ret = fs_blk_io->ops->ctl(fs_blk_io, IOCTL_SETPOS, &pos);
        if (ret < 0) {
            return -1;
        }

        // Read the data block
        long nread = fs_blk_io->ops->read(fs_blk_io, data_block, FS_BLKSZ);
        if (nread != FS_BLKSZ) {
            return -1;
        }

        // Copy data from buf to data_block
        unsigned long bytes_in_block = FS_BLKSZ - block_offset;
        unsigned long bytes_to_copy = (to_write < bytes_in_block) ? to_write : bytes_in_block;

        memcpy(data_block + block_offset, buf_ptr + total_written, bytes_to_copy);

        // Seek back to the data block position
        ret = fs_blk_io->ops->ctl(fs_blk_io, IOCTL_SETPOS, &pos);
        if (ret < 0) {
            return -1;
        }

        // Write the updated data block back to the device
        long nwrite = fs_blk_io->ops->write(fs_blk_io, data_block, FS_BLKSZ);
        if (nwrite != FS_BLKSZ) {
            return -1;
        }

        total_written += bytes_to_copy;
        to_write -= bytes_to_copy;
        offset += bytes_to_copy;
    }

    // Update file position
    f->file_position += total_written;

    return total_written;
}

// IOCTL function
int fs_ioctl(struct io_intf *io, int cmd, void *arg) {
    struct file_t *f = get_file_t(io);
    if (!f) {
        return -1; // Invalid file
    }

    switch (cmd) {
        case IOCTL_GETLEN:
            return fs_getlen(f, arg);
        case IOCTL_GETPOS:
            return fs_getpos(f, arg);
        case IOCTL_SETPOS:
            return fs_setpos(f, arg);
        case IOCTL_GETBLKSZ:
            return fs_getblksz(f, arg);
        default:
            return -1; // Unsupported command
    }
}

// Helper functions
int fs_getlen(struct file_t *f, void *arg) {
    if (!arg) {
        return -1;
    }
    *(unsigned long *)arg = f->file_size;
    return 0;
}

int fs_getpos(struct file_t *f, void *arg) {
    if (!arg) {
        return -1;
    }
    *(unsigned long *)arg = f->file_position;
    return 0;
}

int fs_setpos(struct file_t *f, void *arg) {
    if (!arg) {
        return -1;
    }
    unsigned long new_pos = *(unsigned long *)arg;
    if (new_pos > f->file_size) {
        return -1; // Can't seek beyond end of file
    }
    f->file_position = new_pos;
    return 0;
}

int fs_getblksz(struct file_t *f, void *arg) {
    if (!arg) {
        return -1;
    }
    *(unsigned long *)arg = FS_BLKSZ;
    return 0;
}

// Helper function to get file_t from io_intf
static struct file_t *get_file_t(struct io_intf *io) {
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (&file_array[i].intf == io) {
            if (file_array[i].flags & FILE_IN_USE) {
                return &file_array[i];
            } else {
                return NULL;
            }
        }
    }
    return NULL;
}

// File operations
static const struct io_ops file_ops = {
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .ctl = fs_ioctl,
};
