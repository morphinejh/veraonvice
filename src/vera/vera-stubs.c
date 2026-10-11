/*
 * vera-stubs.c - Stubs for emulators that share C64 code but have no VERA.
 *
 * Written by
 *  Jason Hill
 *
 * The VERA second screen only exists for x64/x64sc, but some of the C64
 * sources (c64video.c for example) are also used by vsid, x64dtv and
 * xscpu64. Those link these stubs instead of the VERA emulation.
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

#include "types.h"
#include "vera.h"
#include "vera-audio.h"
#include "vera-mem.h"

int vera_resources_init(void)
{
    return 0;
}

int vera_cmdline_options_init(void)
{
    return 0;
}

int vera_enabled(void)
{
    return 0;
}

struct raster_s *vera_init(void)
{
    return NULL;
}

void vera_shutdown(void)
{
}

void vera_reset(void)
{
}

void vera_powerup(void)
{
}

struct video_canvas_s *vera_get_canvas(void)
{
    return NULL;
}

void vera_screenshot(struct screenshot_s *screenshot)
{
}

void vera_async_refresh(struct canvas_refresh_s *refresh)
{
}

int vera_snapshot_write_module(struct snapshot_s *s)
{
    return 0;
}

int vera_snapshot_read_module(struct snapshot_s *s)
{
    return 0;
}

void vera_mem_set_enabled(int enabled)
{
}

void vera_sound_chip_init(void)
{
}

void vera_audio_set_enabled(int enabled)
{
}

uint8_t vera_mem_read(uint16_t addr)
{
    return 0;
}

uint8_t vera_mem_peek(uint16_t addr)
{
    return 0;
}

void vera_mem_store(uint16_t addr, uint8_t value)
{
}
