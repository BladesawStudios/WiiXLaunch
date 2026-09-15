/* Linker script for a .wxlm module.
 *
 * A module is linked at 0 and relocated by the loader, exactly like the host
 * payload - see scripts/cemu.ld, which this mirrors. The differences are that a
 * module has no bootstrap to exclude from relocation, and that .init_array is
 * KEPT and bracketed, because the loader is the only thing that will ever run a
 * module's static constructors: the flat build has no crt0 and the host's
 * bootstrap never walks one.
 */

OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(WiiXLaunch_ModEntry)

SECTIONS
{
    . = 0x0;

    /* Declared patches. Its own output section so scripts/wxlm.py can lift
     it out by name, and KEEP because nothing in the module references
     these records - they are read by a script and consumed by the host,
     which is the same shape as the `used` rule in docs/framework/modules.md. */
  .wxlm.patches : { KEEP(*(.wxlm.patches)) }

  .text : {
        *(.text*)
        *(.init)
        *(.fini)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
        *(.sdata*)
        *(.got2*)
    }

    /* Bracketed so scripts/wxlm.py can find the range and record it in the
       header. Two things here are load-bearing and both were found by building
       it rather than by reading:

       KEEP, because nothing references these pointers - the loader finds them
       through the header - so a linker is otherwise entitled to discard them.

       PLAIN ASSIGNMENT rather than PROVIDE for the bracket symbols, for the
       same reason: PROVIDE only defines a symbol that something references, and
       nothing does. With PROVIDE the section was emitted and the range symbols
       were not, so the header recorded an empty .init_array and the module's
       constructor would never have run. */
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        /* devkitPPC's powerpc-eabi GCC emits .ctors, not .init_array, and does
           not accept -fuse-init-array to change that - so both are gathered
           here and the loader walks the result front to back.

           .ctors is conventionally called in REVERSE by a normal crt, so a
           module whose constructors depend on running in a particular order
           relative to each other would see a different order here than on a
           hosted toolchain. That is not something this loader can promise, and
           a module should not rely on it: constructors here must be
           order-independent. One constructor, the common case, is unaffected. */
        KEEP(*(SORT_BY_NAME(.ctors.*)))
        KEEP(*(.ctors))
        __init_array_end = .;
    }

    .bss : {
        *(.bss*)
        *(.sbss*)
        *(COMMON)
    }

    .rela.text : { *(.rela.text .rela.text.* .rela.gnu.linkonce.t.*) }
    .rela.data : { *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*) }
    .rela.rodata : { *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*) }
    .rela.init_array : { *(.rela.init_array) }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
    }
}
