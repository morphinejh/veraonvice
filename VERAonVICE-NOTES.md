# VERA on VICE

A VERA (the video chip of the Commander X16) emulation added to the VICE C64
emulation as a **second screen**, mapped into the C64 I/O-1 area at
**$DE00 - $DE1F**. Currently using version 3.10 of the VICE emulator.

Running `x64sc` now opens two windows: the usual VIC-II screen and a second
640x480 window driven by the VERA. Everything the C64 writes to $DE00-$DE1F
goes to the VERA exactly like a write to $9F20-$9F3F on a Commander X16.

```
              +-------------------+          +--------------------------+
   C64 CPU -> | $DE00-$DE1F       | -------> | VERA: 128KiB video RAM,  |
              | 32 VERA registers |          | 2 layers, 128 sprites,   |
              +-------------------+          | 256 of 4096 colours,     |
                                             | PSG + PCM audio          |
                                             +--------------------------+
                                                        |
                                             second window, 640x480 VGA
```

The VERA emulation is derived from the
[Commander X16 emulator](https://github.com/X16Community/x16-emulator)
(`src/video.c`, 2-clause BSD, Copyright (c) 2019 Michael Steil et al., and
`src/vera_pcm.c` and `src/vera_psg.c`, Copyright (c) 2020 Frank van den Hoef)
and was adapted to the VICE raster/canvas, alarm and sound infrastructure.

## License

This project is VICE (GNU General Public License v2 or later) with an added
VERA chip emulation derived from the Commander X16 emulator (2-clause BSD
license). The BSD-derived files say so in their own headers and retain their
original copyright notices; see `veraonvice/src/vera/LICENSE.x16-emulator` for
the full BSD text and how it relates to the combined GPL work, and
`veraonvice/COPYING` for the GPL text.

## Building

This is the exact command to build VICE with the VERA support:

```sh
cd veraonvice/
./autogen.sh
./configure --enable-gtk3ui --enable-x64
make
```

That produces `src/x64sc` (and `src/x64`), both with the VERA second screen
and VERA audio built in. Run them straight from the build tree, no `make
install` needed:

```sh
./src/x64sc
```

Notes:

* `./autogen.sh` is only needed the first time, or after a `Makefile.am` or
  `configure.ac` change. On later builds `make` is enough.
* `--enable-gtk3ui` is what wires up the second window; the VERA chip itself
  is UI independent, but only the GTK3 UI has been taught about the second
  window so far.
* `--enable-x64` is optional and builds the old (non "SC") `x64` binary
  in addition to `x64sc`.
* Build requirements are the usual VICE ones (gcc, make, autoconf/automake,
  gtk+-3.0, glew, libpng, an audio backend, ...).

The VERA is *ONLY* part of `x64` and `x64sc`. The other emulators (`x128`, `xvic`,
`vsid`, ...) build and run exactly as before.

## Running

Command line options:

| Option                     | Meaning                                              |
|----------------------------|------------------------------------------------------|
| `-vera` / `+vera`          | enable / disable the VERA (resource `VERA`, default on) |
| `-exitscreenshotvera FILE` | write a PNG of the VERA screen when the emulator exits |
| `-sound`, `-soundvolume`, ... | the usual VICE sound options, the VERA audio (PSG + PCM) mixes into the normal VICE audio output |
| `-VERAdsize`, `-VERAfilter`, `-VERAaudioleak`, ... | the usual VICE per-chip video options, with the chip prefix `VERA` |

The VERA also has its own page in *Settings -> Display -> VERA* and its own CRT
controls in the second window's status bar.

Disabling the VERA (`+vera`) removes the device from $DE00-$DE1F and the second
window is not created. The resource is read when the emulator starts; toggling
it at runtime only maps/unmaps the registers, the window is created (or not) at
startup.

## Register map

The 32 VERA registers appear at $DE00-$DE1F (no mirrors, so the rest of I/O-1
stays free for other cartridges):

```
$DE00 ADDRx_L       $DE0B DC_* (DCSEL)     $DE16 L1_TILEBASE
$DE01 ADDRx_M       $DE0C DC_* (DCSEL)     $DE17 L1_HSCROLL_L
$DE02 ADDRx_H       $DE0D L0_CONFIG        $DE18 L1_HSCROLL_H
$DE03 DATA0         $DE0E L0_MAPBASE       $DE19 L1_VSCROLL_L
$DE04 DATA1         $DE0F L0_TILEBASE      $DE1A L1_VSCROLL_H
$DE05 CTRL          $DE10 L0_HSCROLL_L     $DE1B AUDIO_CTRL
$DE06 IEN           $DE11 L0_HSCROLL_H     $DE1C AUDIO_RATE
$DE07 ISR           $DE12 L0_VSCROLL_L     $DE1D AUDIO_DATA
$DE08 IRQLINE_L     $DE13 L0_VSCROLL_H     $DE1E SPI_DATA     (not emulated)
$DE09 DC_* (DCSEL)  $DE14 L1_CONFIG        $DE1F SPI_CTRL     (not emulated)
$DE0A DC_* (DCSEL)  $DE15 L1_MAPBASE
```

With `DCSEL=0` the four DC registers are `DC_VIDEO`, `DC_HSCALE`, `DC_VSCALE`
and `DC_BORDER`, with `DCSEL=1` they are `DC_HSTART`, `DC_HSTOP`, `DC_VSTART`
and `DC_VSTOP`; higher `DCSEL` values select the FX registers of VERA 47.

The VERA reports version 47.0.2, like the emulator it was taken from.

The `IRQ` output of the VERA is wired to the C64 IRQ line, so `VSYNC`, `LINE`,
sprite collision and `AFLOW` (PCM FIFO almost empty) interrupts work from a
normal C64 interrupt handler (see `demo/verairq.asm` and
`demo/verapcmirq.asm`).

## What is emulated

* 128KiB video RAM, both data ports with all address increments
* the display composer: output modes, H/V start/stop, H/V scale, border colour
* layer 0 and layer 1 in text mode (16 and 256 colours), tile mode
  (1/2/4/8 bpp, flipping, palette offsets) and bitmap mode
* 128 sprites, 4 and 8 bpp, flipping, z-depth, palette offsets, collisions
* the fully programmable 256 entry palette (12 bit RGB)
* the PCM audio channel ($DE1B-$DE1D): 4KiB FIFO, mono/stereo, 8/16 bit,
  16 volume steps, FIFO full/empty flags, loop mode and the AFLOW interrupt
* the 16 channel PSG (registers at $1F9C0-$1F9FF in the video RAM): pulse,
  sawtooth, triangle and noise waveforms, pulse width, 64 volume steps and
  left/right speaker selection. PSG and PCM are summed like the VERA does.
* VSYNC / LINE / sprite collision / AFLOW interrupts on the C64 IRQ line
* the FX registers of VERA 47 (line draw, polygon fill, affine, cache,
  multiplier)
* snapshots (VICE snapshot module `VERA`) and screenshots of the second window

## What is not emulated

* **SPI / SD card**: $DE1E-$DE1F read as 0.
* **mid line raster effects**: each scan line is rendered in one go, so a
  register change takes effect at the next line. The two line register history
  of the original implementation is kept, so the usual "change the scroll
  register during the frame" effects still land where they should.
* **NTSC composite output**: the scan out always uses VGA timing (640x480,
  59.52Hz). The NTSC/RGB output modes still select the corresponding palette
  behaviour, but the NTSC overscan dimming of the X16 emulator is not incorporated.

## Licensing

The two code bases are license compatible, in the direction of this port:

| | License |
|---|---|
| VICE | GNU GPL version 2 **or later** (`veraonvice/COPYING`) |
| Commander X16 emulator (`video.c`, `vera_pcm.c`, `vera_psg.c`) | 2-clause BSD, Copyright (c) 2019 Michael Steil, Copyright (c) 2020 Frank van den Hoef |

The 2-clause BSD License is a permissive License with no copyleft and no
clauses that conflict with the GPL, so BSD code may be incorporated into a GPL
work. The result is distributed under the GPL (version 2 or later): that is
the License of the combined binary and of this tree. The BSD terms keep
applying to the parts that came from the X16 emulator, and its copyright
notice may not be removed.

This works in one direction only: the BSD code can go into VICE, but the
combined result cannot be pushed back into the X16 emulator under BSD.

What has been done here:

* the BSD copyright notice, the list of conditions and the warranty
  disclaimer are kept in the source tree, in
  `veraonvice/src/vera/LICENSE.x16-emulator` (it is listed in `EXTRA_DIST`, so
  it travels with a source distribution),
* every file with ported code (`vera-core.c`, `vera-audio.c`, `vera-color.c`,
  `veratypes.h`) names the upstream file, the copyright holders and the
  License in its header, and points at that License file,
* the GPL header of VICE is on those files as well, because the combined work
  is GPL,
* if you distribute **binaries**, the BSD License asks you to reproduce the
  same notice and disclaimer "in the documentation and/or other materials" -
  shipping `LICENSE.x16-emulator` (or this README section) alongside the
  binary covers that,
* the VICE files that were only modified (`c64.c`, `machine.c`, the GTK3 UI,
  ...) stay plain GPL.