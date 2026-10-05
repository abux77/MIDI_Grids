# Hardware validation — still required

Build and host tests do not establish electrical operation or measured clock jitter.
This firmware has not been flashed onto or tested on a connected Pico in this session.

1. Flash the UF2 to an original Pico. Check CDC startup; expect embedded factory audio on incompatible old MicroPython storage.
   Confirm the disk is preserved; use explicit `format` only to initialize editable storage.
2. Verify all seven pots, per-drum CONFIG.TXT MIDI settings and four LEDs. Power up stopped;
   short button release starts/stops. Check clockwise increases all pot values. Check notes 36/38/42 and accents 100/127.
   Match the drum machine receive channels to CONFIG.TXT (`status`).
   Use `test` over CDC to isolate MIDI routing from pattern controls.
3. Scope GP11/12/13: LRCLK approximately 44.1 kHz, BCK approximately 1.4112 MHz,
   16-bit stereo I²S. Listen to all three factory sounds and overlapping hits.
4. Capture DIN and USB output: 24 clocks per quarter note, one Start before running,
   Stop on stopping, no clock while stopped, and no stuck drum notes.
5. Connect the optocoupler's 3.3 V UART output to GP1. Test external clocks at
   40/120/240 BPM, Start resetting, Continue resuming, Stop silencing, and clock-only
   input not starting a stopped sequencer. Repeat from a computer over USB MIDI.
6. At a fixed incoming tempo, test low/middle/high tempo knob ranges. Drum rate should
   be quarter/half/full; forwarded clock rate must remain unchanged.
7. Send clocks to both inputs at different rates: the first active source must win.
   Unplug it; after two seconds running playback falls back to the internal knob
   tempo (or another active source takes over). Repeat loss while stopped.
8. Reboot holding Start/Stop. Confirm no short-press toggle on release,
   DAC DMA stopped, and USB sample volume available. Replace one valid
   WAV, safely eject, and verify stopped reload then the new sound on Start.
9. Set `USB_MIDI,OFF` in CONFIG.TXT and reboot; repeat editing. MIDI USB interface should be absent while CDC and
   sample-drive access remain. Reboot after changing the switch.
10. Try a stereo/incorrect-rate WAV, a missing file, an oversized payload, and a
    malformed/truncated WAV. Check that the voice mutes with a CDC diagnostic while
    other samples/MIDI still work. Restore valid files and eject.
11. Reflash firmware and confirm edited samples survive. Confirm ordinary startup
    never autoformats a nonblank damaged filesystem; use explicit `format` to recover.
12. Stress maximum density/audio overlaps plus DIN+USB activity. Check CDC `status`
    for dropped MIDI; measure timing and look for DAC underruns. Check volume/eject
    behavior on the actual host OS. No endurance or jitter figures are claimed yet.

13. Add sparse numbered samples; while stopped hold Start/Stop and turn each density
    knob. Check audio previews, no MIDI preview notes, release without starting,
    density pickup, and selections surviving reboot. Confirm selection is disabled
    while playing and ordinary long holds never expose the USB drive.
14. Edit CONFIG.TXT with different drum channels/notes, eject and verify DIN/USB
    routing. Check invalid entries use defaults and boot-held storage works with USB MIDI disabled.
    Check USB MIDI changes require reboot, ON/OFF enumerate correctly, CDC remains
    available, GP14 has no effect, and files numbered 09–16 are ignored.
