/*
 * vera-mem.h - VERA register interface at $DE00-$DE1F.
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

#ifndef VICE_VERA_MEM_H
#define VICE_VERA_MEM_H

#include "types.h"

/* register the VERA in the I/O area (at the current "VERABase" address) or
   remove it again */
void vera_mem_set_enabled(int enabled);

/* called when the "VERABase" resource changes; re-registers the I/O device
   at the new address (as returned by vera_base_address()) if it is
   currently registered, a no-op otherwise (the new address takes effect the
   next time it is enabled) */
void vera_mem_set_base(void);

/* register access, also used by the monitor */
uint8_t vera_mem_read(uint16_t addr);
uint8_t vera_mem_peek(uint16_t addr);
void vera_mem_store(uint16_t addr, uint8_t value);

#endif
