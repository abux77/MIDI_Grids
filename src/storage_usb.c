#include "storage.h"
#include "flash_disk.h"
#include "tusb.h"
#include <string.h>
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) {
    (void)lun;
    memcpy(vendor, "GRIDS   ", 8);
    memcpy(product, "Drum Samples    ", 16);
    memcpy(revision, "0200", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (lun == 0 && storage_host_ready()) return true;
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *blocks, uint16_t *block_size) {
    (void)lun;
    *blocks = PVS_DISK_SECTOR_COUNT;
    *block_size = PVS_DISK_SECTOR_SIZE;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    return lun == 0 && storage_host_ready();
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (lun != 0) return false;
    if (load_eject && !start) return storage_host_eject();
    return storage_host_ready();
}

static bool transfer_valid(uint8_t lun, uint32_t lba, uint32_t offset, uint32_t size,
                           uint32_t *absolute) {
    uint64_t address = (uint64_t)lba * PVS_DISK_SECTOR_SIZE + offset;
    if (!tud_msc_test_unit_ready_cb(lun)) return false;
    if (address > PVS_STORAGE_BYTES || size > PVS_STORAGE_BYTES - address) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
        return false;
    }
    *absolute = (uint32_t)address;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
    uint32_t address;
    if (!transfer_valid(lun, lba, offset, size, &address)) return -1;
    if (!flash_disk_read(address, buffer, size)) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0);
        return -1;
    }
    return (int32_t)size;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
    uint32_t address;
    if (!transfer_valid(lun, lba, offset, size, &address)) return -1;
    /* Persist before acknowledging the write, not after TinyUSB has sent CSW.
     * A 4 KiB MSC buffer coalesces host transfers at flash erase granularity. */
    if (!flash_disk_write(address, buffer, size) || !flash_disk_sync()) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
        return -1;
    }
    return (int32_t)size;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t size) {
    (void)buffer;
    (void)size;
    if (command[0] == 0x35) { /* SCSI SYNCHRONIZE CACHE (10) */
        if (tud_msc_test_unit_ready_cb(lun) && flash_disk_sync()) return 0;
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
        return -1;
    }
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    return -1;
}
