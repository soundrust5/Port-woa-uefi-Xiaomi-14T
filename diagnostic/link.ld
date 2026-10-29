ENTRY(_start)
SECTIONS
{
  . = 0;
  .text : { KEEP(*(.text.header)) *(.text.entry) *(.text*) }
  .rodata : { *(.rodata*) }
  . = ALIGN(16);
  .data : { KEEP(*(.data.record)) KEEP(*(.data.stack)) *(.data*) }
  . = ALIGN(16);
  __image_end = .;
  .bss : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
  ASSERT(_start == 0, "Image must start at link offset zero")
  ASSERT(__image_end < 0x100000, "ADR references must remain within reach")
  ASSERT((__stack_end & 15) == 0, "Stack must be 16-byte aligned")
  ASSERT(SIZEOF(.bss) == 0, "Probe has no BSS initialization; include all data in image")
}
