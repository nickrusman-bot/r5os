# VirtualBox 7.2.22 Haiku guest additions

The product image does not contain these binaries. A throwaway VirtualBox disk has run the display, a host resize, and one shared-folder copy. Build notes for that disk are in `docs/VBOX-GUEST.md`.

Oracle VirtualBox 7.2.22 is **GPL-3.0-only**. Some IPRT files are GPL-3.0-only OR CDDL-1.0; this tree takes the GPL-3.0-only choice. The Haiku copyright lines in the additions stay as they are. Do not paste this code into an MIT file. An image that contains `vboxguest`, `vboxdev`, `vboxsf`, `vboxvideo`, the accelerant, `VBoxService`, `VBoxTray`, or `VBoxControl` also ships `LICENSES/GPL-3.0-or-later.txt` and the 7.2.22 source plus the patch in this directory. The “or later” name on that license file is for new R5OS code. It is not added to the VirtualBox sources. See `docs/LICENSING.md`.

The Oracle tarball is not stored in git. The patch is the R5OS build fix against that exact tarball.

## What the patch changes

It lets a Linux host cross-compile the Haiku additions with the R5OS AMD64 compiler, and it carries the fixes that the throwaway disk actually ran:

- ring-0 spinlock, event, and mutex semaphore mistakes that panicked or hung the guest
- `vboxsf` connects on the first mount, not while the kernel is still scanning file systems
- the video driver publishes `get_accelerant_hook`, and `VBoxTray` starts the display service with `B_BACKGROUND_APP`
- Haiku paths used by the link are `R5OS_HAIKU_*` variables, so the patch has no machine-specific directory

Drag and drop is not in this patch. GitHub issue 89.

## Build

Host: Linux (WSL is fine) with the tools VirtualBox `configure` needs (`gcc`, `g++`, `make`, `libxml2-dev`, `libxslt1-dev`, `yasm`, `iasl` or the copies `configure` builds). The Haiku tree must already have been jammed for `x86_64`, so `generated.x86_64/cross-tools-x86_64` exists.

```sh
export R5OS_HAIKU_TREE=/path/to/haiku
export R5OS_VBOX_TREE=/path/to/VirtualBox-7.2.22

tar -xjf VirtualBox-7.2.22.tar.bz2
# https://download.virtualbox.org/virtualbox/7.2.22/VirtualBox-7.2.22.tar.bz2

patch -d "$R5OS_VBOX_TREE" -p1 < distro/vbox-guest/virtualbox-7.2.22-r5os.patch

cd "$R5OS_VBOX_TREE"
./configure --only-additions --nofatal --disable-python --disable-java --build-headless --disable-xpcom

python3 /path/to/r5os/scripts/vbox-additions-setup.py
/path/to/r5os/scripts/build-vbox-additions.sh
```

`configure` writes `env.sh` with the directory it was run in. Run it in `$R5OS_VBOX_TREE`. The build script sources that file.

Outputs, under `$R5OS_VBOX_TREE/out/haiku.amd64/release/bin/additions/`:

| File | Guest path on a throwaway disk |
|---|---|
| `vboxguest` | `/boot/system/non-packaged/add-ons/kernel/generic/vboxguest` |
| `vboxsf` | `/boot/system/non-packaged/add-ons/kernel/file_systems/vboxsf` |
| `vboxvideo` | `/boot/system/non-packaged/add-ons/kernel/drivers/dev/graphics/vboxvideo` |
| `vboxvideo.accelerant` | `/boot/system/non-packaged/add-ons/accelerants/vboxvideo.accelerant` |
| `VBoxTray`, `VBoxService`, `VBoxControl` | `/boot/system/non-packaged/bin/` |

`drivers/dev/misc/vboxguest` is a small IDC stub. Do not replace it with the large `vboxguest` module.

`kmk` does not attach `VBoxTray`'s resources. The build script runs Haiku `xres -o` so Deskbar sees `B_BACKGROUND_APP`.

These files are not copied into `generated/r5-os.vdi` by the product script. QEMU and a real machine have no VirtualBox guest device; the same jobs then fail the open and do not mount a share.
