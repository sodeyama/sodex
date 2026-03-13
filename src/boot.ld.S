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
    }
    __data_end = .;

    . = ALIGN(0x1000);
    .bss.page_aligned (NOLOAD) : AT(ADDR(.bss.page_aligned) - LOAD_OFFSET) {
        first_pg_dir = .;
        * (.bss.page_aligned)
    }

    /*
     * Skip the memory hole: physical 0xA0000-0xFFFFF (VGA + ROM/BIOS).
     * BSS must start at or after physical 0x100000 (1MB).
     */
    . = __PAGE_OFFSET + 0x100000;

    . = ALIGN(0x1000);
    __bss_start = .;
    .bss (NOLOAD) : AT(ADDR(.bss) - LOAD_OFFSET) {
        pg_dir = .;
        . += 0x1000;
        * (.bss)
        * (COMMON)
    }
    __bss_end = .;
}


