/*
 * vera-mem.c - VERA register interface at $DE00-$DE1F.
 *
 * Written by
 *  Jason Hill
 *
 * The 32 VERA registers ($9F20-$9F3F on the Commander X16) are mapped into
 * the C64 I/O-1 area at $DE00-$DE1F:
 *
 *   $DE00 ADDRx_L       $DE0B DC_* (DCSEL)     $DE16 L1_TILEBASE
 *   $DE01 ADDRx_M       $DE0C DC_* (DCSEL)     $DE17 L1_HSCROLL_L
 *   $DE02 ADDRx_H       $DE0D L0_CONFIG        $DE18 L1_HSCROLL_H
 *   $DE03 DATA0         $DE0E L0_MAPBASE       $DE19 L1_VSCROLL_L
 *   $DE04 DATA1         $DE0F L0_TILEBASE      $DE1A L1_VSCROLL_H
 *   $DE05 CTRL          $DE10 L0_HSCROLL_L     $DE1B AUDIO_CTRL   (not emulated)
 *   $DE06 IEN           $DE11 L0_HSCROLL_H     $DE1C AUDIO_RATE   (not emulated)
 *   $DE07 ISR           $DE12 L0_VSCROLL_L     $DE1D AUDIO_DATA   (not emulated)
 *   $DE08 IRQLINE_L     $DE13 L0_VSCROLL_H     $DE1E SPI_DATA     (not emulated)
 *   $DE09 DC_* (DCSEL)  $DE14 L1_CONFIG        $DE1F SPI_CTRL     (not emulated)
 *   $DE0A DC_* (DCSEL)  $DE15 L1_MAPBASE
 *
 * With DCSEL=0 the four DC registers are DC_VIDEO, DC_HSCALE, DC_VSCALE and
 * DC_BORDER, with DCSEL=1 they are DC_HSTART, DC_HSTOP, DC_VSTART and
 * DC_VSTOP, higher DCSEL values select the FX registers.
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

#include "cartio.h"
#include "monitor.h"
#include "types.h"
#include "vera.h"
#include "vera-core.h"
#include "vera-mem.h"
#include "veratypes.h"

static int vera_dump(void);

/* start_address/end_address are overwritten with the current "VERABase"
   address (see vera-resources.c) every time the device is registered, by
   vera_mem_set_enabled() below. The values here are only the factory
   default, used before the first registration. */
static io_source_t vera_device = {
    "VERA",                                 /* name of the device */
    IO_DETACH_RESOURCE,                     /* use a resource to detach the device */
    "VERA",                                 /* resource to set to '0' */
    VERA_REGS_BASE, VERA_REGS_LAST, 0x1f,   /* $DE00-$DE1F by default, no mirrors */
    1,                                      /* read is always valid */
    vera_mem_store,                         /* store function */
    vera_mem_store,                         /* poke function */
    vera_mem_read,                          /* read function */
    vera_mem_peek,                          /* peek function */
    vera_dump,                              /* device state information dump function */
    IO_CART_ID_NONE,                        /* not a cartridge */
    IO_PRIO_NORMAL,                         /* normal priority */
    0,                                      /* insertion order, filled in by the registration */
    IO_MIRROR_NONE                          /* no mirroring */
};

static io_source_list_t *vera_list_item = NULL;

uint8_t vera_mem_read(uint16_t addr)
{
    return vera_core_reg_read((uint8_t)(addr & 0x1f));
}

uint8_t vera_mem_peek(uint16_t addr)
{
    return vera_core_reg_peek((uint8_t)(addr & 0x1f));
}

void vera_mem_store(uint16_t addr, uint8_t value)
{
    vera_core_reg_store((uint8_t)(addr & 0x1f), value);
}

static int vera_dump(void)
{
    unsigned int i;

    mon_out("VERA registers ($%04X-$%04X):\n", vera_device.start_address, vera_device.end_address);
    mon_out("ADDR0: $%05X (inc %u)  ADDR1: $%05X (inc %u)  ADDRSEL: %u  DCSEL: %u\n",
            vera.io_addr[0] & 0x1ffff, (unsigned int)vera.io_inc[0],
            vera.io_addr[1] & 0x1ffff, (unsigned int)vera.io_inc[1],
            (unsigned int)vera.io_addrsel, (unsigned int)vera.io_dcsel);
    mon_out("IEN: $%02X  ISR: $%02X  IRQLINE: %u  scan line: %u\n",
            (unsigned int)vera.ien, (unsigned int)vera.isr,
            (unsigned int)vera.irq_line, (unsigned int)vera.scan_pos_y);
    mon_out("DC_VIDEO: $%02X  HSCALE: %u  VSCALE: %u  BORDER: $%02X\n",
            (unsigned int)vera.reg_composer[0], (unsigned int)vera.reg_composer[1],
            (unsigned int)vera.reg_composer[2], (unsigned int)vera.reg_composer[3]);
    mon_out("DC_HSTART: %u  DC_HSTOP: %u  DC_VSTART: %u  DC_VSTOP: %u\n",
            (unsigned int)vera.reg_composer[4] << 2, (unsigned int)vera.reg_composer[5] << 2,
            (unsigned int)vera.reg_composer[6] << 1, (unsigned int)vera.reg_composer[7] << 1);

    for (i = 0; i < VERA_NUM_LAYERS; i++) {
        mon_out("L%u: CONFIG $%02X  MAPBASE $%02X  TILEBASE $%02X  HSCROLL %u  VSCROLL %u\n",
                i,
                (unsigned int)vera.reg_layer[i][0], (unsigned int)vera.reg_layer[i][1],
                (unsigned int)vera.reg_layer[i][2],
                (unsigned int)(vera.reg_layer[i][3] | ((vera.reg_layer[i][4] & 0xf) << 8)),
                (unsigned int)(vera.reg_layer[i][5] | ((vera.reg_layer[i][6] & 0xf) << 8)));
    }

    return 0;
}

void vera_mem_set_enabled(int enabled)
{
    if (enabled) {
        if (vera_list_item == NULL) {
            vera_device.start_address = (uint16_t)vera_base_address();
            vera_device.end_address = (uint16_t)(vera_device.start_address + 0x1f);
            vera_list_item = io_source_register(&vera_device);
        }
    } else {
        if (vera_list_item != NULL) {
            io_source_unregister(vera_list_item);
            vera_list_item = NULL;
        }
    }
}

void vera_mem_set_base(void)
{
    /* if the VERA isn't currently registered there is nothing to move; the
       new address is picked up from vera_base_address() the next time
       vera_mem_set_enabled(1) runs */
    if (vera_list_item != NULL) {
        vera_mem_set_enabled(0);
        vera_mem_set_enabled(1);
    }
}
