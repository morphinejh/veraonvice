/*
 * vera-audio.h - VERA audio: the PCM channel ($DE1B-$DE1D) and the PSG
 *                (registers at $1F9C0-$1F9FF in the video RAM).
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

#ifndef VICE_VERA_AUDIO_H
#define VICE_VERA_AUDIO_H

#include "types.h"

/* register the VERA sound chip with the VICE sound system, called once from
   the machine init */
void vera_sound_chip_init(void);

/* enable/disable the sound chip (follows the "VERA" resource) */
void vera_audio_set_enabled(int enabled);

/* reset the PCM engine and the PSG */
void vera_audio_reset(void);

/* Run the PCM engine up to the current CPU clock. Safe to call at any time,
   the engine only advances by the time that has passed since the last call. */
void vera_audio_run(void);

/* PSG register write, the PSG registers are written through the video RAM at
   $1F9C0-$1F9FF, 'reg' is the offset within that area */
void vera_audio_psg_write(uint8_t reg, uint8_t value);

/* register access ($DE1B-$DE1D) */
void vera_audio_ctrl_write(uint8_t value);
uint8_t vera_audio_ctrl_read(void);
void vera_audio_rate_write(uint8_t value);
uint8_t vera_audio_rate_read(void);
void vera_audio_fifo_write(uint8_t value);

/* state of the AFLOW flag (ISR bit 3): is the PCM FIFO almost empty? */
int vera_audio_fifo_almost_empty(void);

#endif
