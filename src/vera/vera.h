/*
 * vera.h - A VERA (Commander X16 video chip) emulation as a second screen
 *          for the C64, mapped at $DE00-$DE1F.
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

#ifndef VICE_VERA_H
#define VICE_VERA_H

#include "types.h"

struct canvas_refresh_s;
struct raster_s;
struct screenshot_s;
struct snapshot_s;
struct video_canvas_s;

/* resources / command line options */
int vera_resources_init(void);
void vera_resources_shutdown(void);
int vera_cmdline_options_init(void);

/* Is the VERA enabled? (resource "VERA") */
int vera_enabled(void);

/* Current I/O-1 base address (resource "VERABase"), any 32 byte boundary in
   $DE00-$DEE0, $DE00 by default */
int vera_base_address(void);

/* chip life cycle, called from the C64 machine code */
struct raster_s *vera_init(void);
void vera_shutdown(void);
void vera_reset(void);
void vera_powerup(void);

/* canvas access, used by the UI and the machine video code */
struct video_canvas_s *vera_get_canvas(void);
void vera_screenshot(struct screenshot_s *screenshot);
void vera_async_refresh(struct canvas_refresh_s *refresh);

/* snapshot support */
int vera_snapshot_write_module(struct snapshot_s *s);
int vera_snapshot_read_module(struct snapshot_s *s);

#endif
