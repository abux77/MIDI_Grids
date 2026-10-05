#include "audio.h"
#include "storage.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "i2s.pio.h"
#include <string.h>

#define FRAMES 256
static uint32_t buffers[2][FRAMES];
static dma_channel_config configs[2];
static int channels[2];
static unsigned sm, program_offset;
static volatile bool active;
static unsigned position[3], gain[3];

static void fill(unsigned buffer) {
    for (unsigned frame = 0; frame < FRAMES; ++frame) {
        int sum = 0;
        for (unsigned part = 0; part < 3; ++part) {
            if (position[part] < sample_frames[part])
                sum += sample_data[part][position[part]++] * (int)gain[part] / 127;
        }
        sum = sum * 160 / 256;
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        uint32_t sample = (uint16_t)sum;
        buffers[buffer][frame] = (sample << 16) | sample;
    }
}

/* The other DMA channel keeps I2S supplied while we mix this completed block.
 * IRQ ownership prevents USB/control work from delaying the refill. */
static void dma_complete(void) {
    uint32_t mask = (1u << channels[0]) | (1u << channels[1]);
    uint32_t completed = dma_hw->ints0 & mask;
    dma_hw->ints0 = completed;
    if (!active) return;
    for (unsigned buffer = 0; buffer < 2; ++buffer) {
        if (!(completed & (1u << channels[buffer]))) continue;
        fill(buffer);
        dma_channel_set_read_addr(channels[buffer], buffers[buffer], false);
        dma_channel_set_trans_count(channels[buffer], FRAMES, false);
    }
}

void audio_stop(void) {
    uint32_t state = save_and_disable_interrupts();
    for (unsigned part = 0; part < 3; ++part) position[part] = sample_frames[part];
    restore_interrupts(state);
}

void audio_trigger(unsigned part, unsigned velocity) {
    if (part >= 3) return;
    uint32_t state = save_and_disable_interrupts();
    gain[part] = velocity;
    position[part] = 0;
    restore_interrupts(state);
}

void audio_resume(void) {
    uint32_t state = save_and_disable_interrupts();
    memset(buffers, 0, sizeof(buffers));
    for (unsigned part = 0; part < 3; ++part) position[part] = sample_frames[part];
    pio_sm_clear_fifos(pio0, sm);
    pio_sm_restart(pio0, sm);
    pio_sm_exec(pio0, sm, pio_encode_jmp(program_offset));
    dma_hw->ints0 = (1u << channels[0]) | (1u << channels[1]);
    for (unsigned buffer = 0; buffer < 2; ++buffer) {
        dma_channel_set_config(channels[buffer], &configs[buffer], false);
        dma_channel_set_read_addr(channels[buffer], buffers[buffer], false);
        dma_channel_set_trans_count(channels[buffer], FRAMES, false);
        dma_channel_set_irq0_enabled(channels[buffer], true);
    }
    active = true;
    dma_start_channel_mask(1u << channels[0]);
    pio_sm_set_enabled(pio0, sm, true);
    restore_interrupts(state);
}

void audio_suspend(void) {
    uint32_t state = save_and_disable_interrupts();
    active = false;
    for (unsigned buffer = 0; buffer < 2; ++buffer) {
        dma_channel_set_irq0_enabled(channels[buffer], false);
        /* Disable chaining before aborting either channel. */
        dma_channel_set_config(channels[buffer], &(dma_channel_config){.ctrl = 0}, false);
    }
    pio_sm_set_enabled(pio0, sm, false);
    for (unsigned buffer = 0; buffer < 2; ++buffer) dma_channel_abort(channels[buffer]);
    dma_hw->ints0 = (1u << channels[0]) | (1u << channels[1]);
    restore_interrupts(state);
}

void audio_init(void) {
    sm = pio_claim_unused_sm(pio0, true);
    program_offset = pio_add_program(pio0, &i2s_program);
    for (unsigned pin = 11; pin <= 13; ++pin) pio_gpio_init(pio0, pin);
    pio_sm_set_consecutive_pindirs(pio0, sm, 11, 3, true);
    pio_sm_config config = i2s_program_get_default_config(program_offset);
    sm_config_set_out_pins(&config, 13, 1);
    sm_config_set_sideset_pins(&config, 11);
    sm_config_set_out_shift(&config, false, true, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config, (float)clock_get_hz(clk_sys) / (44100 * 64));
    pio_sm_init(pio0, sm, program_offset, &config);
    channels[0] = dma_claim_unused_channel(true);
    channels[1] = dma_claim_unused_channel(true);
    for (unsigned buffer = 0; buffer < 2; ++buffer) {
        dma_channel_config config = dma_channel_get_default_config(channels[buffer]);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
        channel_config_set_read_increment(&config, true);
        channel_config_set_write_increment(&config, false);
        channel_config_set_dreq(&config, pio_get_dreq(pio0, sm, true));
        channel_config_set_chain_to(&config, channels[1-buffer]);
        configs[buffer] = config;
        dma_channel_configure(channels[buffer], &config, &pio0->txf[sm],
                              buffers[buffer], FRAMES, false);
    }
    irq_set_exclusive_handler(DMA_IRQ_0, dma_complete);
    irq_set_enabled(DMA_IRQ_0, true);
    audio_resume();
}

/* Kept for compatibility with the control loop; refills are interrupt-driven. */
void audio_service(void) {}
bool audio_is_active(void) { return active; }
