/* Linker script of the STM32H743xx DMA bench app (tests/target/dma): the example's map (2048K flash, DTCM 128K at
 * 0x20000000 for data, bss and stack) plus the DMA regions: AXI SRAM (D1, cacheable, kb::dma Maintained) and D2 SRAM1
 * (the window MPU region 0 makes non-cacheable, kb::dma NonCacheable). The DMA sections are loaded from flash like
 * .data (a kb::dma::Buffer is a constant-initialised object whose header holds its data address), copied by the app
 * before use; ASSERTs keep each inside its region and window. Bench code.
 * SPDX-License-Identifier: MIT */
ENTRY(Reset_Handler)
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM_D1 (rw) : ORIGIN = 0x24000000, LENGTH = 512K
  RAM_D2 (rw) : ORIGIN = 0x30000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x1000;
SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
  .text : { . = ALIGN(4); *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame) KEEP(*(.init)) KEEP(*(.fini))
            . = ALIGN(4); _etext = .; } > FLASH
  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
  .preinit_array : { PROVIDE_HIDDEN (__preinit_array_start = .); KEEP(*(.preinit_array*))
                     PROVIDE_HIDDEN (__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN (__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*))
                  PROVIDE_HIDDEN (__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN (__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*))
                  PROVIDE_HIDDEN (__fini_array_end = .); } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
  .kb_dma_d2 : { . = ALIGN(32); _sdata_kb_dma_d2 = .; *(.kb_dma.d2) *(.kb_dma.d2.*) . = ALIGN(32);
                 _edata_kb_dma_d2 = .; } > RAM_D2 AT> FLASH
  _sidata_kb_dma = LOADADDR(.kb_dma_d2);
  .kb_dma_axi : { . = ALIGN(32); _sdata_kb_dma_axi = .; *(.kb_dma.axi) *(.kb_dma.axi.*) . = ALIGN(32);
                  _edata_kb_dma_axi = .; } > RAM_D1 AT> FLASH
  _sidata_kb_dma_axi = LOADADDR(.kb_dma_axi);
  ASSERT(_edata_kb_dma_d2 <= ORIGIN(RAM_D2) + 0x20000, "kb_dma: the D2 DMA section is larger than the MPU window")
  ASSERT((_sdata_kb_dma_d2 % 32) == 0, "kb_dma: the D2 DMA section is not line aligned")
  ASSERT((_sdata_kb_dma_axi % 32) == 0, "kb_dma: the AXI DMA section is not line aligned")
  .bss : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
           __bss_end__ = _ebss; } > RAM
  ._user_heap_stack : { . = ALIGN(8); PROVIDE ( end = . ); PROVIDE ( _end = . ); . = . + _Min_Stack_Size;
                        . = ALIGN(8); } > RAM
  /DISCARD/ : { libc.a ( * ) libm.a ( * ) libgcc.a ( * ) }
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
