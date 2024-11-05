all: mkfs

mkfs: mkfs.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf *.o *.elf *.asm mkfs
