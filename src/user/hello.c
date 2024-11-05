#include "io.h"
#include "string.h"

void main(struct io_intf * io) {
    struct io_intf * tio;
    struct io_term iot;

    tio = ioterm_init(&iot, io);
    ioputs(tio, "Hello, world!");
}