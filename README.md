# FakeCamera

taiHEN plugin that fakes the PS Vita camera on **PlayStation TV** (or on a PS Vita whose camera is broken). Titles which crash or lock when they try to use the camera get the answers they expect instead, and you can even choose a BMP image to be shown as the camera picture.

Most of those titles are blocked by Sony on PS TV: unlock them first with an application like [AntiBlackList](http://vitadb.rinnegatamante.it/#/info/11) by Rinnegatamante.

Version 1.3 is a single plugin with no special dependency (see [Upgrading from 1.2](#upgrading-from-12) if you used an older version).


## Installation

1. Copy `fakecamera.suprx` to `ux0:tai/` (or `ur0:tai/` if that is where your plugins live).
2. In `ux0:tai/config.txt` (or `ur0:tai/config.txt`), add the plugin under the title which needs it:

   ```
   *PCSF00007
   ux0:tai/fakecamera.suprx
   ```

   Replace `PCSF00007` by the title identifier, or use `*ALL` to enable it for every title.
3. Launch the title (reboot if it was already running).

The plugin only acts when the real camera cannot be opened, so it is harmless on a PS Vita with a working camera.


## Camera image (optional)

Without any image, the camera picture stays black. To show a picture, place a BMP file in `ux0:data/FakeCamera/` (create the directory). For each camera opened by a title, the first existing file in this list is used:

 1. `ux0:data/FakeCamera/TITLEID00_Front.bmp` or `ux0:data/FakeCamera/TITLEID00_Back.bmp` (depending on the camera used)
 2. `ux0:data/FakeCamera/TITLEID00.bmp`
 3. `ux0:data/FakeCamera/ALL_Front.bmp` or `ux0:data/FakeCamera/ALL_Back.bmp`
 4. `ux0:data/FakeCamera/ALL.bmp`

Image requirements:

 * Uncompressed BMP, 16, 24 or 32 bits per pixel, up to 2048x2048 (both bottom-up and top-down files are accepted). Compressed or paletted BMP files are rejected.
 * The image is converted into the exact format the title asks for (ARGB, ABGR, YUV422 or YUV420), so any size works. An image bigger than the camera resolution is cropped (and can be scrolled by tilting, see below); a smaller one is centered with black borders.
 * A few titles only stand small pictures: WipEout 2048 for instance works with a 64x64 image.

### Titles which cannot read `ux0:`

Some titles run in a sandbox which hides `ux0:` from them, so the image cannot be loaded (Frobisher Says is one of them, the picture stays black). For those, install **ioPlus**, a kernel plugin which lets every process use the ordinary file functions on `ux0:`:

 * Get `ioplus.skprx` from https://github.com/delon5/ioplus (or the original ioPlus 0.1 by dots-tb).
 * Copy it to `ur0:tai/` and add it under `*KERNEL` in your `config.txt`, above other plugins:

   ```
   *KERNEL
   ur0:tai/ioplus.skprx
   ```

Other plugins (VitaGrafix, iTLS-Enso...) already need it, so you may have it installed already.


## Tilt scrolling

When the image is bigger than the camera resolution, the visible part follows the tilt of the device: lean to the right to see the right part of the picture, lean forward to see the bottom, and so on. This uses the system motion sensors through the standard `SceMotion` library, so:

 * on a **PS Vita**, the built-in sensors are used, nothing to install;
 * on a **PS TV**, the sensors of a DualShock 3 or 4 are used as soon as a motion emulator feeds them to the system. [PSVshell+](https://github.com/delon5/PSV-Shell-Plus) does it once its "Bt Motion" option is enabled in the profile of the title; [ds34motion](https://github.com/MERLev/ds34motion) and the older [DSMotion](https://github.com/OperationNT414C/DSMotion) work too. Enable the emulator before starting the title;
 * without any motion source, the picture simply stays centered.

Motion is started only when an image is actually loaded; titles which do not get a picture are not affected.


## Ready-made templates

The [`templates/`](templates/) directory is a ready-to-copy `ux0:data/FakeCamera/`: pictures drawn for what the camera titles expect (a face for face detection, the six AR Play markers, a room to look around, a colour pattern for stickers and photos...) and a `config.txt` which maps about a hundred title IDs (all regions) of the titles known to use the camera to the right picture. [`templates/README.md`](templates/README.md) explains, title by title, what the camera is used for and what else must be loaded on PS TV. `tools/make_template.py` converts your own photos.


## Configuration (optional)

A `ux0:data/FakeCamera/config.txt` file can tune the plugin. Every line is a `key=value` pair, `#` starts a comment, and the defaults are:

```
# Picture for both cameras (image), or for one of them (front, back, which
# override image): a file in ux0:data/FakeCamera or a full path, tried before
# the TITLEID/ALL names
image=
front=
back=
# Tilt scrolling of a large image (on/off)
motion=on
# Invert the scrolling direction (on/off), in case it feels reversed with your motion emulator
invert_x=off
invert_y=off
# Tilt sensitivity in percent: at 100, about 57 degrees of tilt reach the edge of the image
sensitivity=100
# Write what happens in ux0:data/FakeCamera/log.txt (on/off)
log=off
```

A `*TITLEID` line starts a section which only applies to that title, on top of the global values (the lines before the first section, or after a `*ALL` line):

```
front=face.bmp
back=pattern.bmp

*PCSF00043
back=objects.bmp
sensitivity=150
```

Both cameras, all regions of a title and several titles can thus share one picture, whatever its name; the `TITLEID_Front.bmp` / `TITLEID.bmp` / `ALL.bmp` names of the [Camera image](#camera-image-optional) section remain the fallback.


## Troubleshooting

 * **The title still crashes**: check that the plugin line is under the right title identifier (or `*ALL`) and that no other camera plugin is loaded for it.
 * **The picture stays black**: the BMP file is missing, misnamed (check the `image=`/`front=`/`back=` lines of the title's section) or unsupported, or the title cannot read `ux0:` (see [ioPlus](#titles-which-cannot-read-ux0)). Set `log=on` in the configuration file and look at `ux0:data/FakeCamera/log.txt`: it tells which file was tried and why it was refused. Writing the log needs the same access as reading the image, so it is empty for sandboxed titles without ioPlus.
 * **The image does not scroll**: no motion source is available (see [Tilt scrolling](#tilt-scrolling)), or the image is not bigger than the camera resolution. The log tells whether `SceMotion` was found and started.
 * **The image scrolls in the wrong direction**: set `invert_x=on` and/or `invert_y=on`.

Please report the titles you test, with the log, so that the compatibility lists below can grow.


## Upgrading from 1.2

Version 1.2 came as three plugins: `fakecamera.suprx`, `fakecamerabmp.suprx` (image support, needed `dsmotion.skprx`) and `fakecamerakbmp.suprx` (also needed `kuio.skprx`). All three are replaced by the single `fakecamera.suprx`:

 * replace any `fakecamerabmp.suprx` or `fakecamerakbmp.suprx` line in `config.txt` by `fakecamera.suprx`;
 * `dsmotion.skprx` and `kuio.skprx` are no longer needed by FakeCamera. If nothing else uses them, remove their lines from the `*KERNEL` section (PSVshell+ users: it replaces DSMotion anyway);
 * if `fakecamerakbmp.suprx` was needed for a title, install [ioPlus](#titles-which-cannot-read-ux0) instead of `kuio.skprx`;
 * images and their names in `ux0:data/FakeCamera/` are unchanged.


## Compatibility

 * PCSF00007 - WipEout 2048 - The game won't crash on a multiplayer session start! (due to the useless picture feature)
 * PCSF00214 - Tearaway - It won't crash but it will be locked on some asked interactions, like shaking the PS Vita (use a motion emulator to by-pass this problem)


## Image compatibility

 * PCM300001 - Pro Camera Vita - Works fine
 * VITASHELL - Vita Shell - Works fine in QR scan feature
 * NPXS10007 - Welcome Park - Works fine in "Hello Face" and "Snap + Slide" (ARGB format test case) mini-games
 * PCSB00031 - Virtua Tennis 4 - Works fine in "CAM VT" mode (packed YUV422 format test case)
 * PCSF00214 - Tearaway - Works fine
 * PCSF00007 - WipEout 2048 - Works fine with low resolution images (tested with 64x64)
 * PCSF00043 - Frobisher Says - Needs ioPlus (planar YUV420 format test case), and loading times are highly slowed down

Those results were obtained with version 1.2; the 1.3 image loading and scrolling changes have not yet been confirmed on hardware, reports are welcome.


## Building

With [VitaSDK](https://vitasdk.org/) installed and `VITASDK` set:

```sh
cmake -S . -B build
cmake --build build
```

The plugin is `build/fakecamera.suprx`. Every push is also built by GitHub Actions (the `fakecamera` artifact of the workflow run), and a `release/` copy is committed with its checksum.


## Credits

 * **OperationNT414C** for FakeCamera and DSMotion
 * **Rinnegatamante** for AntiBlackList and kuio
 * **dots-tb** for ioPlus
 * **xerpi** for libvita2d, whose BMP reader inspired this one
 * **Electry** and **MERLev** for PSVshell and ds34motion, which PSVshell+ builds on
 * **VitaSDK** for the toolchain and the NID database
