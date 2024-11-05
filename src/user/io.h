// io.h - Abstract I/O interface for devices, files, etc.
//

#ifndef _IO_H_
#define _IO_H_

#include <stddef.h> // size_t
#include <stdint.h> // uint64_t
#include <stdarg.h> // va_list

#include "error.h"  // ENOTSUP

// EXPORTED TYPE DEFINITIONS
//

struct io_intf; // forward decl.

// I/O operations provided by the interface. Do not call these directly, use the
// function below instead (e.g. ioread). The /read/ function is allowed to read
// fewer than /bufsz/ bytes, but must read at least one. A return value of 0
// from /read/ indicates an end-of-file condition. The /write/ function is
// allowed to write fewer than /n/ bytes, but must write at least one. A return
// value of 0 from /write/ indicates an end-of-file condition (for files that
// cannot grow).

struct io_ops {
	void (*close)(struct io_intf * io);
	long (*read)(struct io_intf * io, void * buf, unsigned long bufsz);
	long (*write)(struct io_intf * io, const void * buf, unsigned long n);
	int (*ctl)(struct io_intf * io, int cmd, void * arg);
};

struct io_intf {
	const struct io_ops * ops;
};

struct io_lit {
    struct io_intf io_intf;
    void * buf;
    size_t size;
    size_t pos;
};

struct io_term {
    struct io_intf io_intf;
    struct io_intf * rawio;
    int8_t cr_out;
    int8_t cr_in;
};

// IOCTL numbers (0..7 are reserved)

// The currently defined IOCTL commands are:

//   IOCTL_GETLEN - Returns the length of the object. Must be defined for a
//   block device and file. Not supported by some devices (e.g. UART).
//
//   IOCTL_SETLEN - Current not supported (do not need to implement).
//
//   IOCTL_GETPOS - Gets current read/write position in the object, as an offset
//   from the beginning.
//
//   IOCTL_SETPOS - Sets the current read/write position in the object, as an
//   offset from the beginning. Read and write operations will take place at this
//   offset. Must be implemented by files and block devices; not supported by
//   some devices (e.g. UART). See ioseek() functions, which is a wrapper around
//   this ioctl operation.
//
//   IOCTL_FLUSH - Current not supported (do not need to implement).
//
//   IOCTL_GETBLKSZ - Returns the block size. Optional.

#define IOCTL_GETLEN        1   // arg is pointer to uint64_t
#define IOCTL_SETLEN        2   // arg is pointer to uint64_t
#define IOCTL_GETPOS        3   // arg is pointer to uint64_t
#define IOCTL_SETPOS        4   // arg is pointer to uint64_t
#define IOCTL_FLUSH         5   // arg is ignored
#define IOCTL_GETBLKSZ      6   // arg is pointer to uint32_t

// EXPORTED FUNCTION DECLARATIONS
//

// Convenience functions for operating on I/O objects. These functions calls the
// object's operations.

// The ioclose function closes the I/O object. No other functions may be called
// after calling close, and the io_intf pointer should be considered invalid.

static inline void
__attribute__ ((nonnull(1)))
ioclose(struct io_intf * io);

// The ioread function reads data from the I/O object into a buffer. The /buf/
// argument is a pointer to the buffer to fill, and /bufsz/ the size of the
// buffer. The ioread function may return after reading fewer than /bufsz/
// bytes, but will block until it can read at least one byte. A return value of
// 0 indicates the end of file. Negative return values signal and error. To read
// a full buffer worht of data, use ioread_full.

static inline long
__attribute__ ((nonnull(1,2)))
ioread(struct io_intf * io, void * buf, unsigned long bufsz);

// The ioread_full function reads data from the I/O object into a buffer until
// the buffer is full. The /buf/ argument is a pointer to the buffer to fill,
// and /bufsz/ the size of the buffer. This function will not return until it
// reads /bufsz/ bytes or reaches the end of file. Negative return values signal
// an error.

extern long
__attribute__ ((nonnull(1,2)))
ioread_full (
    struct io_intf * io, void * buf, unsigned long bufsz);

// The iowrite function writes data from a buffer to the I/O object. The /buf/
// argument is a pointer to the buffer to write, and /n/ the number of bytes to
// write. This function will not return until it writes /n/ bytes or reaches the
// end of file. Negative return values signal an error.

extern long
__attribute__ ((nonnull(1,2)))
iowrite(struct io_intf * io, const void * buf, unsigned long n);

// The ioctl function invokes special functions on the I/O object. See the IOCTL
// numbers defined above.

static inline int
__attribute__ ((nonnull(1)))
ioctl(struct io_intf * io, int cmd, void * arg);

// The ioseek function sets the current position in the I/O object. This is a
// convenience function that is equivalent to ioctl(io, IOCTL_SETPOS, pos).

static inline int
__attribute__ ((nonnull(1)))
ioseek(struct io_intf * io, uint64_t pos);

// Convenience functions for character I/O. The functions call read or write on
// the io_intf, as need.
//
// The ioputc and iogetc functions return the character read or written, or a
// negative value in case of error.
//
// The ioputs function prints writes a string to the I/O object followeed by a
// terminating newline. It returns 0 on success or a negative error code on
// error.

// The ioprintf and iovprintf return the number of characters written on
// success, or a negative error code on error.

static inline int
__attribute__ ((nonnull(1)))
ioputc(struct io_intf * io, char c);

static inline int
__attribute__ ((nonnull(1)))
iogetc(struct io_intf * io);

extern int
__attribute__ ((nonnull(1)))
ioputs(struct io_intf * io, const char * s);

extern long
__attribute__ ((nonnull(1,2)))
ioprintf(struct io_intf * io, const char * fmt, ...);

extern long
__attribute__ ((nonnull(1,2)))
iovprintf(struct io_intf * io, const char * fmt, va_list ap);

// An I/O literal object allows a block of memory to be treated as file. I/O
// operations should be able to be performed via the io_intf associated with 
// the io_lit.

extern struct io_intf *
__attribute__ ((nonnull(1,2)))
iolit_init(struct io_lit * lit, void * buf, size_t size);

// An io_term object is a wrapper around a "raw" I/O object. It provides newline
// conversion and interactive line-editing for string input.
//
// ioterm_init initializes an io_term object for use with an underlying raw I/O
// object. The /iot/ argument is a pointer to an io_term struct to initialize
// and /rawio/ is a pointer to an io_intf that provides backing I/O.

extern struct io_intf *
__attribute__ ((nonnull(1,2)))
ioterm_init(struct io_term * iot, struct io_intf * rawio);

// The ioterm_getsn function reads a line of input of up to /n/ characters from
// a terminal I/O object. The function supports line editing (delete and
// backspace) and limits the input to /n/ characters.

extern char *
__attribute__ ((nonnull(1,2)))
ioterm_getsn(struct io_term * iot, char * buf, size_t n);

// INLINE FUNCTION DEFINITIONS
//

static inline void ioclose(struct io_intf * io) {
    if (io->ops->close != NULL)
        io->ops->close(io);
}

static inline long ioread (
    struct io_intf * io, void * buf, unsigned long bufsz)
{
    if (io->ops->read)
        return io->ops->read(io, buf, bufsz);
    else
        return -ENOTSUP;
}

static inline int ioctl(struct io_intf * io, int cmd, void * arg) {
    if (io->ops->ctl)
        return io->ops->ctl(io, cmd, arg);
    else
        return -ENOTSUP;
}

static inline int ioseek(struct io_intf * io, uint64_t pos) {
    return ioctl(io, IOCTL_SETPOS, &pos);
}

static inline int ioputc(struct io_intf * io, char c) {
    long wlen;

    wlen = iowrite(io, &c, 1);

    if (wlen < 0)
        return wlen;
    else if (wlen == 0)
        return -EIO;
    else
        return (unsigned char)c;
}


static inline int iogetc(struct io_intf * io) {
    long rlen;
    char c;

    rlen = ioread(io, &c, 1);
    
    if (rlen < 0)
        return rlen;
    else if (rlen == 0)
        return -EIO;
    else
        return (unsigned char)c;
}

#endif // _IO_H_
