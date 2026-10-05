# Pico MIDI Grids — C SDK port

Standalone C11 firmware for the original Raspberry Pi Pico (RP2040, 2 MiB flash).
The supplied MicroPython v0.6 and PVS projects have not been changed.

## Flash and run

The compiled firmware is `build/midi_grids.uf2`. Hold BOOTSEL while connecting the
Pico, then copy this file to RPI-RP2. Firmware updates write only the firmware
region; the last 1 MiB is reserved for samples and protected by a linker assertion.
Back up the old MicroPython program and samples before replacing its firmware.

Power-up is stopped. A short Start/Stop press toggles transport **on release**,
with USB storage available only when Start/Stop is held during boot.
DIN MIDI IN uses GP1; former MIDI channel DIP pins GP2–GP5 are unused.

| Function | GPIO |
|---|---|
| DIN MIDI OUT / IN | GP0 / GP1, UART0, 31250 baud |
| Unused (former MIDI DIP inputs) | GP2–GP5 |
| Kick / snare / hi-hat / tempo LEDs | GP6 / GP7 / GP8 / GP9 |
| Start/Stop button to ground | GP10, internal pull-up |
| PCM5102A BCK / LRCLK / DATA | GP11 / GP12 / GP13 |
| Unused (former USB MIDI enable switch) | GP14 |
| CD74HC4067 S0–S3 / analog SIG | GP16–GP19 / GP26 |

Mux channels 0–6 are X, Y, kick density, snare density, hi-hat density, chaos,
and tempo. Default MIDI notes are 36, 38, 42 on channel 1, with velocity 100 or accented velocity 127; each drum can be configured independently.
The 32-step map advances every three MIDI clocks, as v0.6 does. The same map data,
signed interpolation rounding, density threshold and chaos amount are preserved;
chaos uses a C PRNG, so individual random variations differ from MicroPython.
Audio uses a three-voice, retriggering mixer and 44.1 kHz PIO/DMA I²S, with the
mono mix duplicated to both DAC channels. No MicroPython runtime/native modules
are required. ADC mux scanning runs cooperatively on core 0; core 1 stays idle.

## MIDI clock and transport

- Internal tempo: 40–240 BPM; DIN and enabled USB output send 24 PPQN while playing.
- DIN and enabled USB MIDI input accept Clock, Start, Stop and Continue.
- The first source sending an accepted realtime message owns sync. The other
  input is ignored until that source times out; notes and other MIDI input
  messages are ignored. USB is a **device** connection to a computer/USB host.
- Start resets the pattern; Continue retains its position; Stop silences voices
  and ends outgoing clock. Clock alone changes sync source without starting playback.
- External mode tempo knob: low third = quarter speed, middle third = half speed,
  high third = full speed. Small hysteresis prevents division chatter.
- Division changes the drum sequence and tempo LED; outgoing clock is the incoming
  24-PPQN stream at its original speed. Accepted transport messages go to both outputs.
- After **2 seconds without clock**, internal tempo resumes from the knob if
  playback was running. A stopped transport remains stopped. Adjust
  `CLOCK_TIMEOUT_US` in `include/transport.h` if needed.
- Avoid routing the forwarded USB MIDI output back into its input in your DAW.

GP1 needs your MIDI input interface's **3.3 V logic output** and common logic
ground. The DIN connector is connected through your optocoupler circuit, not
wired directly to GP1. DIN output hardware remains as in your existing build.

## Replace samples over USB

1. Hold Start/Stop while powering on or resetting the Pico. The USB sample drive becomes available; release the button.
2. Open the **MIDI GRIDS** volume and replace `BD01.WAV`, `SD01.WAV`, `HH01.WAV`
   in its root directory. Add choices numbered 01–08 for each drum, such as `BD08.WAV`.
3. Safely eject the volume. Firmware remounts it and reloads the samples automatically.
4. Press Start/Stop to resume, or send MIDI Start/Continue. A reboot is not required.

The sample drive and CDC serial remain accessible with `USB_MIDI,OFF`.
CDC serial is available for diagnostics; it is no longer a Python REPL.

WAVs must be uncompressed PCM, **mono, 16-bit, 44,100 Hz**. The three PCM payloads
combined must fit **128 KiB** (about 1.49 seconds total across all three sounds).
The disk itself has 1 MiB minus filesystem overhead. When the filesystem is unavailable, embedded factory WAVs play without modifying
the existing disk. With a mounted filesystem, invalid, missing or over-budget
samples mute the affected voice; MIDI sequencing continues. Larger sample streaming
is outside this version. Samples are loaded into RAM before playback, and audio DMA
is disabled throughout USB editing so it cannot race flash erase/program operations.

Blank storage is automatically formatted and populated with the supplied v0.6 WAVs.
Existing nonblank or damaged storage is preserved. **An old MicroPython installation
may leave incompatible data in this region**, so first installation may need a reset:
open the Pico CDC serial port and send `format` followed by Enter. This explicitly
**erases the sample partition and restores the three supplied factory WAVs**. It
stops audio first and is refused while the USB drive is owned by the computer.
Edit `CONFIG.TXT` in the drive root to set each drum's output channel (1–16)
and note (0–127), for both DIN and enabled USB output:

```text
USB_MIDI,ON
BD,CH1,N36
SD,CH1,N38
HH,CH1,N42
```

Drum settings and sample choices load at boot and after eject. `USB_MIDI,ON`
enables the USB MIDI interface; `USB_MIDI,OFF` disables it. Missing or invalid USB
settings default to ON. Changing USB MIDI requires a reboot to update descriptors;
CDC serial and the sample drive remain available either way. `status` reports the
active USB setting and whether the edited configuration needs a reboot. On an existing sample drive, rename the old
`kick.wav`, `snare.wav`, and `hihat.wav` files to `BD01.WAV`, `SD01.WAV`, and
`HH01.WAV`, and copy the supplied `samples/CONFIG.TXT` to the drive root. Missing or invalid entries use the defaults
shown above. GP2–GP5 MIDI DIP switches are no longer used. GP14 is also unused; USB MIDI is configured in the file.

While stopped, hold Start/Stop and turn a drum's density knob to choose its sample.
The knob spans the available numbered files in order, skipping missing numbers.
A successful choice previews through the audio output, lights that drum's LED, and
prints its filename over CDC; previews do not send MIDI notes. Release to save all
choices to `SELECT.TXT` and remain stopped. The next ordinary press starts playback.
Only the three selected samples occupy the shared 128 KiB RAM arena. Invalid or
oversized choices are rejected and the previous selection is restored. Missing
saved files fall back to the lowest available number for that drum.

Selections survive reboot. Saving occurs once on button release, with audio DMA
suspended during flash writes. After selection, density knobs use pickup: move back
to the previous density position before that knob changes density again. Sample
selection is disabled while playing. USB storage requires a reboot with the button
held; a long hold during normal operation no longer exposes the drive.

Send `status` for transport, per-drum MIDI settings and sample numbers, raw pots,
sample frame counts, division and dropped-message diagnostics. Send `test` to play
the selected bass drum and send its configured MIDI note, even while stopped.
All seven pot directions are inverted to match the hardware: clockwise increases values.

## Build

Use the Raspberry Pi Pico VS Code extension to import this CMake project, choose
`pico`, and build; or use an installed Pico SDK and ARM compiler:

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

Validated here with Pico SDK 2.3.1 and Arm GNU toolchain 15.2.Rel1. SDK's TinyUSB
submodule and picotool must be installed. No network dependencies are fetched by
this project itself. Rebuild embedded defaults after modifying `samples/` or map data:

```sh
python3 tools/embed_assets.py
cmake --build build
```

Run portable host tests with `sh tools/test.sh` (C compiler and Python 3 required).
They cover interpolation and engine parity, external source locking, 1/2/4 divisions,
Start/Stop/Continue, clock loss, FAT provisioning, ownership handoff, sample loading,
and preservation of damaged storage. See `docs/HARDWARE_VALIDATION.md` for bench tests.

## Source and licensing

The Grids map/engine derives from Emilie Gillet / Mutable Instruments and is licensed
GPL-3.0-or-later. See `LICENSE.md`. PVS supplied the flash-disk and MSC ownership
implementation; FatFs remains under its included upstream notice. References:

- https://github.com/pichenettes/eurorack/tree/master/grids
- https://pichenettes.github.io/mutable-instruments-documentation/modules/grids/

## Sample diagnostics over USB serial

Open the Pico USB CDC port in a serial monitor (115200 baud is fine; USB CDC does
not use a physical UART baud rate). Send commands with a newline/Enter:

- `status`: selected sample numbers, loaded frame counts, MIDI settings, and raw knobs.
- `samples`: discovered numbered slots and the actual drive-root filenames and sizes.

While stopped, hold Start/Stop and turn a drum knob. Each attempted selection logs
its filename, file size, WAV encoding/channels/rate/bit depth, PCM size, and RAM usage.
Rejected samples print the specific reason and the previous selection is restored.
The same rejected slot is attempted only once until you move to another slot or
release and hold the button again. Only selected samples load into RAM; the full
library stays on the drive. A small file can still be rejected for stereo, 8/24-bit,
float/compressed encoding, a non-44100 Hz rate, or malformed/truncated WAV chunks.
Use mono, 16-bit PCM, 44100 Hz exports. `samples` requires the USB drive to be ejected
so firmware owns the filesystem. Avoid `format` during diagnosis: it erases the drive.

## Prepare samples with FFmpeg

`tools/convert_samples.py` converts numbered drum WAVs in a folder to **mono,
16-bit PCM, 44,100 Hz**, using FFmpeg with triangular dithering. Originals stay
untouched; output filenames use uppercase BD/SD/HH plus 01–08. Conversion preserves
full lengths and does not normalize volume. Unrecognized WAV names are skipped.
Python 3 and `ffmpeg` on PATH are required; `ffprobe` is not required.

From this project's folder, run:

```sh
python3 tools/convert_samples.py "/path/to/original-samples" "samples-converted"
```

The script verifies every output WAV, reports PCM/file sizes, and checks the largest
sample of each drum together against the shared 128 KiB RAM limit. A full library
containing all three drum types is needed to check every possible combination.
It also estimates library disk usage with 512-byte clusters and a conservative
32 KiB reserve for filesystem/settings/host files; actual free drive space remains
the final check. Exit status is 0 when checks pass, 1 for errors, or 2 when conversion
succeeds but sizes exceed a limit. Outputs remain available for inspection on status 2.

If RAM checks fail, shorten longer samples in an audio editor or explicitly cap
all sounds at 490 ms (this cuts off any longer tails):

```sh
python3 tools/convert_samples.py "/path/to/original-samples" "samples-converted" --overwrite --max-duration 0.49
```

Three 490 ms sounds fit in RAM. A library of many sounds can still exceed the
1 MiB disk; reduce its file count or lengths if the disk check fails. Existing
output WAVs are protected unless `--overwrite` is supplied for matching filenames.
Input and output folders must differ.

Reboot the Pico holding Start/Stop, copy the converted WAVs to the drive root,
replace the matching old files, then safely eject. Keep `CONFIG.TXT` and `SELECT.TXT`.
Verify selections using the serial monitor. Run conversion integration checks with:

```sh
python3 tests/test_convert_samples.py
```

## OP-Z config profile and comments

`config-profiles/op-z/CONFIG.TXT` is a starting profile for OP-Z kick, snare, and
percussion tracks on channels 1, 2, and 3. It uses note 53 as a first-key candidate;
the official OP-Z MIDI guide does not specify numeric drum-key mapping, so verify
that note against your OP-Z kit before relying on it. Change the `N` values to
choose different kit sounds. Copy the profile to the Pico drive root as CONFIG.TXT
and eject to apply it. Enable incoming MIDI on OP-Z and disable
`channel_one_to_active` to keep channel 1 routed to kick rather than the active track.
See https://teenage.engineering/guides/op-z/midi and
https://teenage.engineering/guides/op-z/tracks .

Lines beginning with `#` are ignored by the current parser. Put comments on their
own lines; adding a comment after a setting makes that line invalid. Commenting
out a drum setting restores its firmware default (CH1 and N36/N38/N42), rather than
muting that drum's MIDI. No firmware update is needed for full-line comments.

Only slots 01–08 per drum are supported. Files numbered 09–16 are ignored;
saved selections outside 01–08 fall back to the lowest available supported slot.
