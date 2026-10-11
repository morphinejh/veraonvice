/*
 * vera-core.c - VERA chip logic (registers, video RAM and line rendering).
 *
 * The VERA emulation is derived from the Commander X16 emulator
 * (https://github.com/X16Community/x16-emulator), src/video.c:
 *
 *   Commander X16 Emulator
 *   Copyright (c) 2019 Michael Steil
 *   Copyright (c) 2020 Frank van den Hoef
 *   All rights reserved. License: 2-clause BSD
 *
 * The full text of that license is in LICENSE.x16-emulator in this directory.
 *
 * Adapted for VICE by Jason Hill:
 *  - the SDL specific output code has been replaced by the VICE raster/canvas
 *    code (see vera.c),
 *  - the scan out is driven by a VICE alarm instead of the X16 main loop,
 *  - each scan line is rendered in one go, mid line raster effects are not
 *    emulated (the two line register history of the original is kept),
 *  - the audio (PSG/PCM) and SPI parts of the VERA are not emulated.
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interrupt.h"
#include "lib.h"
#include "maincpu.h"
#include "types.h"
#include "vera-audio.h"
#include "vera-core.h"
#include "veratypes.h"
#include "vsync.h"

#define VERA_MAX(a, b) ((a) > (b) ? (a) : (b))

/* result of the most recently rendered scan line */
uint8_t vera_core_line_buffer[VERA_SCREEN_WIDTH];

/* ------------------------------------------------------------------------- */
/* derived state, recalculated from the registers                            */

struct video_layer_properties {
    uint8_t color_depth;
    uint32_t map_base;
    uint32_t tile_base;

    uint8_t bitmap_mode; /* 0: tile mode / text mode, 1: bitmap mode */
    uint8_t text_mode;
    uint8_t text_mode_256c;
    uint8_t tile_mode;

    uint16_t hscroll;
    uint16_t vscroll;

    uint8_t mapw_log2;
    uint8_t maph_log2;
    uint16_t tilew;
    uint16_t tileh;
    uint8_t tilew_log2;
    uint8_t tileh_log2;

    uint16_t mapw_max;
    uint16_t maph_max;
    uint16_t tilew_max;
    uint16_t tileh_max;
    uint16_t layerw_max;
    uint16_t layerh_max;

    int min_eff_x;
    int max_eff_x;

    uint8_t bits_per_pixel;
    uint8_t first_color_pos;
    uint8_t color_mask;
    uint8_t color_fields_max;
    uint8_t tile_size_log2;
};

struct video_sprite_properties {
    int8_t sprite_zdepth;
    uint8_t sprite_collision_mask;

    int16_t sprite_x;
    int16_t sprite_y;
    uint8_t sprite_width_log2;
    uint8_t sprite_height_log2;
    uint16_t sprite_width;
    uint16_t sprite_height;

    uint8_t hflip;
    uint8_t vflip;

    uint8_t color_mode;
    uint32_t sprite_address;

    uint16_t palette_offset;
};

static struct video_layer_properties layer_properties[VERA_NUM_LAYERS];
static struct video_layer_properties prev_layer_properties[2][VERA_NUM_LAYERS];
static struct video_sprite_properties sprite_properties[VERA_NUM_SPRITES];

static uint8_t prev_reg_composer[2][VERA_COMPOSER_SLOTS];

static uint8_t layer_line[VERA_NUM_LAYERS][VERA_SCREEN_WIDTH];
static uint8_t sprite_line_col[VERA_SCREEN_WIDTH];
static uint8_t sprite_line_z[VERA_SCREEN_WIDTH];
static uint8_t sprite_line_mask[VERA_SCREEN_WIDTH];
static int layer_line_enable[VERA_NUM_LAYERS];
static int old_layer_line_enable[VERA_NUM_LAYERS];
static int sprite_line_enable;
static int old_sprite_line_enable;

/* line rendering history */
static uint16_t y_prev;
static uint32_t eff_y_fp; /* 16.16 fixed point */

/* set when the palette RAM or the output mode changed */
static int palette_dirty;

static const uint8_t vera_version_string[4] = {
    'V', VERA_VERSION_MAJOR, VERA_VERSION_MINOR, VERA_VERSION_PATCH
};

static const uint16_t default_palette[VERA_NUM_COLORS] = {
    0x000, 0xfff, 0x800, 0xafe, 0xc4c, 0x0c5, 0x00a, 0xee7, 0xd85, 0x640, 0xf77, 0x333, 0x777, 0xaf6, 0x08f, 0xbbb,
    0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
    0x211, 0x433, 0x644, 0x866, 0xa88, 0xc99, 0xfbb, 0x211, 0x422, 0x633, 0x844, 0xa55, 0xc66, 0xf77, 0x200, 0x411,
    0x611, 0x822, 0xa22, 0xc33, 0xf33, 0x200, 0x400, 0x600, 0x800, 0xa00, 0xc00, 0xf00, 0x221, 0x443, 0x664, 0x886,
    0xaa8, 0xcc9, 0xfeb, 0x211, 0x432, 0x653, 0x874, 0xa95, 0xcb6, 0xfd7, 0x210, 0x431, 0x651, 0x862, 0xa82, 0xca3,
    0xfc3, 0x210, 0x430, 0x640, 0x860, 0xa80, 0xc90, 0xfb0, 0x121, 0x343, 0x564, 0x786, 0x9a8, 0xbc9, 0xdfb, 0x121,
    0x342, 0x463, 0x684, 0x8a5, 0x9c6, 0xbf7, 0x120, 0x241, 0x461, 0x582, 0x6a2, 0x8c3, 0x9f3, 0x120, 0x240, 0x360,
    0x480, 0x5a0, 0x6c0, 0x7f0, 0x121, 0x343, 0x465, 0x686, 0x8a8, 0x9ca, 0xbfc, 0x121, 0x242, 0x364, 0x485, 0x5a6,
    0x6c8, 0x7f9, 0x020, 0x141, 0x162, 0x283, 0x2a4, 0x3c5, 0x3f6, 0x020, 0x041, 0x061, 0x082, 0x0a2, 0x0c3, 0x0f3,
    0x122, 0x344, 0x466, 0x688, 0x8aa, 0x9cc, 0xbff, 0x122, 0x244, 0x366, 0x488, 0x5aa, 0x6cc, 0x7ff, 0x022, 0x144,
    0x166, 0x288, 0x2aa, 0x3cc, 0x3ff, 0x022, 0x044, 0x066, 0x088, 0x0aa, 0x0cc, 0x0ff, 0x112, 0x334, 0x456, 0x668,
    0x88a, 0x9ac, 0xbcf, 0x112, 0x224, 0x346, 0x458, 0x56a, 0x68c, 0x79f, 0x002, 0x114, 0x126, 0x238, 0x24a, 0x35c,
    0x36f, 0x002, 0x014, 0x016, 0x028, 0x02a, 0x03c, 0x03f, 0x112, 0x334, 0x546, 0x768, 0x98a, 0xb9c, 0xdbf, 0x112,
    0x324, 0x436, 0x648, 0x85a, 0x96c, 0xb7f, 0x102, 0x214, 0x416, 0x528, 0x62a, 0x83c, 0x93f, 0x102, 0x204, 0x306,
    0x408, 0x50a, 0x60c, 0x70f, 0x212, 0x434, 0x646, 0x868, 0xa8a, 0xc9c, 0xfbe, 0x211, 0x423, 0x635, 0x847, 0xa59,
    0xc6b, 0xf7d, 0x201, 0x413, 0x615, 0x826, 0xa28, 0xc3a, 0xf3c, 0x201, 0x403, 0x604, 0x806, 0xa08, 0xc09, 0xf0b
};

/* address increment values, indexed by the increment field of ADDRx_H */
static const int16_t increments[32] = {
    0, 0,
    1, -1,
    2, -2,
    4, -4,
    8, -8,
    16, -16,
    32, -32,
    64, -64,
    128, -128,
    256, -256,
    512, -512,
    40, -40,
    80, -80,
    160, -160,
    320, -320,
    640, -640,
};

/* ------------------------------------------------------------------------- */
/* internal prototypes                                                       */

static void refresh_layer_properties(uint8_t layer);
static void refresh_sprite_properties(uint16_t sprite);
static void video_space_read_range(uint8_t *dest, uint32_t address, uint32_t size);
static uint32_t get_and_inc_address(uint8_t sel, int write);
static void fx_affine_prefetch(void);
static void fx_video_space_write(uint32_t address, int nibble, uint8_t value);
static void fx_vram_cache_write(uint32_t address, uint8_t value, uint8_t mask);
static uint8_t video_get_dc_value(uint8_t reg);

/* ------------------------------------------------------------------------- */
/* palette                                                                   */

int vera_core_palette_is_dirty(void)
{
    return palette_dirty;
}

void vera_core_palette_clear_dirty(void)
{
    palette_dirty = 0;
}

void vera_core_palette_set_dirty(void)
{
    palette_dirty = 1;
}

/* ------------------------------------------------------------------------- */
/* interrupts                                                                */

void vera_core_irq_update(void)
{
    int irq;

    if (!vera.initialized) {
        /* no VERA in this session, don't touch the CPU interrupt state */
        return;
    }

    /* bit 3 (AFLOW) is not stored in the ISR, it directly reflects the state
       of the PCM FIFO */
    irq = (((vera.isr | (vera_audio_fifo_almost_empty() ? 8 : 0)) & vera.ien) & 0x0f)
          ? 1 : 0;

    if (irq != vera.irq_active) {
        vera.irq_active = irq;
        interrupt_set_irq(maincpu_int_status, vera.int_num, irq, maincpu_clk);
    }
}

/* Called once per scan line, 'y' is the line that is about to start. */
void vera_core_line_irq_update(uint16_t y)
{
    if (y == VERA_SCREEN_HEIGHT) {
        if (vera.sprite_line_collisions != 0) {
            vera.isr |= 4;
        }
        vera.isr = (vera.isr & 0xf) | vera.sprite_line_collisions;
        vera.sprite_line_collisions = 0;
        vera.isr |= 1; /* VSYNC IRQ */
    }
    if (y == vera.irq_line) { /* LINE IRQ */
        vera.isr |= 2;
    }

    vera_core_irq_update();
}

/* ------------------------------------------------------------------------- */
/* layer and sprite properties                                               */

static int calc_layer_eff_x(const struct video_layer_properties *props, int x)
{
    return (x + props->hscroll) & (props->layerw_max);
}

static int calc_layer_eff_y(const struct video_layer_properties *props, int y)
{
    return (y + props->vscroll) & (props->layerh_max);
}

static uint32_t calc_layer_map_addr_base2(const struct video_layer_properties *props,
                                          int eff_x, int eff_y)
{
    /* tilew and tileh are powers of two, so we can use shifts here */
    return props->map_base + ((((eff_y >> props->tileh_log2) << props->mapw_log2)
                               + (eff_x >> props->tilew_log2)) << 1);
}

static void refresh_layer_properties(uint8_t layer)
{
    struct video_layer_properties *props = &layer_properties[layer];
    uint16_t prev_layerw_max = props->layerw_max;
    uint16_t prev_hscroll = props->hscroll;
    uint16_t mapw = 0;
    uint16_t maph = 0;

    props->color_depth = vera.reg_layer[layer][0] & 0x3;
    props->map_base = vera.reg_layer[layer][1] << 9;
    props->tile_base = (vera.reg_layer[layer][2] & 0xFC) << 9;
    props->bitmap_mode = (vera.reg_layer[layer][0] & 0x4) != 0;
    props->text_mode = (props->color_depth == 0) && !props->bitmap_mode;
    props->text_mode_256c = (vera.reg_layer[layer][0] & 8) != 0;
    props->tile_mode = !props->bitmap_mode && !props->text_mode;

    if (!props->bitmap_mode) {
        props->hscroll = vera.reg_layer[layer][3] | (vera.reg_layer[layer][4] & 0xf) << 8;
        props->vscroll = vera.reg_layer[layer][5] | (vera.reg_layer[layer][6] & 0xf) << 8;
    } else {
        props->hscroll = 0;
        props->vscroll = 0;
    }

    props->tilew = 0;
    props->tileh = 0;

    if (props->tile_mode || props->text_mode) {
        props->mapw_log2 = 5 + ((vera.reg_layer[layer][0] >> 4) & 3);
        props->maph_log2 = 5 + ((vera.reg_layer[layer][0] >> 6) & 3);
        mapw = 1 << props->mapw_log2;
        maph = 1 << props->maph_log2;

        /* scale the tiles or text characters according to TILEW and TILEH */
        props->tilew_log2 = 3 + (vera.reg_layer[layer][2] & 1);
        props->tileh_log2 = 3 + ((vera.reg_layer[layer][2] >> 1) & 1);
        props->tilew = 1 << props->tilew_log2;
        props->tileh = 1 << props->tileh_log2;
    } else if (props->bitmap_mode) {
        /* bitmap mode is basically tiled mode with a single huge tile */
        props->tilew = (vera.reg_layer[layer][2] & 1) ? 640 : 320;
        props->tileh = VERA_SCREEN_HEIGHT;
    }

    /* mapw, maph, tilew and tileh are powers of two in all cases except in
       bitmap mode, so masks can be used instead of a modulo */
    props->mapw_max = mapw - 1;
    props->maph_max = maph - 1;
    props->tilew_max = props->tilew - 1;
    props->tileh_max = props->tileh - 1;
    props->layerw_max = (mapw * props->tilew) - 1;
    props->layerh_max = (maph * props->tileh) - 1;

    /* find min/max eff_x for bulk reading of the tile data while drawing */
    if (prev_layerw_max != props->layerw_max || prev_hscroll != props->hscroll) {
        int min_eff_x = INT_MAX;
        int max_eff_x = INT_MIN;
        int x;

        for (x = 0; x < VERA_SCREEN_WIDTH; ++x) {
            int eff_x = calc_layer_eff_x(props, x);
            if (eff_x < min_eff_x) {
                min_eff_x = eff_x;
            }
            if (eff_x > max_eff_x) {
                max_eff_x = eff_x;
            }
        }
        props->min_eff_x = min_eff_x;
        props->max_eff_x = max_eff_x;
    }

    props->bits_per_pixel = 1 << props->color_depth;
    props->tile_size_log2 = props->tilew_log2 + props->tileh_log2 + props->color_depth - 3;

    props->first_color_pos = 8 - props->bits_per_pixel;
    props->color_mask = (1 << props->bits_per_pixel) - 1;
    props->color_fields_max = (8 >> props->color_depth) - 1;
}

static void refresh_sprite_properties(uint16_t sprite)
{
    struct video_sprite_properties *props = &sprite_properties[sprite];

    props->sprite_zdepth = (vera.sprite_data[sprite][6] >> 2) & 3;
    props->sprite_collision_mask = vera.sprite_data[sprite][6] & 0xf0;

    props->sprite_x = vera.sprite_data[sprite][2] | (vera.sprite_data[sprite][3] & 3) << 8;
    props->sprite_y = vera.sprite_data[sprite][4] | (vera.sprite_data[sprite][5] & 3) << 8;
    props->sprite_width_log2 = (((vera.sprite_data[sprite][7] >> 4) & 3) + 3);
    props->sprite_height_log2 = ((vera.sprite_data[sprite][7] >> 6) + 3);
    props->sprite_width = 1 << props->sprite_width_log2;
    props->sprite_height = 1 << props->sprite_height_log2;

    /* fix up negative coordinates */
    if (props->sprite_x >= 0x400 - props->sprite_width) {
        props->sprite_x -= 0x400;
    }
    if (props->sprite_y >= 0x400 - props->sprite_height) {
        props->sprite_y -= 0x400;
    }

    props->hflip = vera.sprite_data[sprite][6] & 1;
    props->vflip = (vera.sprite_data[sprite][6] >> 1) & 1;

    props->color_mode = (vera.sprite_data[sprite][1] >> 7) & 1;
    props->sprite_address = vera.sprite_data[sprite][0] << 5
                            | (vera.sprite_data[sprite][1] & 0xf) << 13;

    props->palette_offset = (vera.sprite_data[sprite][7] & 0x0f) << 4;
}

void vera_core_refresh_all(void)
{
    unsigned int i;

    for (i = 0; i < VERA_NUM_LAYERS; i++) {
        refresh_layer_properties((uint8_t)i);
    }
    for (i = 0; i < VERA_NUM_SPRITES; i++) {
        refresh_sprite_properties((uint16_t)i);
    }

    memcpy(prev_layer_properties[0], layer_properties, sizeof(layer_properties));
    memcpy(prev_layer_properties[1], layer_properties, sizeof(layer_properties));
    memcpy(prev_reg_composer[0], vera.reg_composer, sizeof(vera.reg_composer));
    memcpy(prev_reg_composer[1], vera.reg_composer, sizeof(vera.reg_composer));

    palette_dirty = 1;
}

/* ------------------------------------------------------------------------- */
/* video RAM                                                                 */

uint8_t vera_core_vram_read(uint32_t address)
{
    return vera.ram[address & (VERA_RAM_SIZE - 1)];
}

static void video_space_read_range(uint8_t *dest, uint32_t address, uint32_t size)
{
    if ((address + size) <= VERA_RAM_SIZE) {
        memcpy(dest, &vera.ram[address], size);
    } else {
        uint32_t i;
        for (i = 0; i < size; ++i) {
            *dest++ = vera_core_vram_read(address + i);
        }
    }
}

void vera_core_vram_write(uint32_t address, uint8_t value)
{
    vera.ram[address & (VERA_RAM_SIZE - 1)] = value;

    if (address >= VERA_ADDR_PSG_START && address < VERA_ADDR_PSG_END) {
        vera_audio_psg_write((uint8_t)(address & 0x3f), value);
    } else if (address >= VERA_ADDR_PALETTE_START && address < VERA_ADDR_PALETTE_END) {
        vera.palette[address & 0x1ff] = value;
        palette_dirty = 1;
    } else if (address >= VERA_ADDR_SPRDATA_START && address < VERA_ADDR_SPRDATA_END) {
        vera.sprite_data[(address >> 3) & 0x7f][address & 0x7] = value;
        refresh_sprite_properties((address >> 3) & 0x7f);
    }
}

static void fx_video_space_write(uint32_t address, int nibble, uint8_t value)
{
    if (vera.fx_4bit_mode) {
        if (nibble) {
            if (!vera.fx_trans_writes || (value & 0x0f) > 0) {
                vera.ram[address & (VERA_RAM_SIZE - 1)] =
                    (vera.ram[address & (VERA_RAM_SIZE - 1)] & 0xf0) | (value & 0x0f);
            }
        } else {
            if (!vera.fx_trans_writes || (value & 0xf0) > 0) {
                vera.ram[address & (VERA_RAM_SIZE - 1)] =
                    (vera.ram[address & (VERA_RAM_SIZE - 1)] & 0x0f) | (value & 0xf0);
            }
        }
    } else {
        if (!vera.fx_trans_writes || value > 0) {
            vera.ram[address & (VERA_RAM_SIZE - 1)] = value;
        }
    }

    if (address >= VERA_ADDR_PSG_START && address < VERA_ADDR_PSG_END) {
        vera_audio_psg_write((uint8_t)(address & 0x3f), value);
    } else if (address >= VERA_ADDR_PALETTE_START && address < VERA_ADDR_PALETTE_END) {
        vera.palette[address & 0x1ff] = value;
        palette_dirty = 1;
    } else if (address >= VERA_ADDR_SPRDATA_START && address < VERA_ADDR_SPRDATA_END) {
        vera.sprite_data[(address >> 3) & 0x7f][address & 0x7] = value;
        refresh_sprite_properties((address >> 3) & 0x7f);
    }
}

static void fx_vram_cache_write(uint32_t address, uint8_t value, uint8_t mask)
{
    if (!vera.fx_trans_writes || value > 0) {
        switch (mask) {
            case 0:
                vera_core_vram_write(address, value);
                break;
            case 1:
                vera_core_vram_write(address,
                                     (uint8_t)((vera_core_vram_read(address) & 0x0f) | (value & 0xf0)));
                break;
            case 2:
                vera_core_vram_write(address,
                                     (uint8_t)((vera_core_vram_read(address) & 0xf0) | (value & 0x0f)));
                break;
            case 3:
                /* do nothing */
                break;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* data port                                                                 */

static uint32_t get_and_inc_address(uint8_t sel, int write)
{
    uint32_t address = vera.io_addr[sel];
    int16_t incr = increments[vera.io_inc[sel]];

    if (vera.fx_4bit_mode && vera.fx_nibble_incr[sel] && !incr) {
        if (vera.fx_nibble_bit[sel]) {
            if ((vera.io_inc[sel] & 1) == 0) {
                vera.io_addr[sel] += 1;
            }
            vera.fx_nibble_bit[sel] = 0;
        } else {
            if (vera.io_inc[sel] & 1) {
                vera.io_addr[sel] -= 1;
            }
            vera.fx_nibble_bit[sel] = 1;
        }
    }

    if (sel == 1 && vera.fx_16bit_hop) {
        if (incr == 4) {
            if (vera.fx_16bit_hop_align == (address & 0x3)) {
                incr = 1;
            } else {
                incr = 3;
            }
        } else if (incr == 320) {
            if (vera.fx_16bit_hop_align == (address & 0x3)) {
                incr = 1;
            } else {
                incr = 319;
            }
        }
    }

    vera.io_addr[sel] += incr;

    if (sel == 1 && vera.fx_addr1_mode == 1) { /* FX line draw mode */
        vera.fx_x_pixel_position += vera.fx_x_pixel_increment;
        if (vera.fx_x_pixel_position & 0x10000) {
            vera.fx_x_pixel_position &= ~0x10000;
            if (vera.fx_4bit_mode && vera.fx_nibble_incr[0]) {
                if (vera.fx_nibble_bit[1]) {
                    if ((vera.io_inc[0] & 1) == 0) {
                        vera.io_addr[1] += 1;
                    }
                    vera.fx_nibble_bit[1] = 0;
                } else {
                    if (vera.io_inc[0] & 1) {
                        vera.io_addr[1] -= 1;
                    }
                    vera.fx_nibble_bit[1] = 1;
                }
            }
            vera.io_addr[1] += increments[vera.io_inc[0]];
        }
    } else if (vera.fx_addr1_mode == 2 && write == 0) { /* FX polygon fill mode */
        vera.fx_x_pixel_position += vera.fx_x_pixel_increment;
        vera.fx_y_pixel_position += vera.fx_y_pixel_increment;
        vera.fx_poly_fill_length = (uint16_t)(((int32_t)vera.fx_y_pixel_position >> 16)
                                              - ((int32_t)vera.fx_x_pixel_position >> 16));
        if (sel == 0 && vera.fx_cache_byte_cycling && !vera.fx_cache_fill) {
            vera.fx_cache_byte_index = (vera.fx_cache_byte_index + 1) & 3;
        }
        if (sel == 1) {
            if (vera.fx_4bit_mode) {
                vera.io_addr[1] = vera.io_addr[0] + (vera.fx_x_pixel_position >> 17);
                vera.fx_nibble_bit[1] = (vera.fx_x_pixel_position >> 16) & 1;
            } else {
                vera.io_addr[1] = vera.io_addr[0] + (vera.fx_x_pixel_position >> 16);
            }
        }
    } else if (sel == 1 && vera.fx_addr1_mode == 3 && write == 0) { /* FX affine mode */
        vera.fx_x_pixel_position += vera.fx_x_pixel_increment;
        vera.fx_y_pixel_position += vera.fx_y_pixel_increment;
    }

    return address;
}

static void fx_affine_prefetch(void)
{
    uint32_t address;
    uint8_t affine_x_tile;
    uint8_t affine_y_tile;
    uint8_t affine_x_sub_tile;
    uint8_t affine_y_sub_tile;

    if (vera.fx_addr1_mode != 3) {
        return; /* only if affine mode is selected */
    }

    affine_x_tile = (vera.fx_x_pixel_position >> 19) & 0xff;
    affine_y_tile = (vera.fx_y_pixel_position >> 19) & 0xff;
    affine_x_sub_tile = (vera.fx_x_pixel_position >> 16) & 0x07;
    affine_y_sub_tile = (vera.fx_y_pixel_position >> 16) & 0x07;

    if (!vera.fx_affine_clip) { /* wrap */
        affine_x_tile &= vera.fx_affine_map_size - 1;
        affine_y_tile &= vera.fx_affine_map_size - 1;
    }

    if (affine_x_tile >= vera.fx_affine_map_size || affine_y_tile >= vera.fx_affine_map_size) {
        /* we clipped, return value for tile 0 */
        address = vera.fx_affine_tile_base + (affine_y_sub_tile << (3 - vera.fx_4bit_mode))
                  + (affine_x_sub_tile >> vera.fx_4bit_mode);
        vera.fx_nibble_bit[1] = (affine_x_sub_tile & 1) >> (1 - vera.fx_4bit_mode);
    } else {
        uint8_t affine_tile_idx;

        /* get the address within the tile map */
        address = vera.fx_affine_map_base + (affine_y_tile * vera.fx_affine_map_size) + affine_x_tile;
        /* translate that to the tile base address */
        affine_tile_idx = vera_core_vram_read(address);
        address = vera.fx_affine_tile_base + (affine_tile_idx << (6 - vera.fx_4bit_mode));
        /* add the sub-tile address */
        address += (affine_y_sub_tile << (3 - vera.fx_4bit_mode))
                   + (affine_x_sub_tile >> vera.fx_4bit_mode);
        vera.fx_nibble_bit[1] = (affine_x_sub_tile & 1) >> (1 - vera.fx_4bit_mode);
    }

    vera.io_addr[1] = address;
    vera.io_rddata[1] = vera_core_vram_read(address);
}

/* ------------------------------------------------------------------------- */
/* line rendering                                                            */

static void expand_4bpp_data(uint8_t *dst, const uint8_t *src, int dst_size)
{
    while (dst_size >= 2) {
        *dst = (*src) >> 4;
        ++dst;
        *dst = (*src) & 0xf;
        ++dst;

        ++src;
        dst_size -= 2;
    }
}

static void render_sprite_line(uint16_t y)
{
    uint16_t sprite_budget = 800 + 1;
    int i;

    memset(sprite_line_col, 0, VERA_SCREEN_WIDTH);
    memset(sprite_line_z, 0, VERA_SCREEN_WIDTH);
    memset(sprite_line_mask, 0, VERA_SCREEN_WIDTH);

    for (i = 0; i < VERA_NUM_SPRITES; i++) {
        const struct video_sprite_properties *props = &sprite_properties[i];
        uint16_t eff_sy;
        int16_t eff_sx;
        int16_t eff_sx_incr;
        const uint8_t *bitmap_data;
        uint8_t unpacked_sprite_line[64];
        uint16_t width;
        uint8_t vram_fetch_mask;
        uint16_t sx;

        /* one clock per lookup */
        sprite_budget--;
        if (sprite_budget == 0) {
            break;
        }

        if (props->sprite_zdepth == 0) {
            continue;
        }

        /* check whether this line falls within the sprite */
        if (y < props->sprite_y || y >= props->sprite_y + props->sprite_height) {
            continue;
        }

        eff_sy = props->vflip ? ((props->sprite_height - 1) - (y - props->sprite_y))
                              : (y - props->sprite_y);

        eff_sx = (props->hflip ? (props->sprite_width - 1) : 0);
        eff_sx_incr = props->hflip ? -1 : 1;

        bitmap_data = vera.ram + ((props->sprite_address
                                   + (eff_sy << (props->sprite_width_log2 - (1 - props->color_mode))))
                                  & (VERA_RAM_SIZE - 1));

        width = (props->sprite_width < 64 ? props->sprite_width : 64);
        vram_fetch_mask = ((2 - props->color_mode) << 2) - 1;

        if (props->color_mode == 0) {
            /* 4bpp */
            expand_4bpp_data(unpacked_sprite_line, bitmap_data, width);
        } else {
            /* 8bpp */
            memcpy(unpacked_sprite_line, bitmap_data, width);
        }

        for (sx = 0; sx < props->sprite_width; ++sx) {
            const uint16_t line_x = props->sprite_x + sx;
            uint8_t col_index;

            if (line_x >= VERA_SCREEN_WIDTH) {
                eff_sx += eff_sx_incr;
                continue;
            }

            /* one clock per fetched 32 bits */
            if (!(sx & vram_fetch_mask)) {
                sprite_budget--;
                if (sprite_budget == 0) {
                    break;
                }
            }

            /* one clock per rendered pixel */
            sprite_budget--;
            if (sprite_budget == 0) {
                break;
            }

            col_index = unpacked_sprite_line[eff_sx & 63];
            eff_sx += eff_sx_incr;

            /* palette offset */
            if (col_index > 0) {
                vera.sprite_line_collisions |= sprite_line_mask[line_x] & props->sprite_collision_mask;
                sprite_line_mask[line_x] |= props->sprite_collision_mask;

                if (props->sprite_zdepth > sprite_line_z[line_x]) {
                    if (col_index < 16) {
                        col_index += props->palette_offset;
                    }
                    sprite_line_col[line_x] = col_index;
                    sprite_line_z[line_x] = props->sprite_zdepth;
                }
            }
        }
    }
}

static void render_layer_line_text(uint8_t layer, uint16_t y)
{
    const struct video_layer_properties *props = &prev_layer_properties[1][layer];
    const struct video_layer_properties *props0 = &prev_layer_properties[0][layer];

    const uint8_t max_pixels_per_byte = (8 >> props->color_depth) - 1;
    const int eff_y = calc_layer_eff_y(props0, y);
    const int yy = eff_y & props->tileh_max;

    /* additional bytes to reach the correct line of the tile */
    const uint32_t y_add = (yy << props->tilew_log2) >> 3;

    const uint32_t map_addr_begin = calc_layer_map_addr_base2(props, props->min_eff_x, eff_y);
    const uint32_t map_addr_end = calc_layer_map_addr_base2(props, props->max_eff_x, eff_y);
    int size = (int)(map_addr_end - map_addr_begin) + 2;

    uint8_t tile_bytes[512]; /* max 256 tiles, 2 bytes each */
    uint32_t tile_start;
    uint8_t fg_color;
    uint8_t bg_color;
    uint8_t s;
    uint8_t color_shift;
    int x;

    if (size < 0) {
        size = 0;
    }
    if (size > (int)sizeof(tile_bytes)) {
        size = (int)sizeof(tile_bytes);
    }
    video_space_read_range(tile_bytes, map_addr_begin, (uint32_t)size);

    {
        const int eff_x = calc_layer_eff_x(props, 0);
        const int xx = eff_x & props->tilew_max;

        /* extract all information from the map */
        const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

        const uint8_t tile_index = tile_bytes[map_addr & 0x1ff];
        const uint8_t byte1 = tile_bytes[(map_addr + 1) & 0x1ff];

        uint16_t x_add;
        uint32_t tile_offset;

        if (!props->text_mode_256c) {
            fg_color = byte1 & 15;
            bg_color = byte1 >> 4;
        } else {
            fg_color = byte1;
            bg_color = 0;
        }

        /* offset within the tile map of the current tile */
        tile_start = tile_index << props->tile_size_log2;

        /* additional bytes to reach the correct column of the tile */
        x_add = xx >> 3;
        tile_offset = tile_start + y_add + x_add;

        s = vera_core_vram_read(props->tile_base + tile_offset);
        color_shift = max_pixels_per_byte - (xx & 0x7);
    }

    /* render the tile line */
    for (x = 0; x < VERA_SCREEN_WIDTH; x++) {
        /* scrolling */
        const int eff_x = calc_layer_eff_x(props, x);
        const int xx = eff_x & props->tilew_max;
        uint8_t col_index;

        if ((eff_x & 0x7) == 0) {
            uint16_t x_add;
            uint32_t tile_offset;

            if ((eff_x & props->tilew_max) == 0) {
                /* extract all information from the map */
                const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

                const uint8_t tile_index = tile_bytes[map_addr & 0x1ff];
                const uint8_t byte1 = tile_bytes[(map_addr + 1) & 0x1ff];

                if (!props->text_mode_256c) {
                    fg_color = byte1 & 15;
                    bg_color = byte1 >> 4;
                } else {
                    fg_color = byte1;
                    bg_color = 0;
                }

                /* offset within the tile map of the current tile */
                tile_start = tile_index << props->tile_size_log2;
            }

            /* additional bytes to reach the correct column of the tile */
            x_add = xx >> 3;
            tile_offset = tile_start + y_add + x_add;

            s = vera_core_vram_read(props->tile_base + tile_offset);
            color_shift = max_pixels_per_byte;
        }

        /* convert the tile byte to an indexed color */
        col_index = (s >> color_shift) & 1;
        --color_shift;
        layer_line[layer][x] = col_index ? fg_color : bg_color;
    }
}

static void render_layer_line_tile(uint8_t layer, uint16_t y)
{
    const struct video_layer_properties *props = &prev_layer_properties[1][layer];
    const struct video_layer_properties *props0 = &prev_layer_properties[0][layer];

    const uint8_t max_pixels_per_byte = (8 >> props->color_depth) - 1;
    const int eff_y = calc_layer_eff_y(props0, y);
    const uint8_t yy = eff_y & props->tileh_max;
    const uint8_t yy_flip = yy ^ props->tileh_max;
    const uint32_t y_add = (yy << ((props->tilew_log2 + props->color_depth - 3) & 31));
    const uint32_t y_add_flip = (yy_flip << ((props->tilew_log2 + props->color_depth - 3) & 31));

    const uint32_t map_addr_begin = calc_layer_map_addr_base2(props, props->min_eff_x, eff_y);
    const uint32_t map_addr_end = calc_layer_map_addr_base2(props, props->max_eff_x, eff_y);
    int size = (int)(map_addr_end - map_addr_begin) + 2;

    uint8_t tile_bytes[512]; /* max 256 tiles, 2 bytes each */

    uint8_t palette_offset;
    int vflip;
    int hflip;
    uint32_t tile_start;
    uint8_t s;
    uint8_t color_shift;
    int8_t color_shift_incr;
    int x;

    if (size < 0) {
        size = 0;
    }
    if (size > (int)sizeof(tile_bytes)) {
        size = (int)sizeof(tile_bytes);
    }
    video_space_read_range(tile_bytes, map_addr_begin, (uint32_t)size);

    {
        const int eff_x = calc_layer_eff_x(props, 0);

        /* extract all information from the map */
        const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

        const uint8_t byte0 = tile_bytes[map_addr & 0x1ff];
        const uint8_t byte1 = tile_bytes[(map_addr + 1) & 0x1ff];

        uint16_t tile_index;
        int xx;
        uint16_t x_add;
        uint32_t tile_offset;

        /* tile flipping */
        vflip = (byte1 >> 3) & 1;
        hflip = (byte1 >> 2) & 1;

        palette_offset = byte1 & 0xf0;

        /* offset within the tile map of the current tile */
        tile_index = byte0 | ((byte1 & 3) << 8);
        tile_start = tile_index << props->tile_size_log2;

        color_shift_incr = hflip ? props->bits_per_pixel : -props->bits_per_pixel;

        xx = eff_x & props->tilew_max;
        if (hflip) {
            xx = xx ^ (props->tilew_max);
            color_shift = 0;
        } else {
            color_shift = props->first_color_pos;
        }

        /* additional bytes to reach the correct column of the tile */
        x_add = (xx << props->color_depth) >> 3;
        tile_offset = tile_start + (vflip ? y_add_flip : y_add) + x_add;

        s = vera_core_vram_read(props->tile_base + tile_offset);
    }

    /* render the tile line */
    for (x = 0; x < VERA_SCREEN_WIDTH; x++) {
        const int eff_x = calc_layer_eff_x(props, x);
        uint8_t col_index;

        if ((eff_x & max_pixels_per_byte) == 0) {
            int xx;
            uint16_t x_add;
            uint32_t tile_offset;

            if ((eff_x & props->tilew_max) == 0) {
                /* extract all information from the map */
                const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

                const uint8_t byte0 = tile_bytes[map_addr & 0x1ff];
                const uint8_t byte1 = tile_bytes[(map_addr + 1) & 0x1ff];
                uint16_t tile_index;

                /* tile flipping */
                vflip = (byte1 >> 3) & 1;
                hflip = (byte1 >> 2) & 1;

                palette_offset = byte1 & 0xf0;

                /* offset within the tile map of the current tile */
                tile_index = byte0 | ((byte1 & 3) << 8);
                tile_start = tile_index << props->tile_size_log2;

                color_shift_incr = hflip ? props->bits_per_pixel : -props->bits_per_pixel;
            }

            xx = eff_x & props->tilew_max;
            if (hflip) {
                xx = xx ^ (props->tilew_max);
                color_shift = 0;
            } else {
                color_shift = props->first_color_pos;
            }

            /* additional bytes to reach the correct column of the tile */
            x_add = (xx << props->color_depth) >> 3;
            tile_offset = tile_start + (vflip ? y_add_flip : y_add) + x_add;

            s = vera_core_vram_read(props->tile_base + tile_offset);
        }

        /* convert the tile byte to an indexed color */
        col_index = (s >> color_shift) & props->color_mask;
        color_shift += color_shift_incr;

        /* apply the palette offset */
        if (col_index > 0 && col_index < 16) {
            col_index += palette_offset;
            if (props->text_mode_256c) {
                col_index |= 0x80;
            }
        }
        layer_line[layer][x] = col_index;
    }
}

static void render_layer_line_bitmap(uint8_t layer, uint16_t y)
{
    const struct video_layer_properties *props = &prev_layer_properties[1][layer];

    const int yy = y % props->tileh;
    /* additional bytes to reach the correct line of the tile */
    const uint32_t y_add = (yy * props->tilew * props->bits_per_pixel) >> 3;
    int x;

    /* render the tile line */
    for (x = 0; x < VERA_SCREEN_WIDTH; x++) {
        const int xx = x % props->tilew;

        /* extract all information from the map */
        const uint8_t palette_offset = vera.reg_layer[layer][4] & 0xf;

        /* additional bytes to reach the correct column of the tile */
        const uint16_t x_add = (xx * props->bits_per_pixel) >> 3;
        const uint32_t tile_offset = y_add + x_add;
        const uint8_t s = vera_core_vram_read(props->tile_base + tile_offset);

        /* convert the tile byte to an indexed color */
        uint8_t col_index = (s >> (props->first_color_pos
                                   - ((xx & props->color_fields_max) << props->color_depth)))
                            & props->color_mask;

        /* apply the palette offset */
        if (col_index > 0 && col_index < 16) {
            col_index += palette_offset << 4;
            if (props->text_mode_256c) {
                col_index |= 0x80;
            }
        }
        layer_line[layer][x] = col_index;
    }
}

static uint8_t calculate_line_col_index(uint8_t spr_zindex, uint8_t spr_col_index,
                                        uint8_t l1_col_index, uint8_t l2_col_index)
{
    uint8_t col_index = 0;

    switch (spr_zindex) {
        case 3:
            col_index = spr_col_index ? spr_col_index : (l2_col_index ? l2_col_index : l1_col_index);
            break;
        case 2:
            col_index = l2_col_index ? l2_col_index : (spr_col_index ? spr_col_index : l1_col_index);
            break;
        case 1:
            col_index = l2_col_index ? l2_col_index : (l1_col_index ? l1_col_index : spr_col_index);
            break;
        case 0:
            col_index = l2_col_index ? l2_col_index : l1_col_index;
            break;
    }

    return col_index;
}

/* Render one complete scan line into vera_core_line_buffer.
   Returns 1 if the line buffer was updated, 0 if the line was skipped (in
   warp mode most frames are not rendered at all).

   Unlike the X16 emulator this renders the whole line at once, so register
   changes only take effect on the next line (the two line register history
   of the original implementation is kept, so the usual raster effects still
   happen at the right time). */
int vera_core_render_line(uint16_t y)
{
    const uint8_t dc_video = vera.reg_composer[0];
    const uint16_t vstart = vera.reg_composer[6] << 1;
    const uint16_t vstop = vera.reg_composer[7] << 1;

    uint8_t out_mode;
    uint8_t border_color;
    uint16_t hstart;
    uint16_t hstop;
    uint16_t eff_y;
    uint32_t eff_x_fp; /* 16.16 fixed point */
    uint32_t scale;
    unsigned int layer;
    uint16_t x;

    if (y != y_prev) {
        y_prev = y;

        /* Copy the composer array to the 2 line history buffer so that raster
           effects that happen on a delay take effect at exactly the right
           time. */
        memcpy(prev_reg_composer[1], prev_reg_composer[0], sizeof(prev_reg_composer[0]));
        memcpy(prev_reg_composer[0], vera.reg_composer, sizeof(vera.reg_composer));

        /* same with the layer properties */
        memcpy(prev_layer_properties[1], prev_layer_properties[0], sizeof(prev_layer_properties[0]));
        memcpy(prev_layer_properties[0], layer_properties, sizeof(layer_properties));

        if ((dc_video & 3) > 1) { /* 480i or 240p */
            if ((y >> 1) == 0) {
                eff_y_fp = y * (prev_reg_composer[1][2] << 9);
            } else if (((y & 0xfffe) >= vstart) && ((y & 0xfffe) < vstop)) {
                eff_y_fp += (prev_reg_composer[1][2] << 10);
            }
        } else {
            if (y == 0) {
                eff_y_fp = 0;
            } else if ((y >= vstart) && (y < vstop)) {
                eff_y_fp += (prev_reg_composer[1][2] << 9);
            }
        }
    }

    if ((dc_video & 8) && (dc_video & 3) > 1) { /* progressive NTSC/RGB mode */
        y &= 0xfffe;
    }

    if (y >= VERA_SCREEN_HEIGHT) {
        return 0;
    }

    out_mode = vera.reg_composer[0] & 3;
    border_color = vera.reg_composer[3];
    hstart = vera.reg_composer[4] << 2;
    hstop = vera.reg_composer[5] << 2;

    eff_y = (eff_y_fp >> 16);
    if (eff_y >= 480) {
        eff_y = 480 - (y & 1);
    }

    layer_line_enable[0] = dc_video & 0x10;
    layer_line_enable[1] = dc_video & 0x20;
    sprite_line_enable = dc_video & 0x40;

    /* clear layer_line if the layer got disabled */
    for (layer = 0; layer < VERA_NUM_LAYERS; layer++) {
        if (!layer_line_enable[layer] && old_layer_line_enable[layer]) {
            memset(layer_line[layer], 0, VERA_SCREEN_WIDTH);
        }
        old_layer_line_enable[layer] = layer_line_enable[layer];
    }

    /* clear sprite_line if the sprites got disabled */
    if (!sprite_line_enable && old_sprite_line_enable) {
        memset(sprite_line_col, 0, VERA_SCREEN_WIDTH);
        memset(sprite_line_z, 0, VERA_SCREEN_WIDTH);
        memset(sprite_line_mask, 0, VERA_SCREEN_WIDTH);
    }
    old_sprite_line_enable = sprite_line_enable;

    if (sprite_line_enable) {
        render_sprite_line(eff_y);
    }

    if (vsync_get_warp_mode() && (vera.frame_count & 63)) {
        /* the sprites were needed for the collision IRQ, but everything else
           can be skipped most of the time while in warp mode */
        return 0;
    }

    if (layer_line_enable[0]) {
        if (prev_layer_properties[1][0].text_mode) {
            render_layer_line_text(0, eff_y);
        } else if (prev_layer_properties[1][0].bitmap_mode) {
            render_layer_line_bitmap(0, eff_y);
        } else {
            render_layer_line_tile(0, eff_y);
        }
    }
    if (layer_line_enable[1]) {
        if (prev_layer_properties[1][1].text_mode) {
            render_layer_line_text(1, eff_y);
        } else if (prev_layer_properties[1][1].bitmap_mode) {
            render_layer_line_bitmap(1, eff_y);
        } else {
            render_layer_line_tile(1, eff_y);
        }
    }

    if (out_mode == 0) {
        /* video generation is off, the palette shows a blue screen in that
           case (see vera-color.c), so just fill the line with color 0 */
        memset(vera_core_line_buffer, 0, VERA_SCREEN_WIDTH);
        return 1;
    }

    /* calculate the color indices for the line */
    if (y < vstart || y >= vstop) {
        memset(vera_core_line_buffer, border_color, VERA_SCREEN_WIDTH);
        return 1;
    }

    hstart = hstart < VERA_SCREEN_WIDTH ? hstart : VERA_SCREEN_WIDTH;
    hstop = hstop < VERA_SCREEN_WIDTH ? hstop : VERA_SCREEN_WIDTH;

    for (x = 0; x < hstart; ++x) {
        vera_core_line_buffer[x] = border_color;
    }

    eff_x_fp = 0;
    scale = vera.reg_composer[1];
    for (x = hstart; x < hstop; ++x) {
        uint16_t eff_x = eff_x_fp >> 16;
        vera_core_line_buffer[x] = (eff_x < VERA_SCREEN_WIDTH)
                                   ? calculate_line_col_index(sprite_line_z[eff_x],
                                                              sprite_line_col[eff_x],
                                                              layer_line[0][eff_x],
                                                              layer_line[1][eff_x])
                                   : 0;
        eff_x_fp += (scale << 9);
    }

    for (x = hstop; x < VERA_SCREEN_WIDTH; ++x) {
        vera_core_line_buffer[x] = border_color;
    }

    return 1;
}

/* ------------------------------------------------------------------------- */
/* register access                                                           */

static uint8_t video_get_dc_value(uint8_t reg)
{
    switch (reg & 0x1F) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            return vera.reg_composer[reg & 0x1F];
        default:
            break;
    }

    return 0;
}

uint8_t vera_core_reg_peek(uint8_t reg)
{
    uint16_t scanline = vera.scan_pos_y;

    if (scanline >= 512) {
        scanline = 511;
    }

    switch (reg & 0x1F) {
        case 0x00:
            return vera.io_addr[vera.io_addrsel] & 0xff;
        case 0x01:
            return (vera.io_addr[vera.io_addrsel] >> 8) & 0xff;
        case 0x02:
            return (vera.io_addr[vera.io_addrsel] >> 16)
                   | (vera.fx_nibble_bit[vera.io_addrsel] << 1)
                   | (vera.fx_nibble_incr[vera.io_addrsel] << 2)
                   | (vera.io_inc[vera.io_addrsel] << 3);
        case 0x03:
        case 0x04:
            return vera.io_rddata[(reg & 0x1f) - 3];
        case 0x05:
            return (vera.io_dcsel << 1) | vera.io_addrsel;
        case 0x06:
            return ((vera.irq_line & 0x100) >> 1) | ((scanline & 0x100) >> 2) | (vera.ien & 0xF);
        case 0x07:
            return vera.isr | (vera_audio_fifo_almost_empty() ? 8 : 0);
        case 0x08:
            return scanline & 0xFF;
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C: {
            int i = (reg & 0x1f) - 0x09 + (vera.io_dcsel << 2);
            switch (i) {
                case 0x00:
                case 0x01:
                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                case 0x16:
                case 0x17:
                    return video_get_dc_value((uint8_t)i);
                default:
                    break;
            }
            return vera_version_string[i % 4];
        }
        case 0x0D:
        case 0x0E:
        case 0x0F:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            return vera.reg_layer[0][(reg & 0x1f) - 0x0D];

        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
            return vera.reg_layer[1][(reg & 0x1f) - 0x14];

        case 0x1B: /* AUDIO_CTRL */
            return vera_audio_ctrl_read();
        case 0x1C: /* AUDIO_RATE */
            return vera_audio_rate_read();
        case 0x1D: /* AUDIO_DATA, write only */
            return 0;

        default:
            /* SPI registers, not emulated */
            break;
    }

    return 0;
}

uint8_t vera_core_reg_read(uint8_t reg)
{
    uint16_t scanline = vera.scan_pos_y;

    if (scanline >= 512) {
        scanline = 511;
    }

    switch (reg & 0x1F) {
        case 0x03:
        case 0x04: {
            const uint8_t sel = (reg & 0x1f) - 3;
            const int addr_nibble = vera.fx_nibble_bit[sel];
            uint8_t value;

            get_and_inc_address(sel, 0);

            value = vera.io_rddata[sel];

            if (sel == 1 && vera.fx_addr1_mode == 3) {
                fx_affine_prefetch();
            } else {
                vera.io_rddata[sel] = vera_core_vram_read(vera.io_addr[sel]);
            }

            if (vera.fx_cache_fill) {
                if (vera.fx_4bit_mode) {
                    uint8_t nibble_read = (addr_nibble ? ((value & 0x0f) << 4) : (value & 0xf0));

                    if (vera.fx_cache_nibble_index) {
                        vera.fx_cache[vera.fx_cache_byte_index] =
                            (vera.fx_cache[vera.fx_cache_byte_index] & 0xf0) | (nibble_read >> 4);
                        vera.fx_cache_nibble_index = 0;
                        vera.fx_cache_byte_index = ((vera.fx_cache_byte_index + 1) & 0x3);
                    } else {
                        vera.fx_cache[vera.fx_cache_byte_index] =
                            (vera.fx_cache[vera.fx_cache_byte_index] & 0x0f) | (nibble_read);
                        vera.fx_cache_nibble_index = 1;
                    }
                } else {
                    vera.fx_cache[vera.fx_cache_byte_index] = value;
                    if (vera.fx_cache_increment_mode) {
                        vera.fx_cache_byte_index = (vera.fx_cache_byte_index & 0x2)
                                                   | ((vera.fx_cache_byte_index + 1) & 0x1);
                    } else {
                        vera.fx_cache_byte_index = ((vera.fx_cache_byte_index + 1) & 0x3);
                    }
                }
            }

            return value;
        }

        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C: {
            int i = (reg & 0x1f) - 0x09 + (vera.io_dcsel << 2);
            switch (i) {
                case 0x00:
                case 0x01:
                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                case 0x08:
                case 0x16: /* DCSEL=5, $9F2B */
                case 0x17: /* DCSEL=5, $9F2C */
                    return video_get_dc_value((uint8_t)i);
                case 0x18: /* DCSEL=6, $9F29 */
                    vera.fx_mult_accumulator = 0;
                    break;
                case 0x19: { /* DCSEL=6, $9F2A */
                    int32_t m_result = (int16_t)((vera.fx_cache[1] << 8) | vera.fx_cache[0])
                                       * (int16_t)((vera.fx_cache[3] << 8) | vera.fx_cache[2]);
                    if (vera.fx_subtract) {
                        vera.fx_mult_accumulator -= m_result;
                    } else {
                        vera.fx_mult_accumulator += m_result;
                    }
                    break;
                }
                default:
                    /* The rest of the space is write-only, so reading the
                       values out instead returns the version string. */
                    break;
            }
            return vera_version_string[i % 4];
        }

        default:
            break;
    }

    return vera_core_reg_peek(reg);
}

void vera_core_reg_store(uint8_t reg, uint8_t value)
{
    switch (reg & 0x1F) {
        case 0x00:
            if (vera.fx_2bit_poly && vera.fx_4bit_mode && vera.fx_addr1_mode == 2
                && vera.io_addrsel == 1) {
                vera.fx_2bit_poking = 1;
                vera.io_addr[1] = (vera.io_addr[1] & 0x1fffc) | (value & 0x3);
            } else {
                vera.io_addr[vera.io_addrsel] = (vera.io_addr[vera.io_addrsel] & 0x1ff00) | value;
                if (vera.fx_16bit_hop && vera.io_addrsel == 1) {
                    vera.fx_16bit_hop_align = value & 3;
                }
            }
            vera.io_rddata[vera.io_addrsel] = vera_core_vram_read(vera.io_addr[vera.io_addrsel]);
            break;
        case 0x01:
            vera.io_addr[vera.io_addrsel] = (vera.io_addr[vera.io_addrsel] & 0x100ff) | (value << 8);
            vera.io_rddata[vera.io_addrsel] = vera_core_vram_read(vera.io_addr[vera.io_addrsel]);
            break;
        case 0x02:
            vera.io_addr[vera.io_addrsel] = (vera.io_addr[vera.io_addrsel] & 0x0ffff)
                                            | ((value & 0x1) << 16);
            vera.fx_nibble_bit[vera.io_addrsel] = (value >> 1) & 0x1;
            vera.fx_nibble_incr[vera.io_addrsel] = (value >> 2) & 0x1;
            vera.io_inc[vera.io_addrsel] = value >> 3;
            vera.io_rddata[vera.io_addrsel] = vera_core_vram_read(vera.io_addr[vera.io_addrsel]);
            break;
        case 0x03:
        case 0x04: {
            const uint8_t sel = (reg & 0x1f) - 3;
            int nibble;
            uint32_t address;
            uint8_t wrdata_to_use;
            uint8_t ram_wrdata[4];
            uint8_t nibble_mask[4];
            uint8_t cache_to_use[4];

            if (vera.fx_2bit_poking && vera.fx_addr1_mode) {
                uint8_t mask = value >> 6;
                vera.fx_2bit_poking = 0;
                switch (mask) {
                    case 0x00:
                        vera_core_vram_write(vera.io_addr[1],
                                             (uint8_t)((vera.fx_cache[vera.fx_cache_byte_index] & 0xc0)
                                                       | (vera.io_rddata[1] & 0x3f)));
                        break;
                    case 0x01:
                        vera_core_vram_write(vera.io_addr[1],
                                             (uint8_t)((vera.fx_cache[vera.fx_cache_byte_index] & 0x30)
                                                       | (vera.io_rddata[1] & 0xcf)));
                        break;
                    case 0x02:
                        vera_core_vram_write(vera.io_addr[1],
                                             (uint8_t)((vera.fx_cache[vera.fx_cache_byte_index] & 0x0c)
                                                       | (vera.io_rddata[1] & 0xf3)));
                        break;
                    case 0x03:
                        vera_core_vram_write(vera.io_addr[1],
                                             (uint8_t)((vera.fx_cache[vera.fx_cache_byte_index] & 0x03)
                                                       | (vera.io_rddata[1] & 0xfc)));
                        break;
                }
                break; /* break out of the enclosing switch statement early */
            }

            nibble = vera.fx_nibble_bit[sel];
            address = get_and_inc_address(sel, 1);

            if (vera.fx_multiplier) {
                int32_t m_result = (int16_t)((vera.fx_cache[1] << 8) | vera.fx_cache[0])
                                   * (int16_t)((vera.fx_cache[3] << 8) | vera.fx_cache[2]);
                if (vera.fx_subtract) {
                    m_result = vera.fx_mult_accumulator - m_result;
                } else {
                    m_result = vera.fx_mult_accumulator + m_result;
                }
                cache_to_use[0] = (m_result) & 0xff;
                cache_to_use[1] = (m_result >> 8) & 0xff;
                cache_to_use[2] = (m_result >> 16) & 0xff;
                cache_to_use[3] = (m_result >> 24) & 0xff;
            } else {
                memcpy(cache_to_use, vera.fx_cache, sizeof(vera.fx_cache));
            }

            if (vera.fx_cache_byte_cycling) {
                wrdata_to_use = vera.fx_cache[vera.fx_cache_byte_index];
            } else {
                wrdata_to_use = value;
            }

            if (vera.fx_cache_write && !vera.fx_cache_byte_cycling) {
                ram_wrdata[0] = cache_to_use[0];
                ram_wrdata[1] = cache_to_use[1];
                ram_wrdata[2] = cache_to_use[2];
                ram_wrdata[3] = cache_to_use[3];
            } else {
                ram_wrdata[0] = wrdata_to_use;
                ram_wrdata[1] = wrdata_to_use;
                ram_wrdata[2] = wrdata_to_use;
                ram_wrdata[3] = wrdata_to_use;
            }

            if (vera.fx_cache_write) {
                address &= 0x1fffc;
                if (vera.fx_trans_writes) {
                    if (vera.fx_4bit_mode) {
                        nibble_mask[0] = (((ram_wrdata[0] & 0xf0) == 0) << 1) | ((ram_wrdata[0] & 0x0f) == 0);
                        nibble_mask[1] = (((ram_wrdata[1] & 0xf0) == 0) << 1) | ((ram_wrdata[1] & 0x0f) == 0);
                        nibble_mask[2] = (((ram_wrdata[2] & 0xf0) == 0) << 1) | ((ram_wrdata[2] & 0x0f) == 0);
                        nibble_mask[3] = (((ram_wrdata[3] & 0xf0) == 0) << 1) | ((ram_wrdata[3] & 0x0f) == 0);
                    } else {
                        nibble_mask[0] = (ram_wrdata[0] != 0) ? 0 : 3;
                        nibble_mask[1] = (ram_wrdata[1] != 0) ? 0 : 3;
                        nibble_mask[2] = (ram_wrdata[2] != 0) ? 0 : 3;
                        nibble_mask[3] = (ram_wrdata[3] != 0) ? 0 : 3;
                    }
                } else {
                    nibble_mask[0] = value & 0x3;
                    nibble_mask[1] = (value >> 2) & 0x3;
                    nibble_mask[2] = (value >> 4) & 0x3;
                    nibble_mask[3] = (value >> 6) & 0x3;
                }

                fx_vram_cache_write(address + 0, ram_wrdata[0], nibble_mask[0]);
                fx_vram_cache_write(address + 1, ram_wrdata[1], nibble_mask[1]);
                fx_vram_cache_write(address + 2, ram_wrdata[2], nibble_mask[2]);
                fx_vram_cache_write(address + 3, ram_wrdata[3], nibble_mask[3]);
            } else {
                fx_video_space_write(address, nibble, wrdata_to_use); /* normal write */
            }

            vera.io_rddata[sel] = vera_core_vram_read(vera.io_addr[sel]);
            break;
        }
        case 0x05:
            if (value & 0x80) {
                vera_core_reset();
            }
            vera.io_dcsel = (value >> 1) & 0x3f;
            vera.io_addrsel = value & 1;
            break;
        case 0x06:
            vera.irq_line = (vera.irq_line & 0xFF) | ((value >> 7) << 8);
            vera.ien = value & 0xF;
            vera_core_irq_update();
            break;
        case 0x07:
            vera.isr &= value ^ 0xff;
            vera_core_irq_update();
            break;
        case 0x08:
            vera.irq_line = (vera.irq_line & 0x100) | value;
            break;

        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C: {
            int i = (reg & 0x1f) - 0x09 + (vera.io_dcsel << 2);

            if (i == 0) {
                /* the interlace field bit is read-only */
                vera.reg_composer[0] = (vera.reg_composer[0] & ~0x7f) | (value & 0x7f);
                palette_dirty = 1;
            } else {
                vera.reg_composer[i] = value;
            }

            switch (i) {
                case 0x08: /* DCSEL=2, $9F29 */
                    vera.fx_addr1_mode = value & 0x03;
                    vera.fx_4bit_mode = (value & 0x04) >> 2;
                    vera.fx_16bit_hop = (value & 0x08) >> 3;
                    vera.fx_cache_byte_cycling = (value & 0x10) >> 4;
                    vera.fx_cache_fill = (value & 0x20) >> 5;
                    vera.fx_cache_write = (value & 0x40) >> 6;
                    vera.fx_trans_writes = (value & 0x80) >> 7;
                    break;
                case 0x09: /* DCSEL=2, $9F2A */
                    vera.fx_affine_tile_base = (value & 0xfc) << 9;
                    vera.fx_affine_clip = (value & 0x02) >> 1;
                    vera.fx_2bit_poly = (value & 0x01);
                    break;
                case 0x0a: /* DCSEL=2, $9F2B */
                    vera.fx_affine_map_base = (value & 0xfc) << 9;
                    vera.fx_affine_map_size = 2 << ((value & 0x03) << 1);
                    break;
                case 0x0b: /* DCSEL=2, $9F2C */
                    vera.fx_cache_increment_mode = value & 0x01;
                    vera.fx_cache_nibble_index = (value & 0x02) >> 1;
                    vera.fx_cache_byte_index = (value & 0x0c) >> 2;
                    vera.fx_multiplier = (value & 0x10) >> 4;
                    vera.fx_subtract = (value & 0x20) >> 5;
                    if (value & 0x40) { /* accumulate */
                        int32_t m_result = (int16_t)((vera.fx_cache[1] << 8) | vera.fx_cache[0])
                                           * (int16_t)((vera.fx_cache[3] << 8) | vera.fx_cache[2]);
                        if (vera.fx_subtract) {
                            vera.fx_mult_accumulator -= m_result;
                        } else {
                            vera.fx_mult_accumulator += m_result;
                        }
                    }
                    if (value & 0x80) { /* reset accumulator */
                        vera.fx_mult_accumulator = 0;
                    }
                    break;
                case 0x0c: /* DCSEL=3, $9F29 */
                case 0x0d: /* DCSEL=3, $9F2A */
                    vera.fx_x_pixel_increment = ((((vera.reg_composer[0x0d] & 0x7f) << 15)
                                                  + (vera.reg_composer[0x0c] << 7)) /* base value */
                                                 | ((vera.reg_composer[0x0d] & 0x40) ? 0xffc00000 : 0))
                                                /* sign extend if negative */
                                                << 5 * (!!(vera.reg_composer[0x0d] & 0x80));
                                                /* multiply by 32 if the flag is set */
                    if (i == 0x0d && (vera.fx_addr1_mode == 1 || vera.fx_addr1_mode == 2)) {
                        /* reset the subpixel to 0.5 */
                        vera.fx_x_pixel_position = (vera.fx_x_pixel_position & 0x07ff0000) | 0x00008000;
                    }
                    break;
                case 0x0e: /* DCSEL=3, $9F2B */
                case 0x0f: /* DCSEL=3, $9F2C */
                    vera.fx_y_pixel_increment = ((((vera.reg_composer[0x0f] & 0x7f) << 15)
                                                  + (vera.reg_composer[0x0e] << 7)) /* base value */
                                                 | ((vera.reg_composer[0x0f] & 0x40) ? 0xffc00000 : 0))
                                                /* sign extend if negative */
                                                << 5 * (!!(vera.reg_composer[0x0f] & 0x80));
                                                /* multiply by 32 if the flag is set */
                    if (i == 0x0f && (vera.fx_addr1_mode == 1 || vera.fx_addr1_mode == 2)) {
                        /* reset the subpixel to 0.5 */
                        vera.fx_y_pixel_position = (vera.fx_y_pixel_position & 0x07ff0000) | 0x00008000;
                    }
                    break;
                case 0x10: /* DCSEL=4, $9F29 */
                    vera.fx_x_pixel_position = (vera.fx_x_pixel_position & 0x0700ff80) | (value << 16);
                    fx_affine_prefetch();
                    break;
                case 0x11: /* DCSEL=4, $9F2A */
                    vera.fx_x_pixel_position = (vera.fx_x_pixel_position & 0x00ffff00)
                                               | ((value & 0x7) << 24) | (value & 0x80);
                    fx_affine_prefetch();
                    break;
                case 0x12: /* DCSEL=4, $9F2B */
                    vera.fx_y_pixel_position = (vera.fx_y_pixel_position & 0x0700ff80) | (value << 16);
                    fx_affine_prefetch();
                    break;
                case 0x13: /* DCSEL=4, $9F2C */
                    vera.fx_y_pixel_position = (vera.fx_y_pixel_position & 0x00ffff00)
                                               | ((value & 0x7) << 24) | (value & 0x80);
                    fx_affine_prefetch();
                    break;
                case 0x14: /* DCSEL=5, $9F29 */
                    vera.fx_x_pixel_position = (vera.fx_x_pixel_position & 0x07ff0080) | (value << 8);
                    break;
                case 0x15: /* DCSEL=5, $9F2A */
                    vera.fx_y_pixel_position = (vera.fx_y_pixel_position & 0x07ff0080) | (value << 8);
                    break;
                case 0x18: /* DCSEL=6, $9F29 */
                    vera.fx_cache[0] = value;
                    break;
                case 0x19: /* DCSEL=6, $9F2A */
                    vera.fx_cache[1] = value;
                    break;
                case 0x1a: /* DCSEL=6, $9F2B */
                    vera.fx_cache[2] = value;
                    break;
                case 0x1b: /* DCSEL=6, $9F2C */
                    vera.fx_cache[3] = value;
                    break;
            }
            break;
        }

        case 0x0D:
        case 0x0E:
        case 0x0F:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            vera.reg_layer[0][(reg & 0x1f) - 0x0D] = value;
            refresh_layer_properties(0);
            break;

        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
            vera.reg_layer[1][(reg & 0x1f) - 0x14] = value;
            refresh_layer_properties(1);
            break;

        case 0x1B: /* AUDIO_CTRL */
            vera_audio_ctrl_write(value);
            break;
        case 0x1C: /* AUDIO_RATE */
            vera_audio_rate_write(value);
            break;
        case 0x1D: /* AUDIO_DATA */
            vera_audio_fifo_write(value);
            break;

        default:
            /* SPI registers ($1E-$1F), not emulated */
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* reset                                                                     */

void vera_core_reset(void)
{
    int i;

    /* init I/O registers */
    memset(vera.io_addr, 0, sizeof(vera.io_addr));
    memset(vera.io_inc, 0, sizeof(vera.io_inc));
    vera.io_addrsel = 0;
    vera.io_dcsel = 0;
    vera.io_rddata[0] = 0;
    vera.io_rddata[1] = 0;

    vera.ien = 0;
    vera.isr = 0;
    vera.irq_line = 0;
    vera_core_irq_update();

    /* init the layer registers */
    memset(vera.reg_layer, 0, sizeof(vera.reg_layer));

    /* init the composer registers */
    memset(vera.reg_composer, 0, sizeof(vera.reg_composer));
    vera.reg_composer[1] = 128;      /* hscale = 1.0 */
    vera.reg_composer[2] = 128;      /* vscale = 1.0 */
    vera.reg_composer[5] = 640 >> 2; /* hstop */
    vera.reg_composer[7] = 480 >> 1; /* vstop */

    /* initialize the FX registers */
    vera.fx_addr1_mode = 0;
    vera.fx_x_pixel_position = 0x8000;
    vera.fx_y_pixel_position = 0x8000;
    vera.fx_x_pixel_increment = 0;
    vera.fx_y_pixel_increment = 0;

    vera.fx_cache_write = 0;
    vera.fx_cache_fill = 0;
    vera.fx_4bit_mode = 0;
    vera.fx_16bit_hop = 0;
    vera.fx_subtract = 0;
    vera.fx_cache_byte_cycling = 0;
    vera.fx_trans_writes = 0;
    vera.fx_multiplier = 0;

    vera.fx_mult_accumulator = 0;

    vera.fx_2bit_poly = 0;
    vera.fx_2bit_poking = 0;

    vera.fx_cache_nibble_index = 0;
    vera.fx_cache_byte_index = 0;
    vera.fx_cache_increment_mode = 0;

    vera.fx_cache[0] = 0;
    vera.fx_cache[1] = 0;
    vera.fx_cache[2] = 0;
    vera.fx_cache[3] = 0;

    vera.fx_16bit_hop_align = 0;

    vera.fx_nibble_bit[0] = 0;
    vera.fx_nibble_bit[1] = 0;
    vera.fx_nibble_incr[0] = 0;
    vera.fx_nibble_incr[1] = 0;

    vera.fx_poly_fill_length = 0;
    vera.fx_affine_tile_base = 0;
    vera.fx_affine_map_base = 0;
    vera.fx_affine_map_size = 2;
    vera.fx_affine_clip = 0;

    /* init the sprite attribute RAM */
    memset(vera.sprite_data, 0, sizeof(vera.sprite_data));

    /* copy the default palette */
    for (i = 0; i < VERA_NUM_COLORS; i++) {
        vera.palette[i * 2 + 0] = default_palette[i] & 0xff;
        vera.palette[i * 2 + 1] = default_palette[i] >> 8;
    }

    vera.sprite_line_collisions = 0;
    vera.scan_pos_y = 0;

    vera_audio_reset();

    /* reset the rendering state */
    memset(layer_line, 0, sizeof(layer_line));
    memset(sprite_line_col, 0, sizeof(sprite_line_col));
    memset(sprite_line_z, 0, sizeof(sprite_line_z));
    memset(sprite_line_mask, 0, sizeof(sprite_line_mask));
    memset(vera_core_line_buffer, 0, sizeof(vera_core_line_buffer));
    layer_line_enable[0] = layer_line_enable[1] = 0;
    old_layer_line_enable[0] = old_layer_line_enable[1] = 0;
    sprite_line_enable = 0;
    old_sprite_line_enable = 0;
    y_prev = 0;
    eff_y_fp = 0;

    vera_core_refresh_all();
}

void vera_core_powerup(void)
{
    unsigned int i;

    /* fill the video RAM with random data, like the X16 emulator does */
    for (i = 0; i < VERA_RAM_SIZE; i++) {
        vera.ram[i] = (uint8_t)(lib_unsigned_rand(0, 255));
    }

    vera_core_reset();
}

void vera_core_shutdown(void)
{
    /* nothing to do, all state is part of the vera struct */
}
