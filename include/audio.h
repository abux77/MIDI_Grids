#include <stdbool.h>
#pragma once
void audio_init(void);
void audio_service(void);
void audio_trigger(unsigned part,unsigned velocity);
void audio_stop(void);
void audio_suspend(void);
void audio_resume(void);

bool audio_is_active(void);
