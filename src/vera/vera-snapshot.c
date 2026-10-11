/*
 * vera-snapshot.c - Snapshot handling for the VERA emulation.
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

#include "log.h"
#include "snapshot.h"
#include "types.h"
#include "vera.h"
#include "vera-core.h"
#include "vera-snapshot.h"
#include "veratypes.h"

/*

   This is the format of the VERA snapshot module:

   Name         Type    Size      Description

   VRAM         BYTE    131072    video RAM
   PALETTE      BYTE    512       palette RAM
   SPRITEDATA   BYTE    1024      sprite attribute RAM
   LAYER0       BYTE    7         layer 0 registers
   LAYER1       BYTE    7         layer 1 registers
   COMPOSER     BYTE    256       display composer registers (all DCSEL banks)
   ADDR0/1      DWORD   2         data port addresses
   RDDATA       BYTE    2         data port read cache
   INC          BYTE    2         data port increments
   ADDRSEL      BYTE    1
   DCSEL        BYTE    1
   IEN/ISR      BYTE    2
   IRQLINE      WORD    1
   SCANLINE     WORD    1
   COLLISIONS   BYTE    1
   FX...        misc              FX register state
   PCMFIFO      BYTE    4096      PCM audio FIFO
   PCMSTATE     misc              PCM FIFO indices, CTRL, RATE and playback state
   PSG          misc              the 16 PSG channels and the noise shift register

 */

static const char snap_module_name[] = "VERA";
#define SNAP_MAJOR 1
#define SNAP_MINOR 0

int vera_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;
    unsigned int i;

    if (!vera.initialized) {
        /* no VERA in this session, don't write a module at all */
        return 0;
    }

    m = snapshot_module_create(s, snap_module_name, SNAP_MAJOR, SNAP_MINOR);
    if (m == NULL) {
        return -1;
    }

    if (0
        || SMW_BA(m, vera.ram, VERA_RAM_SIZE) < 0
        || SMW_BA(m, vera.palette, sizeof(vera.palette)) < 0
        || SMW_BA(m, (uint8_t *)vera.sprite_data, sizeof(vera.sprite_data)) < 0
        || SMW_BA(m, vera.reg_layer[0], sizeof(vera.reg_layer[0])) < 0
        || SMW_BA(m, vera.reg_layer[1], sizeof(vera.reg_layer[1])) < 0
        || SMW_BA(m, vera.reg_composer, sizeof(vera.reg_composer)) < 0

        || SMW_DW(m, vera.io_addr[0]) < 0
        || SMW_DW(m, vera.io_addr[1]) < 0
        || SMW_B(m, vera.io_rddata[0]) < 0
        || SMW_B(m, vera.io_rddata[1]) < 0
        || SMW_B(m, vera.io_inc[0]) < 0
        || SMW_B(m, vera.io_inc[1]) < 0
        || SMW_B(m, vera.io_addrsel) < 0
        || SMW_B(m, vera.io_dcsel) < 0

        || SMW_B(m, vera.ien) < 0
        || SMW_B(m, vera.isr) < 0
        || SMW_W(m, vera.irq_line) < 0
        || SMW_W(m, vera.scan_pos_y) < 0
        || SMW_B(m, vera.sprite_line_collisions) < 0

        || SMW_B(m, vera.fx_addr1_mode) < 0
        || SMW_DW(m, vera.fx_x_pixel_increment) < 0
        || SMW_DW(m, vera.fx_y_pixel_increment) < 0
        || SMW_DW(m, vera.fx_x_pixel_position) < 0
        || SMW_DW(m, vera.fx_y_pixel_position) < 0
        || SMW_W(m, vera.fx_poly_fill_length) < 0
        || SMW_DW(m, vera.fx_affine_tile_base) < 0
        || SMW_DW(m, vera.fx_affine_map_base) < 0
        || SMW_B(m, vera.fx_affine_map_size) < 0
        || SMW_B(m, vera.fx_4bit_mode) < 0
        || SMW_B(m, vera.fx_16bit_hop) < 0
        || SMW_B(m, vera.fx_cache_byte_cycling) < 0
        || SMW_B(m, vera.fx_cache_fill) < 0
        || SMW_B(m, vera.fx_cache_write) < 0
        || SMW_B(m, vera.fx_trans_writes) < 0
        || SMW_B(m, vera.fx_2bit_poly) < 0
        || SMW_B(m, vera.fx_2bit_poking) < 0
        || SMW_B(m, vera.fx_cache_increment_mode) < 0
        || SMW_B(m, vera.fx_cache_nibble_index) < 0
        || SMW_B(m, vera.fx_cache_byte_index) < 0
        || SMW_B(m, vera.fx_multiplier) < 0
        || SMW_B(m, vera.fx_subtract) < 0
        || SMW_B(m, vera.fx_affine_clip) < 0
        || SMW_B(m, vera.fx_16bit_hop_align) < 0
        || SMW_B(m, vera.fx_nibble_bit[0]) < 0
        || SMW_B(m, vera.fx_nibble_bit[1]) < 0
        || SMW_B(m, vera.fx_nibble_incr[0]) < 0
        || SMW_B(m, vera.fx_nibble_incr[1]) < 0
        || SMW_BA(m, vera.fx_cache, sizeof(vera.fx_cache)) < 0
        || SMW_DW(m, (uint32_t)vera.fx_mult_accumulator) < 0

        || SMW_BA(m, vera.pcm_fifo, VERA_PCM_FIFO_SIZE) < 0
        || SMW_DW(m, vera.pcm_fifo_wridx) < 0
        || SMW_DW(m, vera.pcm_fifo_rdidx) < 0
        || SMW_DW(m, vera.pcm_fifo_cnt) < 0
        || SMW_B(m, vera.pcm_ctrl) < 0
        || SMW_B(m, vera.pcm_rate) < 0
        || SMW_B(m, vera.pcm_loop) < 0
        || SMW_B(m, vera.pcm_phase) < 0
        || SMW_W(m, (uint16_t)vera.pcm_cur_l) < 0
        || SMW_W(m, (uint16_t)vera.pcm_cur_r) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    for (i = 0; i < VERA_PSG_CHANNELS; i++) {
        if (0
            || SMW_W(m, vera.psg_channels[i].freq) < 0
            || SMW_W(m, vera.psg_channels[i].volume) < 0
            || SMW_B(m, vera.psg_channels[i].left) < 0
            || SMW_B(m, vera.psg_channels[i].right) < 0
            || SMW_B(m, vera.psg_channels[i].pw) < 0
            || SMW_B(m, vera.psg_channels[i].waveform) < 0
            || SMW_W(m, vera.psg_channels[i].noiseval) < 0
            || SMW_DW(m, vera.psg_channels[i].phase) < 0) {
            snapshot_module_close(m);
            return -1;
        }
    }

    if (SMW_W(m, vera.psg_noise_state) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    return snapshot_module_close(m);
}

int vera_snapshot_read_module(snapshot_t *s)
{
    uint8_t major_version, minor_version;
    snapshot_module_t *m;
    uint32_t accumulator = 0;
    uint16_t cur_l = 0;
    uint16_t cur_r = 0;
    unsigned int i;

    m = snapshot_module_open(s, snap_module_name, &major_version, &minor_version);
    if (m == NULL) {
        /* no VERA module in this snapshot, that is not an error */
        return 0;
    }

    if (snapshot_version_is_bigger(major_version, minor_version, SNAP_MAJOR, SNAP_MINOR)) {
        log_error(vera.log, "Snapshot module version (%d.%d) newer than %d.%d.",
                  major_version, minor_version, SNAP_MAJOR, SNAP_MINOR);
        goto fail;
    }

    if (0
        || SMR_BA(m, vera.ram, VERA_RAM_SIZE) < 0
        || SMR_BA(m, vera.palette, sizeof(vera.palette)) < 0
        || SMR_BA(m, (uint8_t *)vera.sprite_data, sizeof(vera.sprite_data)) < 0
        || SMR_BA(m, vera.reg_layer[0], sizeof(vera.reg_layer[0])) < 0
        || SMR_BA(m, vera.reg_layer[1], sizeof(vera.reg_layer[1])) < 0
        || SMR_BA(m, vera.reg_composer, sizeof(vera.reg_composer)) < 0

        || SMR_DW(m, &vera.io_addr[0]) < 0
        || SMR_DW(m, &vera.io_addr[1]) < 0
        || SMR_B(m, &vera.io_rddata[0]) < 0
        || SMR_B(m, &vera.io_rddata[1]) < 0
        || SMR_B(m, &vera.io_inc[0]) < 0
        || SMR_B(m, &vera.io_inc[1]) < 0
        || SMR_B(m, &vera.io_addrsel) < 0
        || SMR_B(m, &vera.io_dcsel) < 0

        || SMR_B(m, &vera.ien) < 0
        || SMR_B(m, &vera.isr) < 0
        || SMR_W(m, &vera.irq_line) < 0
        || SMR_W(m, &vera.scan_pos_y) < 0
        || SMR_B(m, &vera.sprite_line_collisions) < 0

        || SMR_B(m, &vera.fx_addr1_mode) < 0
        || SMR_DW(m, &vera.fx_x_pixel_increment) < 0
        || SMR_DW(m, &vera.fx_y_pixel_increment) < 0
        || SMR_DW(m, &vera.fx_x_pixel_position) < 0
        || SMR_DW(m, &vera.fx_y_pixel_position) < 0
        || SMR_W(m, &vera.fx_poly_fill_length) < 0
        || SMR_DW(m, &vera.fx_affine_tile_base) < 0
        || SMR_DW(m, &vera.fx_affine_map_base) < 0
        || SMR_B(m, &vera.fx_affine_map_size) < 0
        || SMR_B(m, &vera.fx_4bit_mode) < 0
        || SMR_B(m, &vera.fx_16bit_hop) < 0
        || SMR_B(m, &vera.fx_cache_byte_cycling) < 0
        || SMR_B(m, &vera.fx_cache_fill) < 0
        || SMR_B(m, &vera.fx_cache_write) < 0
        || SMR_B(m, &vera.fx_trans_writes) < 0
        || SMR_B(m, &vera.fx_2bit_poly) < 0
        || SMR_B(m, &vera.fx_2bit_poking) < 0
        || SMR_B(m, &vera.fx_cache_increment_mode) < 0
        || SMR_B(m, &vera.fx_cache_nibble_index) < 0
        || SMR_B(m, &vera.fx_cache_byte_index) < 0
        || SMR_B(m, &vera.fx_multiplier) < 0
        || SMR_B(m, &vera.fx_subtract) < 0
        || SMR_B(m, &vera.fx_affine_clip) < 0
        || SMR_B(m, &vera.fx_16bit_hop_align) < 0
        || SMR_B(m, &vera.fx_nibble_bit[0]) < 0
        || SMR_B(m, &vera.fx_nibble_bit[1]) < 0
        || SMR_B(m, &vera.fx_nibble_incr[0]) < 0
        || SMR_B(m, &vera.fx_nibble_incr[1]) < 0
        || SMR_BA(m, vera.fx_cache, sizeof(vera.fx_cache)) < 0
        || SMR_DW(m, &accumulator) < 0

        || SMR_BA(m, vera.pcm_fifo, VERA_PCM_FIFO_SIZE) < 0
        || SMR_DW(m, &vera.pcm_fifo_wridx) < 0
        || SMR_DW(m, &vera.pcm_fifo_rdidx) < 0
        || SMR_DW(m, &vera.pcm_fifo_cnt) < 0
        || SMR_B(m, &vera.pcm_ctrl) < 0
        || SMR_B(m, &vera.pcm_rate) < 0
        || SMR_B(m, &vera.pcm_loop) < 0
        || SMR_B(m, &vera.pcm_phase) < 0
        || SMR_W(m, &cur_l) < 0
        || SMR_W(m, &cur_r) < 0) {
        goto fail;
    }

    for (i = 0; i < VERA_PSG_CHANNELS; i++) {
        if (0
            || SMR_W(m, &vera.psg_channels[i].freq) < 0
            || SMR_W(m, &vera.psg_channels[i].volume) < 0
            || SMR_B(m, &vera.psg_channels[i].left) < 0
            || SMR_B(m, &vera.psg_channels[i].right) < 0
            || SMR_B(m, &vera.psg_channels[i].pw) < 0
            || SMR_B(m, &vera.psg_channels[i].waveform) < 0
            || SMR_W(m, &vera.psg_channels[i].noiseval) < 0
            || SMR_DW(m, &vera.psg_channels[i].phase) < 0) {
            goto fail;
        }
    }

    if (SMR_W(m, &vera.psg_noise_state) < 0) {
        goto fail;
    }

    vera.fx_mult_accumulator = (int32_t)accumulator;
    vera.pcm_cur_l = (int16_t)cur_l;
    vera.pcm_cur_r = (int16_t)cur_r;

    /* keep the FIFO indices sane, they come from a file after all */
    vera.pcm_fifo_wridx %= VERA_PCM_FIFO_SIZE;
    vera.pcm_fifo_rdidx %= VERA_PCM_FIFO_SIZE;
    if (vera.pcm_fifo_cnt > VERA_PCM_FIFO_SIZE) {
        vera.pcm_fifo_cnt = VERA_PCM_FIFO_SIZE;
    }

    /* recalculate everything that is derived from the registers */
    vera_core_refresh_all();
    vera_core_irq_update();

    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    return -1;
}
