/* Linker script for an AArch64 .wxlm module (Switch).
 *
 * The PowerPC one is scripts/wxlm_mod.ld and this mirrors it section for
 * section. The differences are all architecture, not policy:
 *
 *   - elf64, little-endian, and an eight-byte world.
 *   - No .sdata/.sbss/.got2. Those are PowerPC small-data sections; aarch64 has
 *     no equivalent and -msdata=none is not even a valid flag there.
 *   - .init_array only. devkitA64's GCC emits it, unlike devkitPPC which emits
 *     .ctors and refuses -fuse-init-array. .ctors is still gathered here so a
 *     module built by some other toolchain does not silently lose its
 *     constructors.
 *
 * ALIGNMENT MATTERS MORE HERE THAN IT LOOKS. A64 code reaches its own data with
 * adrp+add, which is PC-relative and carries no runtime fixup, so it stays
 * correct only if the image moves by a whole number of pages. The loader
 * page-aligns aarch64 images for that reason; the sections below are aligned to
 * 8 so nothing inside the image is misaligned relative to the start.
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(WiiXLaunch_ModEntry)

SECTIONS
{
    . = 0x0;

    /* Declared patches. Its own output section so scripts/wxlm.py can lift it
       out by name, and KEEP because nothing in the module references these
       records - they are read by a script and consumed by the host. */
    .wxlm.patches : { KEEP(*(.wxlm.patches)) }

    . = ALIGN(8);
    .text : {
        *(.text*)
        *(.init)
        *(.fini)
    }

    . = ALIGN(8);
    .rodata : {
        *(.rodata*)
    }

    . = ALIGN(8);
    .data : {
        *(.data*)
        *(.got)
        *(.got.plt)
    }

    /* Bracketed so scripts/wxlm.py can find the range and record it in the
       header. KEEP and PLAIN ASSIGNMENT for the same two reasons spelled out in
       wxlm_mod.ld: nothing references these pointers, so a linker may discard
       the section, and PROVIDE would define no symbol for a range nobody names
       - which produced a header saying "no constructors" for a module that had
       one. */
    . = ALIGN(8);
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_NAME(.ctors.*)))
        KEEP(*(.ctors))
        __init_array_end = .;
    }

    . = ALIGN(8);
    .bss : {
        *(.bss*)
        *(COMMON)
    }

    .rela.text : { *(.rela.text .rela.text.* .rela.gnu.linkonce.t.*) }
    .rela.data : { *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*) }
    .rela.rodata : { *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*) }
    .rela.init_array : { *(.rela.init_array) }
    .rela.got : { *(.rela.got) }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
