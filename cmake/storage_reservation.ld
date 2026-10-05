/* The last MiB of physical QSPI flash belongs exclusively to the editable FAT
 * volume. Keep the SDK's real flash size visible to the flash block driver.
 * This fragment is included after the SDK's __flash_binary_end definition. */
ASSERT(LENGTH(FLASH) >= 2M, "PVS requires at least 2 MiB of QSPI flash")
ASSERT(__flash_binary_end <= ORIGIN(FLASH) + LENGTH(FLASH) - 1M,
       "PVS firmware overlaps the reserved 1 MiB user filesystem")
