/*
 * vera-resources.c - Resources for the VERA emulation.
 *
 * Written by
 *  Jason Hill
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

#include "archdep.h"
#include "fullscreen.h"
#include "log.h"
#include "raster-resources.h"
#include "resources.h"
#include "vera.h"
#include "vera-audio.h"
#include "vera-mem.h"
#include "vera-resources.h"
#include "veratypes.h"
#include "video.h"

/* Flag: is the VERA enabled? */
static int vera_is_enabled = 1;

/* I/O-1 base address; the real VERA's address decoding can be relocated to
   any 32 byte boundary in I/O-1 ($DE00-$DEFF) */
static int vera_base = VERA_REGS_BASE;

static video_chip_cap_t video_chip_cap;

int vera_enabled(void)
{
    return vera_is_enabled;
}

int vera_base_address(void)
{
    return vera_base;
}

static int set_vera_enabled(int value, void *param)
{
    vera_is_enabled = value ? 1 : 0;

    /* Before the chip is initialized the value is only remembered, the I/O
       registration happens in vera_init(). Enabling the VERA after the
       emulator has been started does not create the second window, that only
       happens on the next start of the emulator. */
    if (vera.initialized) {
        vera_mem_set_enabled(vera_is_enabled);
        vera_audio_set_enabled(vera_is_enabled);
    }

    return 0;
}

static int set_vera_base(int value, void *param)
{
    /* any 32 byte boundary in I/O-1 ($DE00-$DEE0 is the last one that still
       leaves room for all 32 registers before $DF00) */
    if (value < VERA_REGS_BASE || value > 0xdee0 || (value & 0x1f) != 0) {
        return -1;
    }

    if (value == vera_base) {
        return 0;
    }

    vera_base = value;

    /* if the VERA is already registered, move it to the new address right
       away; otherwise the new address is picked up the next time it is
       enabled */
    if (vera.initialized) {
        vera_mem_set_base();
    }

    return 0;
}

static const resource_int_t resources_int[] =
{
    { "VERA", 1, RES_EVENT_STRICT, (resource_value_t)1,
      &vera_is_enabled, set_vera_enabled, NULL },
    { "VERABase", VERA_REGS_BASE, RES_EVENT_STRICT, (resource_value_t)VERA_REGS_BASE,
      &vera_base, set_vera_base, NULL },
    RESOURCE_INT_LIST_END
};

int vera_resources_init(void)
{
    video_chip_cap.dsize_allowed = 1;
    video_chip_cap.dsize_default = 0;
    video_chip_cap.dsize_limit_width = 0;
    video_chip_cap.dsize_limit_height = 0;
    video_chip_cap.dscan_allowed = 1;
    video_chip_cap.interlace_allowed = 0;
    /* no palette file is shipped for the VERA, its palette is generated from
       the palette RAM of the chip (see vera-color.c) */
    video_chip_cap.external_palette_name = "vera";

    video_chip_cap.single_mode.sizex = 1;
    video_chip_cap.single_mode.sizey = 1;
    video_chip_cap.single_mode.rmode = VIDEO_RENDER_RGBI_1X1;
    video_chip_cap.double_mode.sizex = 2;
    video_chip_cap.double_mode.sizey = 2;
    video_chip_cap.double_mode.rmode = VIDEO_RENDER_RGBI_2X2;

    video_chip_cap.video_has_palntsc = 0;

    fullscreen_capability(&(video_chip_cap.fullscreen));

    vera.video_chip_cap = &video_chip_cap;

    if (raster_resources_chip_init("VERA", &vera.raster, &video_chip_cap) < 0) {
        return -1;
    }

    return resources_register_int(resources_int);
}
