/*
 * veratypes.h - Types for the VERA emulation.
 *
 * Written by
 *  Jason Hill
 *
 * The VERA chip emulation is based on the Commander X16 emulator
 * (https://github.com/X16Community/x16-emulator), Copyright (c) 2019 Michael
 * Steil, Copyright (c) 2020 Frank van den Hoef, which is distributed under
 * the 2-clause BSD license (see LICENSE.x16-emulator in this directory).
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

#ifndef VICE_VERATYPES_H
#define VICE_VERATYPES_H

#include "vice.h"

#include "alarm.h"
#include "log.h"
#include "raster.h"
#include "types.h"

/* VERA version reported by the version registers */
#define VERA_VERSION_MAJOR  47
#define VERA_VERSION_MINOR  0
#define VERA_VERSION_PATCH  2

/* size of the VERA video RAM (128KiB) */
#define VERA_RAM_SIZE       0x20000

/* special areas inside the VERA address space */
#define VERA_ADDR_PSG_START     0x1F9C0
#define VERA_ADDR_PSG_END       0x1FA00
#define VERA_ADDR_PALETTE_START 0x1FA00
#define VERA_ADDR_PALETTE_END   0x1FC00
#define VERA_ADDR_SPRDATA_START 0x1FC00
#define VERA_ADDR_SPRDATA_END   0x20000

/* visible area we are drawing */
#define VERA_SCREEN_WIDTH   640
#define VERA_SCREEN_HEIGHT  480

/* VGA scan timing, both VGA and NTSC modes use 525 lines */
#define VERA_SCAN_HEIGHT    525
#define VERA_SCAN_WIDTH     800

/* VERA pixel clock in Hz (as used by the X16 emulator) */
#define VERA_PIXEL_FREQ     25000000

/* resulting line frequency: 25000000 / 800 = 31250Hz */
#define VERA_LINE_FREQ      (VERA_PIXEL_FREQ / VERA_SCAN_WIDTH)

/* size of the PCM audio FIFO */
#define VERA_PCM_FIFO_SIZE  4096

/* number of PSG channels */
#define VERA_PSG_CHANNELS   16

#define VERA_NUM_SPRITES    128
#define VERA_NUM_COLORS     256
#define VERA_NUM_LAYERS     2

/* the composer registers are banked by DCSEL, 4 registers per bank */
#define VERA_COMPOSER_SLOTS (4 * 64)

/* number of video modes known to the raster code, we only use a single one */
#define VERA_NUM_VMODES     1

/* first address of the register block in the C64 I/O space */
#define VERA_REGS_BASE      0xde00u
#define VERA_REGS_LAST      0xde1fu

struct video_chip_cap_s;

/* one channel of the PSG, the registers live in the video RAM at
   $1F9C0-$1F9FF (4 bytes per channel) */
struct vera_psg_channel_s {
    uint16_t freq;
    uint16_t volume;
    uint8_t left;
    uint8_t right;
    uint8_t pw;
    uint8_t waveform;

    uint16_t noiseval;
    uint32_t phase;
};

struct vera_s {
    /* --------------------------------------------------------------------
     * VICE glue
     * -------------------------------------------------------------------- */

    /* Flag: is the VERA emulation initialized (canvas created)? */
    int initialized;

    /* Log descriptor */
    log_t log;

    /* Raster used to draw into the VERA canvas */
    raster_t raster;

    /* Alarm that emulates one VERA scan line */
    alarm_t *raster_draw_alarm;

    /* Interrupt number used for the VERA IRQ line */
    unsigned int int_num;

    /* Current state of the VERA IRQ output */
    int irq_active;

    /* Line timing: 16.16 fixed point CPU cycles per VERA scan line and the
       accumulated fraction */
    unsigned int line_increment;
    unsigned int line_accu;

    /* CPU frequency the line increment was calculated for */
    long cycles_per_sec;

    /* Video chip capabilities as used by the VICE video code */
    struct video_chip_cap_s *video_chip_cap;

    /* Frame counter, used to skip frames in warp mode */
    unsigned int frame_count;

    /* --------------------------------------------------------------------
     * VERA chip state
     * -------------------------------------------------------------------- */

    /* video RAM */
    uint8_t ram[VERA_RAM_SIZE];

    /* palette RAM, 256 entries of 2 bytes (0x0RGB, little endian) */
    uint8_t palette[VERA_NUM_COLORS * 2];

    /* sprite attribute RAM, 128 sprites of 8 bytes */
    uint8_t sprite_data[VERA_NUM_SPRITES][8];

    /* layer registers */
    uint8_t reg_layer[VERA_NUM_LAYERS][7];

    /* display composer registers (banked by DCSEL) */
    uint8_t reg_composer[VERA_COMPOSER_SLOTS];

    /* data port state */
    uint32_t io_addr[2];
    uint8_t io_rddata[2];
    uint8_t io_inc[2];
    uint8_t io_addrsel;
    uint8_t io_dcsel;

    /* interrupt state */
    uint8_t ien;
    uint8_t isr;
    uint16_t irq_line;

    /* current VERA scan line (0..VERA_SCAN_HEIGHT-1) */
    uint16_t scan_pos_y;

    /* sprite collisions collected during the current frame */
    uint8_t sprite_line_collisions;

    /* --------------------------------------------------------------------
     * PCM audio ($DE1B-$DE1D)
     * -------------------------------------------------------------------- */

    uint8_t pcm_fifo[VERA_PCM_FIFO_SIZE];
    uint32_t pcm_fifo_wridx;
    uint32_t pcm_fifo_rdidx;
    uint32_t pcm_fifo_cnt;

    /* PSG ($1F9C0-$1F9FF in the video RAM) */
    struct vera_psg_channel_s psg_channels[VERA_PSG_CHANNELS];
    uint16_t psg_noise_state;

    uint8_t pcm_ctrl;
    uint8_t pcm_rate;
    uint8_t pcm_loop;
    uint8_t pcm_phase;

    int16_t pcm_cur_l;
    int16_t pcm_cur_r;

    /* --------------------------------------------------------------------
     * FX registers (VERA 0.3.x "FX" extensions)
     *
     * The pixel positions/increments are 16.16 fixed point here, even though
     * the VERA uses smaller bit widths for the whole and fractional parts.
     * -------------------------------------------------------------------- */

    uint8_t fx_addr1_mode;

    uint32_t fx_x_pixel_increment;
    uint32_t fx_y_pixel_increment;
    uint32_t fx_x_pixel_position;
    uint32_t fx_y_pixel_position;

    uint16_t fx_poly_fill_length;

    uint32_t fx_affine_tile_base;
    uint32_t fx_affine_map_base;
    uint8_t fx_affine_map_size;

    uint8_t fx_4bit_mode;
    uint8_t fx_16bit_hop;
    uint8_t fx_cache_byte_cycling;
    uint8_t fx_cache_fill;
    uint8_t fx_cache_write;
    uint8_t fx_trans_writes;

    uint8_t fx_2bit_poly;
    uint8_t fx_2bit_poking;

    uint8_t fx_cache_increment_mode;
    uint8_t fx_cache_nibble_index;
    uint8_t fx_cache_byte_index;
    uint8_t fx_multiplier;
    uint8_t fx_subtract;

    uint8_t fx_affine_clip;

    uint8_t fx_16bit_hop_align;

    uint8_t fx_nibble_bit[2];
    uint8_t fx_nibble_incr[2];

    uint8_t fx_cache[4];

    int32_t fx_mult_accumulator;
};
typedef struct vera_s vera_t;

extern vera_t vera;

#endif
