/*
 * vera-color.c - Colors for the VERA emulation.
 *
 * Written by
 *  Jason Hill
 *
 * The conversion of the VERA palette RAM into RGB values follows
 * refresh_palette() of the Commander X16 emulator
 * (https://github.com/X16Community/x16-emulator), src/video.c:
 *
 *   Commander X16 Emulator
 *   Copyright (c) 2019 Michael Steil
 *   Copyright (c) 2020 Frank van den Hoef
 *   All rights reserved. License: 2-clause BSD
 *
 * The full text of that license is in LICENSE.x16-emulator in this directory.
 *
 * Unlike the other video chips emulated by VICE the VERA has a completely
 * programmable palette (256 entries of 12 bit RGB in its video RAM), so the
 * palette handed to the VICE video code is rebuilt whenever the palette RAM
 * or the output mode of the display composer changes.
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

#include "vera-color.h"
#include "vera-core.h"
#include "veratypes.h"
#include "video.h"

/* names of the palette entries, "VERA 0" ... "VERA 255" */
static char vera_color_names[VERA_NUM_COLORS][12];

static video_cbm_color_t vera_colors[VERA_NUM_COLORS];

static video_cbm_palette_t vera_palette = {
    VERA_NUM_COLORS,
    vera_colors,
    NULL, NULL,
    0.0f,
    CBM_PALETTE_RGB
};

static int names_initialized = 0;

/* Recalculate the RGB values of all palette entries from the VERA palette
   RAM. This mirrors refresh_palette() of the X16 emulator. */
static void vera_color_refresh_entries(void)
{
    const uint8_t out_mode = vera.reg_composer[0] & 3;
    const int chroma_disable = ((vera.reg_composer[0] & 0x07) == 6);
    unsigned int i;

    if (!names_initialized) {
        for (i = 0; i < VERA_NUM_COLORS; i++) {
            snprintf(vera_color_names[i], sizeof(vera_color_names[i]), "VERA %u", i);
            vera_colors[i].name = vera_color_names[i];
            vera_colors[i].direction = 0;
        }
        names_initialized = 1;
    }

    for (i = 0; i < VERA_NUM_COLORS; i++) {
        uint8_t r, g, b;

        if (out_mode == 0) {
            /* video generation is off -> show a blue screen */
            r = 0;
            g = 0;
            b = 255;
        } else {
            uint16_t entry = vera.palette[i * 2] | vera.palette[i * 2 + 1] << 8;

            r = ((entry >> 8) & 0xf) << 4 | ((entry >> 8) & 0xf);
            g = ((entry >> 4) & 0xf) << 4 | ((entry >> 4) & 0xf);
            b = (entry & 0xf) << 4 | (entry & 0xf);

            if (chroma_disable) {
                r = g = b = (uint8_t)((r + g + b) / 3);
            }
        }

        /* for CBM_PALETTE_RGB the video code (ab)uses the luminance, angle
           and saturation fields for red, green and blue */
        vera_colors[i].luminance = (float)r;
        vera_colors[i].angle = (float)g;
        vera_colors[i].saturation = (float)b;
    }
}

int vera_color_update_palette(struct video_canvas_s *canvas)
{
    vera_color_refresh_entries();
    video_color_palette_internal(canvas, &vera_palette);

    return 0;
}
