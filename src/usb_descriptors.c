#include <string.h>
#include "pico/unique_id.h"
#include "tusb.h"

/* TinyUSB example VID/PID for this development firmware. */
static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xcafe, .idProduct = 0x4017, .bcdDevice = 0x0200,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_descriptor;
}

enum { INTERFACE_CDC, INTERFACE_CDC_DATA, INTERFACE_MSC, INTERFACE_MIDI, INTERFACE_MIDI_STREAM, INTERFACE_COUNT };
extern bool usb_midi_enabled;
enum { CONFIG_SIZE = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN + TUD_MIDI_DESC_LEN };
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, INTERFACE_COUNT, 0, CONFIG_SIZE, 0, 100),
    TUD_CDC_DESCRIPTOR(INTERFACE_CDC, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_MSC_DESCRIPTOR(INTERFACE_MSC, 5, 0x03, 0x83, 64),
    TUD_MIDI_DESCRIPTOR(INTERFACE_MIDI, 6, 0x04, 0x84, 64),
};
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    static uint8_t without_midi[CONFIG_SIZE - TUD_MIDI_DESC_LEN];
    if (usb_midi_enabled) return configuration;
    memcpy(without_midi, configuration, sizeof(without_midi));
    without_midi[2] = sizeof(without_midi) & 255;
    without_midi[3] = sizeof(without_midi) >> 8;
    without_midi[4] = INTERFACE_MIDI;
    return without_midi;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language) {
    (void)language;
    static uint16_t descriptor[40];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    static const char *const strings[] = {
        "", "MIDI Grids", "Pico MIDI Grids", NULL,
        "Grids Console", "Grids Samples", "Grids MIDI",
    };
    size_t length;
    if (index == 0) {
        descriptor[1] = 0x0409;
        length = 1;
    } else {
        if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
        const char *text = strings[index];
        if (index == 3) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            text = serial;
        }
        length = strlen(text);
        if (length > 39) length = 39;
        for (size_t i = 0; i < length; ++i) descriptor[i + 1] = (uint8_t)text[i];
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
    return descriptor;
}
