#include "elf.h"
#include "io.h"
#include <stdint.h>
#include <string.h>
#include "heap.h"

#define ELF_MAGIC "\x7F""ELF" // this is to validate if this is the valid elf header
#define PT_LOAD 1  // this is the magic number for the elf laod
#define ELF_MIN_ADDR 0x8010000000  // lowest available memory add
#define ELF_MAX_ADDR 0x8100000000  // highest available memory add
#define PN_XNUM 0xffff  // this is the program number threashold magic number
#define EI_NIDENT 16
#define ELFCLASS64 2
#define EL_CALSS 4
#define EI_MAG3 3
#define EI_MAG2 2
#define EI_MAG1 1
#define EI_MAG0 0
#define EI_MAGNUM0 0x7f
#define EI_MAGNUM1 0x45
#define EI_MAGNUM2 0x4c
#define EI_MAGNUM3 0x46

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf64_Ehdr; // this is the elf header for validation and parameters


typedef struct {
    uint32_t p_type;     // Segment type
    uint32_t p_flags;    // Segment attributes
    uint64_t p_offset;   // Offset in file
    uint64_t p_vaddr;    // Virtual address in memory
    uint64_t p_paddr;    // Reserved
    uint64_t p_filesz;   // Size of segment in file
    uint64_t p_memsz;    // Size of segment in memory
    uint64_t p_align;    // Segment alignment
} Elf64_Phdr; // this is the struct for processing phheader

// Section Header for 64-bit
typedef struct {
    uint32_t sh_name;      // Section name (string tbl index)
    uint32_t sh_type;      // Section type
    uint64_t sh_flags;     // Section flags
    uint64_t sh_addr;      // Section virtual addr at execution
    uint64_t sh_offset;    // Section file offset
    uint64_t sh_size;      // Section size in bytes
    uint32_t sh_link;      // Link to another section
    uint32_t sh_info;      // Additional section information
    uint64_t sh_addralign; // Section alignment
    uint64_t sh_entsize;   // Entry size if section holds table
} Elf64_Shdr;  // this is the def for shdr in case we need to access the sh_info

int valid_elf_header(Elf64_Ehdr header){
    unsigned char*  e_indent = header.e_ident;
    if (!(e_indent[EI_MAG0]==EI_MAGNUM0&&e_indent[EI_MAG1]==EI_MAGNUM1&&e_indent[EI_MAG2]==EI_MAGNUM2&&e_indent[EI_MAG3]==EI_MAGNUM3&&e_indent[EL_CALSS]==ELFCLASS64)) {
    return 0;
    }

    return 1;

    // THIS IS THE FUNCTION TO JUDGE WHETHER THIS IS A VALID ELF_HEADER
    // ONLY IF THIS IS A VALID ELF HEADER SHALL WE PROCEED
}

// THIS IS A TEMPORARY FOR FLAG SETTING
// void set_memory_protection(void *addr, size_t size, uint32_t p_flags) {
//     int prot = 0;

//     if (p_flags & PF_R) prot |= PROT_READ;
//     if (p_flags & PF_W) prot |= PROT_WRITE;
//     if (p_flags & PF_X) prot |= PROT_EXEC;

//     if (mprotect(addr, size, prot) == -1) {
//         perror("Error setting memory protection");
//     }
// }


int alternative_pnum(struct io_intf *io, Elf64_Ehdr * elf){
    Elf64_Shdr section_header;
    long res;
    if(elf->e_shoff>sizeof(Elf64_Ehdr)){
        unsigned char tmp[elf->e_shoff-sizeof(Elf64_Ehdr)];
        res = ioread_full(io,tmp,sizeof(tmp));
        if(res<0||res!=sizeof(tmp)){
            return -1;
        }
    }
    res= ioread_full(io, &section_header, sizeof(Elf64_Shdr));
    if(res<0||res!= sizeof(Elf64_Shdr)){
        return -1;
    }
    return section_header.sh_info;

}

int elf_load(struct io_intf* io, void(**entryptr)(struct io_intf* io)){
    Elf64_Ehdr elf_header;
    long res;
    res = ioread_full(io, &elf_header,sizeof(Elf64_Ehdr));
    if (res< 0 || res != sizeof(Elf64_Ehdr)) {
        return -1;
    }
    if (!valid_elf_header(elf_header)) {
        // fprintf(stderr, "Invalid ELF file\n");
        return -1;
    }
    *entryptr = (void (*)(struct io_intf *)) (uintptr_t) elf_header.e_entry;
    uint16_t phnum = elf_header.e_phnum;
    if (phnum>= PN_XNUM) {
        phnum = alternative_pnum(io,&elf_header);
        if(phnum ==(uint16_t)-1){
            return -1;
        }
    }
    size_t phdr_table_size = phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *phdr_table = kmalloc(phdr_table_size);
    if (!phdr_table) {
        // fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }

    // 跳过到程序头表的偏移位置
    if (elf_header.e_phoff > sizeof(Elf64_Ehdr)) {
        unsigned char skip_buf[elf_header.e_phoff - sizeof(Elf64_Ehdr)];
        res = ioread_full(io, skip_buf, sizeof(skip_buf));
        if (res < 0 || res != sizeof(skip_buf)) {
            // fprintf(stderr, "Error skipping to program headers\n");
            kfree(phdr_table);
            return -1;
        }
    }

    // 从当前位置开始读取程序头表
    res = ioread_full(io, phdr_table, phdr_table_size);
    if (res < 0 || res != phdr_table_size) {
        // fprintf(stderr, "Error reading program headers\n");
        kfree(phdr_table);
        return -1;
    }

    // 遍历程序头表，加载每个 PT_LOAD 段
    for (int i = 0; i < phnum; i++) {
        Elf64_Phdr *phdr = &phdr_table[i];  // 获取第 i 个程序头

        // 仅处理 PT_LOAD 段
        if (phdr->p_type == PT_LOAD) {
            // 检查虚拟地址范围
            if (phdr->p_vaddr < ELF_MIN_ADDR || phdr->p_vaddr + phdr->p_memsz > ELF_MAX_ADDR) {
                // fprintf(stderr, "Segment address out of range: 0x%lx\n", phdr->p_vaddr);
                kfree(phdr_table);
                return -1;
            }

            // 为段数据分配临时缓冲区
            void *segment_buffer = kmalloc(phdr->p_memsz);
            if (!segment_buffer) {
                // fprintf(stderr, "Memory allocation failed for segment\n");
                kfree(phdr_table);
                return -1;
            }

            // 从文件读取段数据到缓冲区
            res = ioread_full(io, segment_buffer, phdr->p_filesz);
            if (res < 0 || res != phdr->p_filesz) {
                // fprintf(stderr, "Error reading segment data\n");
                kfree(segment_buffer);
                kfree(phdr_table);
                return -1;
            }

            // 填充未初始化部分为 0
            if (phdr->p_memsz > phdr->p_filesz) {
                memset((char *)segment_buffer + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
            }

            // 将数据复制到指定的虚拟地址 p_vaddr
            memcpy((void *)(uintptr_t)phdr->p_vaddr, segment_buffer, phdr->p_memsz);

            // printf("Loaded segment: vaddr=0x%lx, file size=0x%lx, mem size=0x%lx, flags=0x%x\n",
            //        phdr->p_vaddr, phdr->p_filesz, phdr->p_memsz, phdr->p_flags);

            // 释放段缓冲区内存
            kfree(segment_buffer);
        }
    }

    // 清理程序头表内存
    kfree(phdr_table);
    return 0;
}