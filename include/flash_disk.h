#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Physical board flash size stays unchanged; linker reserves its final MiB. */
#define PVS_STORAGE_BYTES (1024u * 1024u)
#define PVS_DISK_SECTOR_SIZE 512u
#define PVS_DISK_SECTOR_COUNT (PVS_STORAGE_BYTES / PVS_DISK_SECTOR_SIZE)
bool flash_disk_init(void);
bool flash_disk_is_blank(void);
bool flash_disk_read(uint32_t offset, void *buffer, size_t size);
bool flash_disk_write(uint32_t offset, const void *buffer, size_t size);
bool flash_disk_sync(void);

/* Internal USB ownership boundary. No FatFs call occurs in MSC callbacks. */
bool storage_host_ready(void);
bool storage_host_eject(void);
void storage_service_remount(void);
