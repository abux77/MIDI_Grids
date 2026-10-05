#pragma once
#include <stdbool.h>
void storage_init(void);
bool storage_begin_edit(void);
bool storage_edit_active(void);
bool storage_reload_pending(void);
void storage_service_remount(void);
bool samples_load(void);
extern const short *sample_data[3];
extern unsigned sample_frames[3];

bool storage_format_factory(void);

bool storage_is_ready(void);

/* Channels are zero based internally; sample numbers are 1..8. */
#define SAMPLE_SLOTS 8
extern bool configured_usb_midi;
extern unsigned drum_channel[3], drum_note[3], selected_sample[3];
void settings_load(void);
unsigned sample_choice(unsigned part, unsigned knob);
bool sample_select(unsigned part, unsigned number);
bool selections_save(void); /* Caller must suspend audio during flash writes. */

void samples_report(void); /* Read-only serial diagnostics; no PCM reload. */
