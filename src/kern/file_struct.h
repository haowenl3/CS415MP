// file_struct.h
#ifndef _FILE_STRUCT_H_
#define _FILE_STRUCT_H_

#include "io.h"

struct file_t {
    struct io_intf intf;
    unsigned long file_position;
    unsigned long file_size;
    uint64_t inode_number;
    uint64_t flags;
};

#endif // _FILE_STRUCT_H_
