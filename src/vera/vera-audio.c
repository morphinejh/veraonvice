/*
 * vera-audio.c - VERA audio: the PCM channel ($DE1B-$DE1D) and the PSG
 *                (16 channels, registers at $1F9C0-$1F9FF in the video RAM).
 *
 * The PCM and PSG engines are derived from the Commander X16 emulator
 * (https://github.com/X16Community/x16-emulator), src/vera_pcm.c and
 * src/vera_psg.c:
 *
 *   Commander X16 Emulator
 *   Copyright (c) 2020 Frank van den Hoef
 *   All rights reserved. License: 2-clause BSD
 *
 * The full text of that license is in LICENSE.x16-emulator in this directory.
 *
 * Adapted for VICE by Jason Hill:
 *  - the PCM and PSG engines are clocked from the CPU clock instead of from the audio
 *    callback, so the FIFO level (and with it the FIFO flags and the AFLOW
 *    interrupt) behaves the same no matter what the host audio does, even
 *    when sound output is turned off completely,
 *  - the PSG and PCM outputs are summed (like the VERA does) and go into a
 *    ring buffer which is resampled to the VICE output sample rate by the
 *    VICE sound chip interface below.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdio.h>
#include <string.h>

#include "machine.h"
#include "maincpu.h"
#include "sound.h"
#include "types.h"
#include "vera-audio.h"
#include "vera-core.h"
#include "veratypes.h"

/* The VERA generates its audio samples with 25MHz / 512 = 48828.125Hz. */
#define VERA_AUDIO_CLOCK_NUM  25000000ULL
#define VERA_AUDIO_CLOCK_DIV  512

/* Size of the ring buffer holding the generated samples, in stereo samples.
   32768 samples are ~0.67 seconds, way more than the emulator will ever be
   ahead of the sound output. */
#define VERA_AUDIO_RING_SIZE  32768

/* ------------------------------------------------------------------------- */
/* PSG                                                                       */

/* waveforms */
#define PSG_WF_PULSE     0
#define PSG_WF_SAWTOOTH  1
#define PSG_WF_TRIANGLE  2
#define PSG_WF_NOISE     3

/* volume look up table of the PSG */
static const uint16_t psg_volume_lut[64] = {
      0,                                           4,   8,  12,
     16,  17,  18,  20,  21,  22,  23,  25,  26,  28,  30,  31,
     33,  35,  37,  40,  42,  45,  47,  50,  53,  56,  60,  63,
     67,  71,  75,  80,  85,  90,  95, 101, 107, 113, 120, 127,
    135, 143, 151, 160, 170, 180, 191, 202, 214, 227, 241, 255,
    270, 286, 303, 321, 341, 361, 382, 405, 429, 455, 482, 511
};

void vera_audio_psg_write(uint8_t reg, uint8_t value)
{
    struct vera_psg_channel_s *ch;
    int idx;

    /* render everything up to this point before the change takes effect */
    vera_audio_run();

    reg &= 0x3f;
    ch = &vera.psg_channels[reg / 4];
    idx = reg & 3;

    switch (idx) {
        case 0:
            ch->freq = (ch->freq & 0xff00) | value;
            break;
        case 1:
            ch->freq = (ch->freq & 0x00ff) | (value << 8);
            break;
        case 2:
            ch->right = (value & 0x80) != 0;
            ch->left = (value & 0x40) != 0;
            ch->volume = psg_volume_lut[value & 0x3f];
            break;
        case 3:
            ch->pw = value & 0x3f;
            ch->waveform = value >> 6;
            break;
    }
}

static void psg_reset(void)
{
    memset(vera.psg_channels, 0, sizeof(vera.psg_channels));
    vera.psg_noise_state = 1;
}

/* Generate one PSG sample. */
static void psg_render_sample(int16_t *left, int16_t *right)
{
    int16_t l = 0;
    int16_t r = 0;
    int i;

    for (i = 0; i < VERA_PSG_CHANNELS; i++) {
        struct vera_psg_channel_s *ch = &vera.psg_channels[i];
        uint32_t new_phase;
        uint32_t v = 0;
        int16_t sv;
        int16_t val;

        /* In the FPGA implementation the noise values are generated every
           system clock and the channels are updated one after the other, so
           two channels fetching a noise value in the same sample get
           different values. */
        vera.psg_noise_state = (uint16_t)((vera.psg_noise_state << 1)
                                          | (((vera.psg_noise_state >> 1)
                                              ^ (vera.psg_noise_state >> 2)
                                              ^ (vera.psg_noise_state >> 4)
                                              ^ (vera.psg_noise_state >> 15)) & 1));

        new_phase = (ch->left || ch->right) ? ((ch->phase + ch->freq) & 0x1ffff) : 0;
        if ((ch->phase & 0x10000) && !(new_phase & 0x10000)) {
            ch->noiseval = (vera.psg_noise_state >> 1) & 0x3f;
        }
        ch->phase = new_phase;

        switch (ch->waveform) {
            case PSG_WF_PULSE:
                v = ((ch->phase >> 10) > ch->pw) ? 0 : 0x3f;
                break;
            case PSG_WF_SAWTOOTH:
                v = (ch->phase >> 11) ^ ((ch->pw ^ 0x3f) & 0x3f);
                break;
            case PSG_WF_TRIANGLE:
                v = ((ch->phase & 0x10000) ? (~(ch->phase >> 10) & 0x3f)
                                           : ((ch->phase >> 10) & 0x3f))
                    ^ ((ch->pw ^ 0x3f) & 0x3f);
                break;
            case PSG_WF_NOISE:
                v = ch->noiseval;
                break;
        }

        sv = (int16_t)(v ^ 0x20);
        if (sv & 0x20) {
            sv |= 0xffc0;
        }

        val = (int16_t)(sv * ch->volume);

        if (ch->left) {
            l = (int16_t)(l + (val >> 3));
        }
        if (ch->right) {
            r = (int16_t)(r + (val >> 3));
        }
    }

    *left = l;
    *right = r;
}

/* ------------------------------------------------------------------------- */
/* PCM engine                                                                */

/* volume look up table of the VERA */
static const uint8_t volume_lut[16] = {
    0, 1, 2, 3, 4, 5, 6, 8, 11, 14, 18, 23, 30, 38, 49, 64
};

/* CPU clock the PCM engine has been run up to */
static CLOCK audio_last_clk;

/* 32.32 fixed point: VERA audio samples per CPU cycle and the accumulated
   fraction */
static uint64_t audio_step;
static uint64_t audio_accu;

/* CPU frequency audio_step was calculated for */
static long audio_cycles_per_sec;

/* AFLOW state (ISR bit 3) as it was after the last update */
static int audio_aflow;

/* generated samples, waiting to be picked up by the sound code */
static int16_t ring_buffer[VERA_AUDIO_RING_SIZE * 2];
static unsigned int ring_write;
static unsigned int ring_read;
static unsigned int ring_count;

/* output sample rate of the VICE sound system */
static int audio_speed;

/* 16.16 fixed point: VERA samples per output sample */
static uint32_t resample_step;
static uint32_t resample_accu;

/* last sample handed to the sound system, used when the ring runs dry */
static int16_t resample_last_l;
static int16_t resample_last_r;

static void vera_audio_update_timing(void)
{
    long cycles_per_sec = machine_get_cycles_per_second();

    if (cycles_per_sec == audio_cycles_per_sec) {
        return;
    }

    audio_cycles_per_sec = cycles_per_sec;
    if (cycles_per_sec <= 0) {
        audio_step = 0;
        return;
    }

    audio_step = ((VERA_AUDIO_CLOCK_NUM << 32) / VERA_AUDIO_CLOCK_DIV)
                 / (uint64_t)cycles_per_sec;
}

static void ring_reset(void)
{
    ring_write = 0;
    ring_read = 0;
    ring_count = 0;
    resample_accu = 0;
    resample_last_l = 0;
    resample_last_r = 0;
}

static void ring_push(int16_t left, int16_t right)
{
    if (ring_count == VERA_AUDIO_RING_SIZE) {
        /* The sound system is not keeping up (sound disabled, warp mode,
           emulator paused, ...), drop the oldest sample. */
        ring_read = (ring_read + 1) % VERA_AUDIO_RING_SIZE;
        ring_count--;
    }

    ring_buffer[ring_write * 2] = left;
    ring_buffer[ring_write * 2 + 1] = right;
    ring_write = (ring_write + 1) % VERA_AUDIO_RING_SIZE;
    ring_count++;
}

static void fifo_reset(void)
{
    vera.pcm_fifo_wridx = 0;
    vera.pcm_fifo_rdidx = 0;
    vera.pcm_fifo_cnt = 0;
}

static void fifo_restart(void)
{
    vera.pcm_fifo_rdidx = 0;
    vera.pcm_fifo_cnt = vera.pcm_fifo_wridx;
}

static uint8_t read_fifo(void)
{
    uint8_t result;

    if (vera.pcm_fifo_cnt == 0) {
        return 0;
    }

    result = vera.pcm_fifo[vera.pcm_fifo_rdidx++];
    if (vera.pcm_fifo_rdidx == VERA_PCM_FIFO_SIZE) {
        vera.pcm_fifo_rdidx = 0;
    }
    vera.pcm_fifo_cnt--;

    return result;
}

int vera_audio_fifo_almost_empty(void)
{
    return vera.pcm_fifo_cnt < 1024;
}

static int16_t clamp16(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

/* Generate one VERA audio sample (PSG + PCM, like the VERA sums them) and
   push it into the ring buffer. */
static void render_sample(void)
{
    uint8_t old_phase = vera.pcm_phase;
    int16_t left;
    int16_t right;
    int16_t psg_l;
    int16_t psg_r;

    vera.pcm_phase += vera.pcm_rate;

    if ((old_phase & 0x80) != (vera.pcm_phase & 0x80)) {
        if (vera.pcm_fifo_cnt == 0) {
            vera.pcm_cur_l = 0;
            vera.pcm_cur_r = 0;
        } else {
            switch ((vera.pcm_ctrl >> 4) & 3) {
                case 0: /* mono, 8 bit */
                    vera.pcm_cur_l = (int16_t)((int8_t)read_fifo() << 8);
                    vera.pcm_cur_r = vera.pcm_cur_l;
                    break;
                case 1: /* stereo, 8 bit */
                    if (vera.pcm_fifo_cnt < 2) {
                        vera.pcm_fifo_cnt = 0;
                        vera.pcm_fifo_rdidx = vera.pcm_fifo_wridx;
                    } else {
                        vera.pcm_cur_l = (int16_t)((int8_t)read_fifo() << 8);
                        vera.pcm_cur_r = (int16_t)((int8_t)read_fifo() << 8);
                    }
                    break;
                case 2: /* mono, 16 bit */
                    if (vera.pcm_fifo_cnt < 2) {
                        vera.pcm_fifo_cnt = 0;
                        vera.pcm_fifo_rdidx = vera.pcm_fifo_wridx;
                    } else {
                        uint16_t sample = read_fifo();
                        sample |= (uint16_t)read_fifo() << 8;
                        vera.pcm_cur_l = (int16_t)sample;
                        vera.pcm_cur_r = vera.pcm_cur_l;
                    }
                    break;
                case 3: /* stereo, 16 bit */
                    if (vera.pcm_fifo_cnt < 4) {
                        vera.pcm_fifo_cnt = 0;
                        vera.pcm_fifo_rdidx = vera.pcm_fifo_wridx;
                    } else {
                        uint16_t sample = read_fifo();
                        sample |= (uint16_t)read_fifo() << 8;
                        vera.pcm_cur_l = (int16_t)sample;
                        sample = read_fifo();
                        sample |= (uint16_t)read_fifo() << 8;
                        vera.pcm_cur_r = (int16_t)sample;
                    }
                    break;
            }

            if (vera.pcm_loop && vera.pcm_fifo_cnt == 0) {
                fifo_restart();
            }
        }
    }

    left = (int16_t)((int32_t)vera.pcm_cur_l * volume_lut[vera.pcm_ctrl & 0xf] / 64);
    right = (int16_t)((int32_t)vera.pcm_cur_r * volume_lut[vera.pcm_ctrl & 0xf] / 64);

    psg_render_sample(&psg_l, &psg_r);

    ring_push(clamp16((int32_t)left + psg_l), clamp16((int32_t)right + psg_r));
}

void vera_audio_run(void)
{
    CLOCK elapsed;
    uint64_t samples;
    int aflow;

    vera_audio_update_timing();

    if (maincpu_clk < audio_last_clk) {
        /* should not happen, but don't run wild if it does */
        audio_last_clk = maincpu_clk;
        return;
    }

    elapsed = maincpu_clk - audio_last_clk;
    audio_last_clk = maincpu_clk;

    if (audio_step == 0) {
        return;
    }

    /* Don't try to catch up more than a second worth of samples, that only
       happens when the engine has not been running at all. */
    if (elapsed > (CLOCK)audio_cycles_per_sec) {
        elapsed = (CLOCK)audio_cycles_per_sec;
    }

    audio_accu += (uint64_t)elapsed * audio_step;
    samples = audio_accu >> 32;
    audio_accu &= 0xffffffffULL;

    while (samples--) {
        render_sample();
    }

    /* the AFLOW flag feeds into the VERA interrupt output */
    aflow = vera_audio_fifo_almost_empty();
    if (aflow != audio_aflow) {
        audio_aflow = aflow;
        vera_core_irq_update();
    }
}

/* ------------------------------------------------------------------------- */
/* register access                                                           */

void vera_audio_ctrl_write(uint8_t value)
{
    vera_audio_run();

    if ((value & 0xc0) == 0xc0) {
        vera.pcm_loop = 1;
    } else {
        vera.pcm_loop = 0;
        if (value & 0x80) {
            fifo_reset();
        }
    }
    if (value & 0x40) {
        fifo_restart();
    }
    vera.pcm_ctrl = value & 0x3f;

    vera_core_irq_update();
}

uint8_t vera_audio_ctrl_read(void)
{
    uint8_t result;

    vera_audio_run();

    result = vera.pcm_ctrl;
    if (vera.pcm_fifo_cnt == VERA_PCM_FIFO_SIZE - 1) {
        result |= 0x80; /* FIFO full */
    }
    if (vera.pcm_fifo_cnt == 0) {
        result |= 0x40; /* FIFO empty */
    }

    return result;
}

void vera_audio_rate_write(uint8_t value)
{
    vera_audio_run();

    vera.pcm_rate = (value > 128) ? (uint8_t)(256 - value) : value;
}

uint8_t vera_audio_rate_read(void)
{
    return vera.pcm_rate;
}

void vera_audio_fifo_write(uint8_t value)
{
    vera_audio_run();

    if (vera.pcm_fifo_cnt < VERA_PCM_FIFO_SIZE - 1) {
        vera.pcm_fifo[vera.pcm_fifo_wridx++] = value;
        if (vera.pcm_fifo_wridx == VERA_PCM_FIFO_SIZE) {
            vera.pcm_fifo_wridx = 0;
        }
        vera.pcm_fifo_cnt++;
    }

    vera_core_irq_update();
}

void vera_audio_reset(void)
{
    psg_reset();

    fifo_reset();
    vera.pcm_ctrl = 0;
    vera.pcm_rate = 0;
    vera.pcm_loop = 0;
    vera.pcm_cur_l = 0;
    vera.pcm_cur_r = 0;
    vera.pcm_phase = 0;

    audio_last_clk = maincpu_clk;
    audio_accu = 0;
    audio_aflow = vera_audio_fifo_almost_empty();

    ring_reset();
}

/* ------------------------------------------------------------------------- */
/* VICE sound chip                                                           */

static int vera_sound_machine_init(sound_t *psid, int speed, int cycles_per_sec);
static void vera_sound_machine_store(sound_t *psid, uint16_t addr, uint8_t val);
static uint8_t vera_sound_machine_read(sound_t *psid, uint16_t addr);
static void vera_sound_machine_reset(sound_t *psid, CLOCK cpu_clk);
static int vera_sound_machine_calculate_samples(sound_t **psid, int16_t *pbuf, int nr,
                                                int soc, int scc, CLOCK *delta_t);

static int vera_sound_machine_cycle_based(void)
{
    return 0;
}

static int vera_sound_machine_channels(void)
{
    return 2;
}

static sound_chip_t vera_sound_chip = {
    NULL,                                 /* NO sound chip open function */
    vera_sound_machine_init,              /* sound chip init function */
    NULL,                                 /* NO sound chip close function */
    vera_sound_machine_calculate_samples, /* sound chip calculate samples function */
    vera_sound_machine_store,             /* sound chip store function */
    vera_sound_machine_read,              /* sound chip read function */
    vera_sound_machine_reset,             /* sound chip reset function */
    vera_sound_machine_cycle_based,       /* the chip is NOT cycle based */
    vera_sound_machine_channels,          /* the chip has 2 channels */
    0                                     /* sound chip enabled flag */
};

static uint16_t vera_sound_chip_offset = 0;

void vera_sound_chip_init(void)
{
    vera_sound_chip_offset = sound_chip_register(&vera_sound_chip);
}

void vera_audio_set_enabled(int enabled)
{
    vera_sound_chip.chip_enabled = enabled ? 1 : 0;
}

static int vera_sound_machine_init(sound_t *psid, int speed, int cycles_per_sec)
{
    audio_speed = speed;

    if (speed > 0) {
        /* how many VERA samples make up one output sample (16.16) */
        resample_step = (uint32_t)(((VERA_AUDIO_CLOCK_NUM << 16) / VERA_AUDIO_CLOCK_DIV)
                                   / (uint64_t)speed);
    } else {
        resample_step = 1 << 16;
    }

    ring_reset();

    return 1;
}

static void vera_sound_machine_store(sound_t *psid, uint16_t addr, uint8_t val)
{
    /* the VERA registers are handled by the chip emulation itself */
}

static uint8_t vera_sound_machine_read(sound_t *psid, uint16_t addr)
{
    return 0;
}

static void vera_sound_machine_reset(sound_t *psid, CLOCK cpu_clk)
{
    ring_reset();
}

/* Hand the generated samples to the VICE sound system, converting from the
   VERA sample rate to the output sample rate on the way. The samples that
   make up one output sample are averaged, which gives a cheap low pass
   filter for the (usually much lower) PCM sample rates. */
static int vera_sound_machine_calculate_samples(sound_t **psid, int16_t *pbuf, int nr,
                                                int soc, int scc, CLOCK *delta_t)
{
    uint32_t step = resample_step;
    int i;

    if (step == 0) {
        return nr;
    }

    /* Keep the latency in check: when the ring buffer runs full the output is
       consumed a little faster, when it runs dry a little slower. Both sides
       are driven by emulated time, so this only compensates rounding. */
    if (ring_count > (VERA_AUDIO_RING_SIZE / 2)) {
        step += step / 64;
    } else if (ring_count < (VERA_AUDIO_RING_SIZE / 32)) {
        step -= step / 64;
    }

    for (i = 0; i < nr; i++) {
        int32_t sum_l = 0;
        int32_t sum_r = 0;
        unsigned int count = 0;
        unsigned int n;

        resample_accu += step;
        n = resample_accu >> 16;
        resample_accu &= 0xffff;

        while (n-- > 0) {
            if (ring_count == 0) {
                break;
            }
            sum_l += ring_buffer[ring_read * 2];
            sum_r += ring_buffer[ring_read * 2 + 1];
            ring_read = (ring_read + 1) % VERA_AUDIO_RING_SIZE;
            ring_count--;
            count++;
        }

        if (count > 0) {
            resample_last_l = (int16_t)(sum_l / (int32_t)count);
            resample_last_r = (int16_t)(sum_r / (int32_t)count);
        }

        if (soc == SOUND_OUTPUT_STEREO) {
            pbuf[i * soc] = sound_audio_mix(pbuf[i * soc], resample_last_l);
            pbuf[i * soc + 1] = sound_audio_mix(pbuf[i * soc + 1], resample_last_r);
        } else {
            pbuf[i * soc] = sound_audio_mix(pbuf[i * soc],
                                            (int16_t)((resample_last_l + resample_last_r) / 2));
        }
    }

    return nr;
}
