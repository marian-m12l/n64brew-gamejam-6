/*
Stack uses the last 64KiB of RDRAM (either 0x803f0000 or 0x807f0000)
Malloc heap normally starts right after __bss_end --> we move it to 0x80300000 to reserve space for our own heaps
Expansion pak RDRAM will be used for our own extra heaps, if available.
*/

PHDRS
{
    /* We don't want to load anything in there */
    persistent PT_LOAD AT ( 0xa0100000 );
    heaps PT_LOAD AT ( 0xa0101000 );
    cached_heaps PT_LOAD AT ( 0x80101000 );
}

SECTIONS {
    /* Make sure the ROM will fit before our persistent data */
    ASSERT(end <= 0x80100000, "ERROR: should leave all memory free starting at 1MiB offset")

    __actual_bss_end = .;

    . = 0xa0100000;
    .persistent (NOLOAD) : {
        *(.persistent)
        . = ALIGN(8);
    } : persistent

    /* Make sure persistent data will fit before our heaps */
    ASSERT(. <= 0xa0101000, "ERROR: too much persistent data")

    . = 0xa0101000;
    .rdram_heap (NOLOAD) : {
        *(.rdram_heap)
        . = ALIGN(8);
        __rdram_heap_end = .;
    } : heaps

    /* Make sure our heaps will fit before malloc heap */
    ASSERT(. <= 0xa0300000, "ERROR: custom heap too large")

    . = 0xa0400000;
    .rdram_expansion_heap (NOLOAD) : {
        *(.rdram_expansion_heap)
        . = ALIGN(8);
        __rdram_expansion_heap_end = .;
    }

    /* Make sure our extra heaps will fit before stack */
    ASSERT(. <= 0xa07f0000, "ERROR: extra heap too large")

    . = 0x80101000;
    .cached_heap (NOLOAD) : {
        *(.cached_heap)
        . = ALIGN(8);
        __cached_heap_end = .;
    } : cached_heaps

    /* Make sure our heaps will fit before malloc heap */
    ASSERT(. <= 0x80300000, "ERROR: custom heap too large")

    . = 0x80400000;
    .cached_expansion_heap (NOLOAD) : {
        *(.cached_expansion_heap)
        . = ALIGN(8);
        __cached_expansion_heap_end = .;
    }

    /* Make sure our extra heaps will fit before stack */
    ASSERT(. <= 0x807f0000, "ERROR: extra heap too large")

    . = 0x80300000;
    __bss_end = .;
}

INSERT AFTER .bss;
