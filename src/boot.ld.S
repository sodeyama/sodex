#include <ld/page_linker.h>

#define LOAD_OFFSET __PAGE_OFFSET

SECTIONS {
    . = __KERNEL_START; 

    __text_start = .; 
    .text : AT(ADDR(.text) - LOAD_OFFSET) {
        "startup.o"(.text)
        "kernel.o"(.text)
        * (.text)
    }
    __text_end = .; 

    . = ALIGN(0x1000); 
    __bss_start = .;
    .bss.page_aligned : AT(ADDR(.bss.page_aligned) - LOAD_OFFSET) {
        first_pg_dir = .;               
        * (.bss.page_aligned)
    }
    . = ALIGN(0x1000); 
    .bss : AT(ADDR(.bss) - LOAD_OFFSET) {
        pg_dir = .;
        . += 0x1000; 
        * (.bss)
        * (COMMON)
    }
    __bss_end = .; 

    __rodata_start = .; 
    .rodata : AT(ADDR(.rodata) - LOAD_OFFSET) {
        "vga.o" (.rodata)
        * (.rodata)
    }
    __rodata_end = .;

    . = ALIGN(0x1000); 
    __data_start = .; 
    .data : AT(ADDR(.data) - LOAD_OFFSET) {
        "startup.o"(.data)
        * (.data)
        * (COMMON)
    }
    __data_end = .; 
}


