# MIDI Grids
This is based on the Mutable Instruments Grid module. I liked the idea of it but I don't have any eurorack modules so I thought I would make a version that fits in a guitar pedal and triggers sounds via MIDI. This has been written using the C SDK for the Raspberry Pi Pico and includes a number of features the original module didn't have. MIDI Grids supports MIDI via 5-pin DIN and USB, it also supports mono sample playback directly with 8 selectable samples per voice.

## Flashing your Raspberry Pi Pico
The compiled firmware is in `build/midi_grids.uf2`. Hold **BOOTSEL** while connecting the Pico, then copy this file to RPI-RP2. Firmware updates write only the firmware region; the last 1 MB is reserved for samples and configuration files. You can access this 1 MB region by holding the Start/Stop button during power up, the region will mount in your computer like a USB drive and you can replace samples and/or edit the configuration file.

## Hardware
**GPIO Pins**
| Function | GPIO |
|---|---|
| DIN MIDI OUT / IN | GP0 / GP1, UART0, 31250 baud |
| Kick / snare / hi-hat / tempo LEDs | GP6 / GP7 / GP8 / GP9 |
| Start/Stop button to ground | GP10, internal pull-up |
| PCM5102A BCK / LRCLK / DATA | GP11 / GP12 / GP13 |
| CD74HC4067 S0–S3 / Analog Signal | GP16–GP19 / GP26 |

**Knobs**
1. Bass Drum (BD) - this controls the number of triggers (density) per measure.
2. Snare Drum (SD) - this controls the number of triggers (density) per measure.
3. High Hat (HH) - this controls the number of triggers (density) per measure.
4. X - this adjusts where the drum pattern is selected from in the pattern map.
5. Y - this adjusts where the drum pattern is selected from in the pattern map.
6. Chaos - if this knob is all the way left the drum pattern will stay the same from measure to measure, turning it to the right will increase the randomness of the pattern.
7. Tempo - this sets the tempo manually if no MIDI clock is detected.

**Start/Stop**
- Starts and stops the playing of samples and MIDI signals.
- Holding down while booting with enter USB drive mode.
- Holding down while stopped will allow you to pick a sample for each drum voice by turning it's knob. While turning the knob a preview of each sample will be played back. Release the button to save all of your choices to `SELECT.TXT` 

## MIDI Clock and Transport

- Internal tempo: 40–240 BPM; DIN and enabled USB output send 24 PPQN while playing.
- DIN and enabled USB MIDI input accept Clock, Start, Stop and Continue.
- The first source sending an accepted realtime message owns sync. The other input is ignored until that source times out; notes and other MIDI input messages are ignored. USB is a **device** connection to a computer/USB host.
- Start resets the pattern; Continue retains its position; Stop silences voices and ends outgoing clock. Clock alone changes sync source without starting playback.
- External mode tempo knob: low third = quarter speed, middle third = half speed, high third = full speed. Small hysteresis prevents division chatter.
- Division changes the drum sequence and tempo LED; outgoing clock is the incoming 24-PPQN stream at its original speed. Accepted transport messages go to both outputs.
- After **2 seconds without clock**, internal tempo resumes from the knob if playback was running. A stopped transport remains stopped. Adjust `CLOCK_TIMEOUT_US` in `include/transport.h` if needed.
- Avoid routing the forwarded USB MIDI output back into its input in your DAW.

## USB Drive Mode

1. MIDI Grids has a 1 MB region you can access by holding the Start/Stop button during power up, the region will mount in your computer like a USB drive.
2. Open the **MIDI GRIDS** volume where you are able to load samples and or edit the configuration file that is in its root directory.
3. One you have made your changes safely eject the volume. The unit will reboot and the new samples and/or configuration will loaded.

## CONFIG.TXT

Edit `CONFIG.TXT` in the drive root to set each drum's output channel (1–16) and note (0–127), for both DIN and enabled USB output:

Here is the standard MIDI configuration
```text
USB_MIDI,ON
BD,CH1,N36
SD,CH1,N38
HH,CH1,N42
```

Here is a MIDI configuration that works with the OP-Z
```text
USB_MIDI,ON
BD,CH1,N53
SD,CH2,N53
HH,CH3,N53
```
Enable incoming MIDI on OP-Z and disable `channel_one_to_active` to keep channel 1 routed to kick rather than the active track.
See https://teenage.engineering/guides/op-z/midi and
https://teenage.engineering/guides/op-z/tracks .

`USB_MIDI,ON` enables the USB MIDI interface; `USB_MIDI,OFF` disables it. Missing or invalid USB settings default to ON. Changing USB MIDI requires a reboot to update descriptors.

Lines beginning with `#` are ignored by the current parser. Put comments on their own lines; adding a comment after a setting makes that line invalid. Commenting out a drum setting restores its firmware default (CH1 and  36/N38/N42), rather than muting that drum's MIDI.

## Loading Samples via USB

If providing your own samples they must be uncompressed PCM, **mono, 16-bit, 44,100 Hz** WAV files. Each voice can have 8 samples. Bass Drum (BD) samples are named `BD01.WAV` to  `BD02.WAV`. Snare Drum (SD) samples are named `SD01.WAV` to  `SD02.WAV`. High Hat (HH) samples are named `HH01.WAV` to  `HH02.WAV`

The disk itself has 1 MiB minus filesystem overhead so you will need to be loading short samples. In addition to this all three drum samples must fit into **128 KiB** of RAM (this is about 1.49 seconds total across all three sounds).

A small file can still be rejected for stereo, 8/24-bit, float/compressed encoding, a non-44100 Hz rate, or malformed/truncated WAV chunks.

If you are using a Pico that previously had MicroPython installed you may need to format the 1 MB region. **An old MicroPython installation may leave incompatible data in this region**, so the first installation may need a reset: open the Pico CDC serial port and send `format` followed by Enter. This explicitly **erases the sample partition and restores the three supplied factory WAVs**. 

## Serial Debug
It is possible to debug MIDI Grids via a serial monitor. Open the Pico USB CDC port in a serial monitor (115200 baud is fine; USB CDC does not use a physical UART baud rate). Send commands with a newline/Enter:

- `status`: selected sample numbers, loaded frame counts, MIDI settings, and raw knobs.
- `samples`: discovered numbered slots and the actual drive-root filenames and sizes.

While stopped, hold Start/Stop and turn a drum knob. Each attempted selection logs its filename, file size, WAV encoding/channels/rate/bit depth, PCM size, and RAM usage. Rejected samples print the specific reason and the previous selection is restored. 

Send `status` for transport, per-drum MIDI settings and sample numbers, raw pots, sample frame counts, division and dropped-message diagnostics. Send `test` to play the selected bass drum and send its configured MIDI note, even while stopped. All seven pot directions are inverted to match the hardware: clockwise increases values.

## Build

Use the Raspberry Pi Pico VS Code extension to import this CMake project, choose `pico`, and build; or use an installed Pico SDK and ARM compiler:
```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```
Validated here with Pico SDK 2.3.1 and Arm GNU toolchain 15.2.Rel1. SDK's TinyUSB submodule and picotool must be installed. No network dependencies are fetched by this project itself. Rebuild embedded defaults after modifying `samples/` or map data:
```sh
python3 tools/embed_assets.py
cmake --build build
```
Run portable host tests with `sh tools/test.sh` (C compiler and Python 3 required). They cover interpolation and engine parity, external source locking, 1/2/4 divisions, Start/Stop/Continue, clock loss, FAT provisioning, ownership handoff, sample loading, and preservation of damaged storage. See `docs/HARDWARE_VALIDATION.md` for bench tests.

## Source and Licensing

The Grids map/engine was derived from Emilie Gillet / Mutable Instruments and is licensed GPL-3.0-or-later. See `LICENSE.md`. PVS supplied the flash-disk and MSC ownership implementation; FatFs remains under its included upstream notice. References:

- https://github.com/pichenettes/eurorack/tree/master/grids
- https://pichenettes.github.io/mutable-instruments-documentation/modules/grids/

## Prepare Samples with FFmpeg

`tools/convert_samples.py` converts numbered drum WAVs in a folder to **mono, 16-bit PCM, 44,100 Hz**, using FFmpeg with triangular dithering. Originals stay untouched; output filenames use uppercase BD/SD/HH plus 01–08. Conversion preserves full lengths and does not normalize volume. Unrecognized WAV names are skipped.
Python 3 and `ffmpeg` on PATH are required; `ffprobe` is not required.

From this project's folder, run:

```sh
python3 tools/convert_samples.py "/path/to/original-samples" "samples-converted"
```

The script verifies every output WAV, reports PCM/file sizes, and checks the largest sample of each drum together against the shared 128 KiB RAM limit. A full library containing all three drum types is needed to check every possible combination. It also estimates library disk usage with 512-byte clusters and a conservative
32 KiB reserve for filesystem/settings/host files; actual free drive space remains the final check. Exit status is 0 when checks pass, 1 for errors, or 2 when conversion succeeds but sizes exceed a limit. Outputs remain available for inspection on status 2.

If RAM checks fail, shorten longer samples in an audio editor or explicitly cap all sounds at 490 ms (this cuts off any longer tails):
```sh
python3 tools/convert_samples.py "/path/to/original-samples" "samples-converted" --overwrite --max-duration 0.49
```
Three 490 ms sounds fit in RAM. A library of many sounds can still exceed the 1 MiB disk; reduce its file count or lengths if the disk check fails. Existing output WAVs are protected unless `--overwrite` is supplied for matching filenames. Input and output folders must differ.

Reboot the Pico holding Start/Stop, copy the converted WAVs to the drive root, replace the matching old files, then safely eject. Keep `CONFIG.TXT` and `SELECT.TXT`. Verify selections using the serial monitor. Run  conversion integration checks with:
```sh
python3 tests/test_convert_samples.py
```
