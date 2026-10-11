/*
 * vera-cmdline-options.c - Command line options for the VERA emulation.
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

#include "cmdline.h"
#include "lib.h"
#include "resources.h"
#include "raster-cmdline-options.h"
#include "util.h"
#include "vera.h"
#include "vera-cmdline-options.h"
#include "veratypes.h"

static const cmdline_option_t cmdline_options[] =
{
    { "-vera", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "VERA", (resource_value_t)1,
      NULL, "Enable the VERA second screen at $DE00-$DE1F" },
    { "+vera", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "VERA", (resource_value_t)0,
      NULL, "Disable the VERA second screen" },
    CMDLINE_LIST_END
};

/* the description is generated at init time since it lists the valid
   addresses, see vera_cmdline_options_init() below */
static char *vera_base_address_list = NULL;

static cmdline_option_t base_cmdline_options[] =
{
    { "-verabase", SET_RESOURCE, CMDLINE_ATTRIB_NEED_ARGS,
      NULL, NULL, "VERABase", NULL,
      "<Base address>", NULL },
    CMDLINE_LIST_END
};

int vera_cmdline_options_init(void)
{
    char *addr_list;

    if (raster_cmdline_options_chip_init("VERA", vera.video_chip_cap) < 0) {
        return -1;
    }

    if (cmdline_register_options(cmdline_options) < 0) {
        return -1;
    }

    /* $DE00-$DEE0 in steps of $20, the last slot that still leaves room for
       all 32 registers before $DF00 */
    addr_list = util_gen_hex_address_list(0xde00, 0xdf00, 0x20);
    vera_base_address_list = util_concat("I/O-1 base address of the VERA second screen. (",
                                         addr_list, ")", NULL);
    lib_free(addr_list);

    base_cmdline_options[0].description = vera_base_address_list;

    /* vera_base_address_list is kept for the lifetime of the process (the
       cmdline_option_t above points into it), it is not freed here */
    return cmdline_register_options(base_cmdline_options);
}
