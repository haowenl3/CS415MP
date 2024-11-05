OUTPUT_ARCH("riscv")
ENTRY(main)

PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS {

  . = 0x80100000;

  .text (READONLY) : {
    PROVIDE(_user_text_start = .);
    *(.text .text.*)
    . = ALIGN(16);
    PROVIDE(_user_text_end = .);
    . = ALIGN(4096);
  } :text

  .rodata : {
    *(.srodata .srodata.*)
    . = ALIGN(16);
    *(.rodata .rodata.*)
    . = ALIGN(16);
    . = ALIGN(4096);
  } :data

  .data : {
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
    . = ALIGN(16);
  } :data

  .bss : {
    PROVIDE(_user_bss_start = .);
    *(.sbss .sbss.*)
    . = ALIGN(16);
    *(.bss .bss.*)
    . = ALIGN(16);
    PROVIDE(_user_bss_end = .);
    . = ALIGN(4096);
  } :data
  
  PROVIDE(_user_end = .);
}
