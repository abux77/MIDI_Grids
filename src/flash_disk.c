#include "flash_disk.h"
#include <string.h>
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

_Static_assert(PICO_FLASH_SIZE_BYTES > PVS_STORAGE_BYTES, "Storage needs at least 2 MiB flash");
_Static_assert(PVS_STORAGE_BYTES % FLASH_SECTOR_SIZE == 0, "Storage alignment");
#define STORAGE_OFFSET (PICO_FLASH_SIZE_BYTES - PVS_STORAGE_BYTES)
extern char __flash_binary_end;

/* One erase-sector cache absorbs the host's 512-byte sector writes. All use is
 * serialized on core 0. Core 1 is unused; future multicore code must cooperate
 * with pico_flash lockout before accessing this backend. Audio is stopped before
 * any write. DMA must never read XIP while a flash operation is in progress. */
static uint8_t cache[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
static uint32_t cache_offset = UINT32_MAX;
static bool dirty;
static bool available;

static const uint8_t *flash_bytes(void) {
    return (const uint8_t *)(XIP_BASE + STORAGE_OFFSET);
}

static void __not_in_flash_func(commit_sector)(void *unused) {
    (void)unused;
    flash_range_erase(STORAGE_OFFSET + cache_offset, FLASH_SECTOR_SIZE);
    flash_range_program(STORAGE_OFFSET + cache_offset, cache, FLASH_SECTOR_SIZE);
}

bool flash_disk_sync(void) {
    if (!available) return false;
    if (!dirty) return true;
    if (flash_safe_execute(commit_sector, NULL, 1000) != PICO_OK) return false;
    if (memcmp(flash_bytes() + cache_offset, cache, sizeof(cache))) return false;
    dirty = false;
    return true;
}

bool flash_disk_init(void) {
    available = (uintptr_t)&__flash_binary_end <= XIP_BASE + STORAGE_OFFSET;
    return available;
}

bool flash_disk_is_blank(void) {
    if (!available || dirty) return false;
    /* A damaged filesystem must never be mistaken for an uninitialized one. */
    const uint32_t *p = (const uint32_t *)flash_bytes();
    for (size_t i = 0; i < PVS_STORAGE_BYTES / sizeof(*p); ++i)
        if (p[i] != UINT32_MAX) return false;
    return true;
}

static bool valid_range(uint32_t offset, size_t size) {
    return available && offset <= PVS_STORAGE_BYTES && size <= PVS_STORAGE_BYTES - offset;
}

bool flash_disk_read(uint32_t offset, void *buffer, size_t size) {
    if (!buffer || !valid_range(offset, size)) return false;
    uint8_t *destination = buffer;
    while (size) {
        uint32_t sector = offset & ~(FLASH_SECTOR_SIZE - 1u);
        size_t inside = offset - sector;
        size_t count = FLASH_SECTOR_SIZE - inside;
        if (count > size) count = size;
        const uint8_t *source = sector == cache_offset ? cache : flash_bytes() + sector;
        memcpy(destination, source + inside, count);
        destination += count;
        offset += (uint32_t)count;
        size -= count;
    }
    return true;
}

bool flash_disk_write(uint32_t offset, const void *buffer, size_t size) {
    if (!buffer || !valid_range(offset, size)) return false;
    const uint8_t *source = buffer;
    while (size) {
        uint32_t sector = offset & ~(FLASH_SECTOR_SIZE - 1u);
        size_t inside = offset - sector;
        size_t count = FLASH_SECTOR_SIZE - inside;
        if (count > size) count = size;
        if (cache_offset != sector) {
            if (!flash_disk_sync()) return false;
            memcpy(cache, flash_bytes() + sector, sizeof(cache));
            cache_offset = sector;
        }
        if (memcmp(cache + inside, source, count)) {
            memcpy(cache + inside, source, count);
            dirty = true;
        }
        source += count;
        offset += (uint32_t)count;
        size -= count;
    }
    return true;
}
