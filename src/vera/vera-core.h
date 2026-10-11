/*
 * vera-core.h - VERA chip logic (registers, video RAM and line rendering).
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

#ifndef VICE_VERA_CORE_H
#define VICE_VERA_CORE_H

#include "types.h"
#include "veratypes.h"

/* Line buffer holding the palette indices of the most recently rendered
   scan line. */
extern uint8_t vera_core_line_buffer[VERA_SCREEN_WIDTH];

/* register access ($DE00-$DE1F) */
uint8_t vera_core_reg_read(uint8_t reg);
uint8_t vera_core_reg_peek(uint8_t reg);
void vera_core_reg_store(uint8_t reg, uint8_t value);

/* video RAM access */
uint8_t vera_core_vram_read(uint32_t address);
void vera_core_vram_write(uint32_t address, uint8_t value);

/* chip state */
void vera_core_reset(void);
void vera_core_powerup(void);
void vera_core_shutdown(void);

/* recalculate all derived state, used after loading a snapshot */
void vera_core_refresh_all(void);

/* Render one scan line into vera_core_line_buffer. Returns 1 if the line
   buffer holds a freshly rendered line, 0 if rendering was skipped. */
int vera_core_render_line(uint16_t y);

/* update ISR and sprite collisions at the start of scan line 'y' */
void vera_core_line_irq_update(uint16_t y);

/* raise/lower the VERA IRQ line to the CPU */
void vera_core_irq_update(void);

/* palette handling, the palette is written through the video RAM */
int vera_core_palette_is_dirty(void);
void vera_core_palette_clear_dirty(void);
void vera_core_palette_set_dirty(void);

#endif
