/*
 * vera.c - VERA emulation for VICE, used as a second screen for the C64.
 *
 * Written by
 *  Jason Hill
 *
 * This file contains the glue between the VERA chip emulation (vera-core.c,
 * derived from the Commander X16 emulator) and the VICE raster/canvas code:
 * one VICE alarm emulates one VERA scan line, the rendered line is handed to
 * the raster code which takes care of the second emulator window.
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

#include "alarm.h"
#include "interrupt.h"
#include "log.h"
#include "machine.h"
#include "maincpu.h"
#include "raster.h"
#include "raster-line.h"
#include "raster-modes.h"
#include "resources.h"
#include "screenshot.h"
#include "types.h"
#include "vera.h"
#include "vera-audio.h"
#include "vera-color.h"
#include "vera-core.h"
#include "veratypes.h"
#include "video.h"
#include "viewport.h"

vera_t vera;

static void vera_raster_draw_alarm_handler(CLOCK offset, void *data);

/* ------------------------------------------------------------------------- */
/* raster interface                                                          */

/* Set when the current scan line was actually rendered. In warp mode most
   frames are skipped, in that case the previous frame is left alone instead
   of being overwritten with a stale line. */
static int line_rendered;

/* Draw one line into the raster draw buffer. The line has already been
   rendered into vera_core_line_buffer by the alarm handler. */
static void vera_draw_line(void)
{
    if (line_rendered) {
        memcpy(vera.raster.draw_buffer_ptr, vera_core_line_buffer, VERA_SCREEN_WIDTH);
    }
}

/* The following are only needed to fill the raster mode table, the VERA
   raster runs with the line cache disabled so they are never called. */
static int vera_fill_cache(struct raster_cache_s *cache, unsigned int *changed_start,
                           unsigned int *changed_end, int no_check)
{
    return 1;
}

static void vera_draw_line_cached(struct raster_cache_s *cache, unsigned int start,
                                  unsigned int end)
{
    vera_draw_line();
}

static void vera_draw_background(unsigned int start_pixel, unsigned int end_pixel)
{
}

static void vera_draw_foreground(unsigned int start_char, unsigned int end_char)
{
}

static void vera_set_geometry(void)
{
    raster_t *raster = &vera.raster;

    /* the VERA has no border in the VIC-II sense, the display composer
       generates the border colour itself, so the whole line is "graphics" */
    raster->display_ystart = 0;
    raster->display_ystop = VERA_SCREEN_HEIGHT;
    raster->display_xstart = 0;
    raster->display_xstop = VERA_SCREEN_WIDTH;

    raster_set_geometry(raster,
                        VERA_SCREEN_WIDTH, VERA_SCREEN_HEIGHT,   /* canvas size */
                        VERA_SCREEN_WIDTH, VERA_SCAN_HEIGHT,     /* virtual screen size */
                        VERA_SCREEN_WIDTH, VERA_SCREEN_HEIGHT,   /* size of the gfx area */
                        VERA_SCREEN_WIDTH / 8, VERA_SCREEN_HEIGHT / 8, /* gfx area in chars */
                        0, 0,                                    /* gfx position */
                        0,                                       /* gfx area doesn't move */
                        0,                                       /* first displayed line */
                        VERA_SCREEN_HEIGHT - 1,                  /* last displayed line */
                        0, 0);                                   /* extra offscreen border */

    /* the VERA generates a 640x480 VGA signal with square pixels */
    raster->geometry->pixel_aspect_ratio = 1.0f;
    raster->geometry->char_pixel_width = 8;
    raster->viewport->crt_type = VIDEO_CRT_TYPE_RGB;
}

static int init_raster(void)
{
    raster_t *raster = &vera.raster;

    raster->sprite_status = NULL;
    raster_line_changes_init(raster);

    if (raster_init(raster, VERA_NUM_VMODES) < 0) {
        return -1;
    }

    raster_modes_set(raster->modes, 0,
                     vera_fill_cache,
                     vera_draw_line_cached,
                     vera_draw_line,
                     vera_draw_background,
                     vera_draw_foreground);
    raster_modes_set_idle_mode(raster->modes, 0);
    raster->video_mode = 0;

    /* the VERA renders every line from scratch, so the raster line cache is
       of no use here */
    raster_enable_cache(raster, 0);
    raster->dont_cache = 1;
    raster->dont_cache_all = 1;

    /* the VERA draws the complete line itself, including its border */
    raster->border_disable = 1;
    raster->can_disable_border = 0;

    vera_set_geometry();

    if (vera_color_update_palette(vera.raster.canvas) < 0) {
        log_error(vera.log, "Cannot load palette.");
        return -1;
    }

    if (raster_realize(raster) < 0) {
        return -1;
    }

    raster->border_color = 0;

    return 0;
}

/* ------------------------------------------------------------------------- */
/* line timing                                                               */

/* Calculate how many CPU cycles one VERA scan line takes (16.16 fixed
   point). The VERA runs with its own clock, so this depends on the speed of
   the emulated machine. */
static void vera_update_line_timing(void)
{
    long cycles_per_sec = machine_get_cycles_per_second();

    if (cycles_per_sec == vera.cycles_per_sec) {
        return;
    }

    vera.cycles_per_sec = cycles_per_sec;
    vera.line_increment = (unsigned int)(((double)cycles_per_sec * 65536.0)
                                         / (double)VERA_LINE_FREQ);

    if (vera.line_increment == 0) {
        vera.line_increment = 1 << 16;
    }
}

static void vera_set_next_alarm(CLOCK offset)
{
    unsigned int next_alarm;

    vera.line_accu += vera.line_increment;
    next_alarm = vera.line_accu >> 16;
    vera.line_accu -= (next_alarm << 16);

    alarm_set(vera.raster_draw_alarm, maincpu_clk + (CLOCK)next_alarm - offset);
}

/* ------------------------------------------------------------------------- */

static void vera_raster_draw_alarm_handler(CLOCK offset, void *data)
{
    /* vera_core_reset() can be triggered by a soft chip-level reset
       (writing bit 7 to $DE05), independently of vera_reset(). That path
       zeroes vera.scan_pos_y but has no knowledge of VICE's separate
       raster->current_line bookkeeping, so the two can end up out of sync,
       causing the displayed picture to be rendered starting from the wrong
       line. Re-establish the invariant before using either. */
    if (vera.raster.current_line != vera.scan_pos_y) {
        vera.raster.current_line = vera.scan_pos_y;
    }

    /* keep the PCM engine in sync with the CPU (this is also done on every
       audio register access, this call makes sure the FIFO keeps draining
       while the CPU is doing something else) */
    vera_audio_run();

    /* render the scan line the VERA is currently scanning out */
    line_rendered = vera_core_render_line(vera.scan_pos_y);

    /* hand it over to the raster code, which calls vera_draw_line() for the
       visible lines and takes care of the end of frame handling */
    raster_line_emulate(&vera.raster);

    /* advance the VERA scan position */
    vera.scan_pos_y++;
    if (vera.scan_pos_y == VERA_SCAN_HEIGHT) {
        vera.scan_pos_y = 0;
        vera.frame_count++;

        /* the palette lives in the VERA video RAM and can be changed at any
           time, so it is picked up once per frame */
        if (vera_core_palette_is_dirty()) {
            vera_core_palette_clear_dirty();
            vera_color_update_palette(vera.raster.canvas);
        }

        /* the machine may have switched between PAL and NTSC */
        vera_update_line_timing();
    }

    /* VSYNC/LINE interrupts and the sprite collision register */
    vera_core_line_irq_update(vera.scan_pos_y);

    vera_set_next_alarm(offset);
}

/* ------------------------------------------------------------------------- */
/* public interface                                                          */

raster_t *vera_init(void)
{
    vera.initialized = 0;

    vera.log = log_open("VERA");

    vera.raster_draw_alarm = alarm_new(maincpu_alarm_context, "VeraRasterDraw",
                                       vera_raster_draw_alarm_handler, NULL);

    vera.int_num = interrupt_cpu_status_int_new(maincpu_int_status, "VERA");
    vera.irq_active = 0;

    /* the PCM audio of the VERA is only rendered when the chip is there */
    vera_audio_set_enabled(1);

    vera_core_powerup();

    /* The canvas is handed to the UI while the raster is being realized, and
       the UI asks for it through vera_get_canvas() to find out which window
       it belongs to, so mark the VERA as present before that happens. */
    vera.initialized = 1;

    if (init_raster() < 0) {
        vera.initialized = 0;
        return NULL;
    }

    resources_touch("VERADoubleSize");

    log_message(vera.log, "VERA at $%04X-$%04X, %dx%d, %d colors.",
                VERA_REGS_BASE, VERA_REGS_LAST,
                VERA_SCREEN_WIDTH, VERA_SCREEN_HEIGHT, VERA_NUM_COLORS);

    return &vera.raster;
}

void vera_reset(void)
{
    if (vera.initialized) {
        raster_reset(&vera.raster);
    }

    vera_core_reset();

    vera.scan_pos_y = 0;
    vera.frame_count = 0;
    vera.line_accu = 0;
    vera.cycles_per_sec = 0;
    vera_update_line_timing();

    if (vera.raster_draw_alarm != NULL) {
        vera_set_next_alarm((CLOCK)0);
    }
}

void vera_powerup(void)
{
    vera_core_powerup();
    vera_reset();
}

void vera_shutdown(void)
{
    vera_audio_set_enabled(0);

    if (vera.initialized) {
        raster_shutdown(&vera.raster);
        vera.initialized = 0;
    }
    vera_core_shutdown();
}

struct video_canvas_s *vera_get_canvas(void)
{
    if (!vera.initialized) {
        return NULL;
    }

    return vera.raster.canvas;
}

void vera_screenshot(screenshot_t *screenshot)
{
    raster_screenshot(&vera.raster, screenshot);

    screenshot->chipid = "VERA";
    screenshot->video_regs = vera.reg_composer;
    screenshot->screen_ptr = vera.ram;
    screenshot->chargen_ptr = vera.ram;
    screenshot->bitmap_ptr = vera.ram;
    screenshot->bitmap_low_ptr = NULL;
    screenshot->bitmap_high_ptr = NULL;
    screenshot->color_ram_ptr = vera.palette;
}

void vera_async_refresh(struct canvas_refresh_s *refresh)
{
    raster_async_refresh(&vera.raster, refresh);
}
