OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64",
	      "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_main)
SECTIONS
{
	PROVIDE (__executable_start = SEGMENT_START("text-segment", 0x400000)); . = SEGMENT_START("text-segment", 0x400000) + SIZEOF_HEADERS;
	. = 0x200020;
	.text : { *(.text) }
	PROVIDE (__etext = .);
	PROVIDE (_etext = .);
	PROVIDE (etext = .);
 	. = ALIGN (CONSTANT (MAXPAGESIZE)) - ((CONSTANT (MAXPAGESIZE) - .) & (CONSTANT (MAXPAGESIZE) - 1));
	. = DATA_SEGMENT_ALIGN (CONSTANT (MAXPAGESIZE), CONSTANT (COMMONPAGESIZE));
	.data : { *(.data) }
	_edata = .; PROVIDE (edata = .);
	. = .;
	__bss_start = .;
	.bss : { *(.bss) }
	. = ALIGN(64 / 8);
	. = SEGMENT_START("ldata-segment", .);
	. = ALIGN(64 / 8);
	_end = .; PROVIDE (end = .);
	. = DATA_SEGMENT_END (.);
}
