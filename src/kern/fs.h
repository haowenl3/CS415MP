//           fs.h - File system interface
//          

#ifndef _FS_H_
#define _FS_H_
#include "file_struct.h"
#include "io.h"

extern char fs_initialized;
/**
 * Initializes the file system.
 */
extern void fs_init(void);

/**
 * Takes an io_intf* to the filesystem provider and sets up the filesystem 
 * for future fs_open operations. Once you complete this checkpoint, 
 * io will come from the vioblk device struct.
 */
extern int fs_mount(struct io_intf * blkio);
/**
 * Takes the name of the file to be opened and modifies the given pointer 
 * to contain the io_intf of the file. This function should also associate 
 * a specific file struct with the file and mark it as in-use. The user 
 * program will use this io_intf to interact with the file.
 */
extern int fs_open(const char * name, struct io_intf ** ioptr);
/**
 * Marks the file struct associated with io as unused.
 */
extern void fs_close(struct io_intf * io);

/**
 * Writes n bytes from buf into the file associated with io. 
 * The size of the file should not change. You should only 
 * overwrite existing data. Write should also not create any 
 * new files. Updates metadata in the file struct as appropriate. 
 * Use fs_open to get io.
 */
extern long fs_write(struct io_intf * io, const void * buf, unsigned long n);

/**
 * Reads n bytes from the file associated with io into buf. 
 * Updates metadata in the file struct as appropriate. 
 * Use fs_open to get io.
 */
extern long fs_read(struct io_intf * io, void * buf, unsigned long n);

/**
 * Performs a device-specific function based on cmd. Note, ioctl functions 
 * should return values by using arg. See io.h for details.
 */
extern int fs_ioctl(struct io_intf * io, int cmd, void * arg);

/**
 * Helper function for fs_ioctl. Returns the length of the file.
 */
extern int fs_getlen(struct file_t * fd, void * arg);

/**
 * Helper function for fs_ioctl. Returns the current position in the file.
 */
extern int fs_getpos(struct file_t * fd, void * arg);

/**
 * Helper function for fs_ioctl. Sets the current position in the file.
 */
extern int fs_setpos(struct file_t * fd, void * arg);

/**
 * Helper function for fs_ioctl. Returns the block size of the filesystem.
 */
extern int fs_getblksz(struct file_t * fd, void * arg);

//           _FS_H_
#endif
