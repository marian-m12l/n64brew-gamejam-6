PHDRS
{
    /* We don't want to load anything in there */
    persistent PT_LOAD AT ( 0x80100000 );
    cached_heaps PT_LOAD AT ( 0x80101000 );
    heaps PT_LOAD AT ( 0xa0101000 );
}

SECTIONS {
    /* Make sure the ROM will fit before our persistent data */
    ASSERT(end <= 0x80100000, "ERROR: should leave all memory free starting at 1MiB offset")

    . = 0x80100000;
    .persistent (NOLOAD) : {
        *(.persistent)
        . = ALIGN(8);
    } : persistent

    /* Make sure persistent data will fit before our heaps */
    ASSERT(. <= 0x80101000, "ERROR: too much persistent data")

    . = 0x80101000;
    .cached_1mib (NOLOAD) : {
        *(.cached_1mib)
        . = ALIGN(8);
    } : cached_heaps

    . = 0x80301000;
    .cached_3mib (NOLOAD) : {
        *(.cached_3mib)
        . = ALIGN(8);
    }

    . = 0x80401000;
    .cached_4mib (NOLOAD) : {
        *(.cached_4mib)
        . = ALIGN(8);
    }

    . = 0x80701000;
    .cached_7mib (NOLOAD) : {
        *(.cached_7mib)
        . = ALIGN(8);
    }

    . = 0xa0101000;
    .rdram_1mib (NOLOAD) : {
        *(.rdram_1mib)
        . = ALIGN(8);
    } : heaps

    . = 0xa0301000;
    .rdram_3mib (NOLOAD) : {
        *(.rdram_3mib)
        . = ALIGN(8);
    }

    . = 0xa0401000;
    .rdram_4mib (NOLOAD) : {
        *(.rdram_4mib)
        . = ALIGN(8);
    }

    . = 0xa0701000;
    .rdram_7mib (NOLOAD) : {
        *(.rdram_7mib)
        . = ALIGN(8);
    }
}

/* Inserting after bss --> DO NOT USE MALLOC AS IT WILL INITIALIZE THE HEAP AT __bss_end */
INSERT AFTER .bss;
