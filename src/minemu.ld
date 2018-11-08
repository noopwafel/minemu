ENTRY(start);

/* see mm.h for a description of the memory layout */
INCLUDE "src/mm.ld";

PHDRS
{
  text PT_LOAD FILEHDR PHDRS;
  bss PT_LOAD FLAGS(6);
  data PT_LOAD FLAGS(6);
  note PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
  /* user: text,brk,bss,heap,mmaps,stack */
  /* taint                               */
  /* jit code                            */
  . = minemu_start ;
  minemu_code_start = . ;
  . = SIZEOF_HEADERS + .;
  .text : { *(.text) . = ALIGN(0x1000); } :text
  minemu_code_end = . ;
  .bss : { *(.bss) *(COMMON) . = ALIGN(0x1000); } :bss
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end ) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }

  .data : { *(*) . = ALIGN(0x1000); } :data
  minemu_end = . ;

}

