FatFs R0.15 (ChaN), copied unchanged from the FatFs source bundled with Pico SDK
2.3.1 / TinyUSB. The copyright and redistribution terms are at the top of ff.c,
ff.h and ffunicode.c. Only ffconf.h is configured for PVS: one writable volume,
FAT12/16/32, fixed 512-byte sectors, long filenames up to 64 characters, static
LFN workspace, no heap, no RTC, one shared sector window, format and label APIs.

All filesystem calls are serialized on core 0. USB edit mode unmounts FatFs and
gives the host exclusive raw-block access; only host eject permits remounting.
