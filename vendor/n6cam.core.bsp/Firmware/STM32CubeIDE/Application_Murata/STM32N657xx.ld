/* Entry Point */
ENTRY(Reset_Handler)

/* Specify the memory areas */
MEMORY
{
  /* SRAM */
  CODE   		(xr ): ORIGIN = 0x34000400, LENGTH = 1023K
  SRAM  		(xrw): ORIGIN = 0x34100000, LENGTH =  768K
  SRAM_UNCACHED	(xrw): ORIGIN = 0x341C0000, LENGTH =  256K
  SRAM_NPU  	(xrw): ORIGIN = 0x34200000, LENGTH = 2048K

  /* PSRAM is 32M, 0x90000000..0x92000000, but a slice in it is not ours: the
     generated model hardcodes its read/write pool at 0x91a00000 and uses the
     first 512K of it ("offset=0x91a00000 absolute_mode size=6291448" and
     "global pool 4 is 512.00 KB" in Model/network.c; the pool's declared 6M
     is what the allocator was offered, the 512K is what it took, and
     ATON_LIB_PHYSICAL_TO_VIRTUAL_ADDR is the identity macro, so these are
     real addresses).

     Nothing in the link knew that. PSRAM was one 32M region, .psram_bss grew
     from 0x90000000 straight through 0x91a00000, and _live_rgb ended up
     sitting on the NN's working memory: every tiled sweep wrote a picture
     through the model's buffers, and `frame grab live` came back with a band
     of float32 activations where the top of the frame should be.

     So PSRAM now stops at the pool, NN_POOL holds it and nothing is placed
     there, and PSRAM_HI is the rest above it. Overflowing either is a link
     error instead of a corrupted picture. */
  PSRAM   		(xrw): ORIGIN = 0x90000000, LENGTH =   26M	/* to 0x91a00000     */
  NN_POOL 		( rw): ORIGIN = 0x91a00000, LENGTH =  512K	/* model's, keep clear */
  PSRAM_HI		(xrw): ORIGIN = 0x91a80000, LENGTH = 5632K	/* the rest, above it */

  /* FLASH */
  FLASH       	(xrw): ORIGIN = 0x70000000, LENGTH =   64M	/* Use for reference */
  FSBL        	(xrw): ORIGIN = 0x70000000, LENGTH =  512K
  REGISTRY		( rw): ORIGIN = 0x703E0000, LENGTH =  128K
  SLOT1_APP		(xrw): ORIGIN = 0x70400000, LENGTH = 1024K
  SLOT1_WIFI	( rw): ORIGIN = 0x70500000, LENGTH = 1024K
  SLOT1_WEIGHTS	( rw): ORIGIN = 0x70600000, LENGTH =   28M
  SLOT2			( rw): ORIGIN = 0x72200000, LENGTH =   30M
}

/* Heap/Stack */
_min_heap  = 0x200;
_min_stack = 0x800;

/* Highest address of the user mode stack */
_estack = ORIGIN(SRAM) + LENGTH(SRAM);
_sstack = _estack - _min_stack;

/* PSRAM start/end */
_spsram = ORIGIN(SRAM);
_epsram = _spsram + LENGTH(SRAM);

/* FLASH start/end */
_sflash = ORIGIN(FLASH);
_eflash = _sflash + LENGTH(FLASH);

/* Registry start/end */
_sregistry = ORIGIN(REGISTRY);
_eregistry = _sregistry + LENGTH(REGISTRY);

/* Define output sections */
SECTIONS
{
  /* Startup code */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) 	/* Startup code */
    . = ALIGN(4);
  } >CODE

  /* Program code */
  .text :
  {
    . = ALIGN(4);
    *(.text)           		/* .text sections (code) */
    *(.text*)          		/* .text* sections (code) */
    *(.glue_7)         		/* Glue arm to thumb code */
    *(.glue_7t)        		/* Glue thumb to arm code */
    *(.eh_frame)
    *(.RamFunc)        		/* .RamFunc sections */
    *(.RamFunc*)       		/* .RamFunc* sections */

    KEEP (*(.init))
    KEEP (*(.fini))
    KEEP (*(.keep_me))

    . = ALIGN(4);
    _etext = .;        		/* Define a global symbols at end of code */
  } >CODE

  /* Constant data */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         		/* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        		/* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >CODE

  /* System code */
  .gnu.sgstubs :
  {
    . = ALIGN(4);
    *(.gnu.sgstubs*)   		/* Secure Gateway stubs */
    . = ALIGN(4);
  } >CODE

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >CODE

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >CODE

  .preinit_array     :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >CODE

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >CODE

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >CODE

  /* Data */
  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        		/* Create a global symbol at data start */
    *(.data)           		/* .data sections */
    *(.data*)          		/* .data* sections */

    . = ALIGN(4);
    _edata = .;        		/* Define a global symbol at data end */
  } >SRAM AT > CODE

  /* Uninitialized data */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;         		/* Define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         		/* Define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >SRAM

  /* Hep/Stack */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _min_heap;
    . = . + _min_stack;
    . = ALIGN(8);
  } >SRAM

  /* Extras START -----------*/

  /* SRAM uncached */
  .uncached_bss (NOLOAD) :
  {
    . = ALIGN(4);
    __uncached_bss_start__ = .;
    *(.uncached_bss)
    *(.uncached_bss*)
    . = ALIGN(4);
    __uncached_bss_end__ = .;
  } >SRAM_UNCACHED

  /* PSRAM */
  .psram_bss (NOLOAD) :
  {
  	. = ALIGN(4);
    __psram_bss_start__ = .;
    *(.psram_bss)
    *(.psram_bss*)
    . = ALIGN(4);
    __psram_bss_end__ = .;
  } >PSRAM

  /* PSRAM above the model's pool. Same memory, reached over the reserved
     512K; for buffers that do not fit under it. */
  .psram_hi_bss (NOLOAD) :
  {
  	. = ALIGN(4);
    __psram_hi_bss_start__ = .;
    *(.psram_hi_bss)
    *(.psram_hi_bss*)
    . = ALIGN(4);
    __psram_hi_bss_end__ = .;
  } >PSRAM_HI

  /* WIFI Stack */
  .wifi_stack (NOLOAD) :
  {
  	. = ALIGN(4);
    __wifi_stack_start__ = .;
    *(.whd_fw)
    *(.whd_fw*)
    . = ALIGN(4);
    __wifi_stack_end__ = .;
  } >SLOT1_WIFI

  /* Extras END -------------*/

  /* Remove information from the compiler libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
