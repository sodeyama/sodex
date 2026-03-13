SECTIONS {
    . = (0xC0000000 +0x1000);
    __text_start = .;
    .text : AT(ADDR(.text) - 0xC0000000) {
        "startup.o"(.text)
        "kernel.o"(.text)
        * (.text)
    }
    __text_end = .;
    __rodata_start = .;
    .rodata : AT(ADDR(.rodata) - 0xC0000000) {
        "vga.o" (.rodata)
        * (.rodata)
    }
    __rodata_end = .;
    . = ALIGN(0x1000);
    __data_start = .;
    .data : AT(ADDR(.data) - 0xC0000000) {
        "startup.o"(.data)
        * (.data)
    }
    __data_end = .;
    . = ALIGN(0x1000);
    .bss.page_aligned (NOLOAD) : AT(ADDR(.bss.page_aligned) - 0xC0000000) {
        first_pg_dir = .;
        * (.bss.page_aligned)
    }
    . = 0xC0000000 + 0x100000;
    . = ALIGN(0x1000);
    __bss_start = .;
    .bss (NOLOAD) : AT(ADDR(.bss) - 0xC0000000) {
        pg_dir = .;
        . += 0x1000;
        * (.bss)
        * (COMMON)
    }
    __bss_end = .;
}
