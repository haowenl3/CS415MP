//           ezheap.c - Leaking memory manager for small allocations
//          

#include "heap.h"

#include "console.h"
#include "string.h"
#include "halt.h"

#include <stdint.h>

//           EXPORTED GLOBAL VARIABLES
//          

char heap_initialized = 0;

//           INTERNAL GLOBAL VARIABLES
//          

static void * heap_start;
static void * heap_end;

//           EXPORTED FUNCTION DEFINITIONS
//          

void heap_init(void * start, void * end) {
    assert (start < end);
    heap_start = start;
    heap_end = end;
    heap_initialized = 1;
}

void * kmalloc(size_t size) {
    //           Round up to multiple of 16 bytes
    size = (size + 16-1) / 16 * 16;

    if (size <= heap_end - heap_start)
        heap_end -= size;
    else
        panic("Out of memory");
    
    return heap_end;
}

void * kcalloc(size_t n, size_t size) {
    if (n <= (heap_end - heap_start) / size)
        heap_end -= n * size;
    else
        panic("Out of memory");

    memset(heap_end, 0, n * size);
    return heap_end;
}

void * krealloc(void * ptr, size_t size) {
    panic("krealloc not implemented");
}

void kfree(void * ptr) {
    //           do nothing
}