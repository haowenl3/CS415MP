#!/bin/bash

# Description:
# Places the contents of file `$1` into an object which contains a ".companion" section
# At link time, this will be placed in the .data section of the kernel
# See kernel.ld:38
#
#
# This utility is mostly provided for Checkpoint 1. It will allow you to test
# program loading or filesystem operation before your virtio block device is
# fully implemented.
#
# added 2024-10-20 by Ingi Helgason

AS=riscv64-unknown-elf-as
OBJCOPY=riscv64-unknown-elf-objcopy
echo .end | $AS -o empty.o
if [ -z "$1" ]; then
	mv empty.o companion.o
else
	$OBJCOPY --set-section-flags .companion=alloc,load,readonly,data --add-section .companion=$1 empty.o companion.o
	rm empty.o
fi
