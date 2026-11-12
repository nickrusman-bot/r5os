# R5 Operating System

R5 is a desktop operating system built on [Haiku](https://www.haiku-os.org/). It keeps what makes Haiku pleasant: one fast, coherent system with a clean desktop. On top of that it adds what a daily computer needs: accounts with sign-in, modern networking, better sound, and support for current laptops.

The name is a nod to BeOS R5, as history. It is not a product claim.

> R5 Operating System is **not** the official Haiku distribution, and it is **not** BeOS. It is an independent project and is not associated with the Haiku Project. See [`distro/DISCLAIMER.txt`](distro/DISCLAIMER.txt).

**Status:** in development. The current tree is **R5 Operating System 2026.1-dev, build 10059**. There is no public release yet.

---

## What works today

### Desktop
- R5 Operating System branding throughout: Deskbar, installer, boot splash, About.
- Dark mode, plus fixes to the Deskbar, Tracker and the desktop icons.
- **Settings** is one window. Home shows a card per category; the list on the left folds open to the pages of one category, and Search jumps to any page. Pages include Information (your computer at a glance), Display (your screen and workspaces), Appearance, Desktop and Deskbar, Wallpaper, Notifications and Virtual memory. A change applies at once. Revert goes back to how the page was when you opened it, and Defaults to the factory setup. The separate preference programs are still installed.
- Notifications, with a choice of where they appear on screen.
- **Device Manager** shows every device in your computer, warns about a device without a driver, and can turn a driver off and on again.
- Touchpads: a precision touchpad is one device, with clicks, scrolling and gestures. TrackPoints work too.

### Accounts
- A **sign-in screen** before the desktop starts. **Log out** and **Lock screen** are in the leaf menu.
- **Users & Accounts** to add and remove people. Every person gets a private home folder.
- The administrator is a normal account, not root. Machine-wide settings (network, display, time, users, printers, repositories) are locked. A standard account can still view them, and unlocks them with an administrator's password.

### Network
- Wi-Fi: join a network, and it is remembered.
- IPv6 on a home network (SLAAC).
- Printing to AirPrint/IPP printers.
- **Network shares:** mount a Windows (SMB) share from Tracker, by IP address. The password stays in the keystore.
- Sound to AirPlay 1 speakers. AirPlay 2 is not supported.

### Sound
- Clearer resampling, and a mixer that recovers from late sound.
- A latency choice: Normal, Low latency, or Pro audio.
- USB sound devices can be set to stereo, 4.0, 5.1 or 7.1. HDMI outputs are labeled as HDMI.
- Virtual surround for headphones.
- A USB sound card that stops or changes its settings comes back on its own, and can be unplugged safely.
- Finer MIDI velocity and controller values. Existing MIDI programs keep working.
- MIDI 2.0 support, including the clock, start and stop.
- **MIDI Monitor** shows every message a MIDI keyboard or controller sends.

### Graphics
**Not just any port.** We've done it the Haiku and BeOS way: a native layer between Vulkan and R5.

- Native Vulkan support \*
- Vulkan programs draw in an ordinary window, on the GPU on AMD Ryzen laptops \*
- Native OpenGL, running on top of Vulkan \*
- Your existing Haiku apps keep working, unchanged
- Software rendering as a fallback

\* In active development. Hardware support is still limited. Details: [`docs/GRAPHICS.md`](docs/GRAPHICS.md).

### Processor
- On AMD Ryzen, R5 knows which threads share a core, so busy programs spread over the real cores. On processors with fast and efficient cores, busy programs go to the fast cores first. \*\*
- An idle computer can use the processor's deeper sleep states, also on AMD. \*\*

\*\* New in build 10059. Not yet checked on the reference laptop.

### Virtual machines
- VirtualBox Guest Additions 7.2.22 are included: the guest modules, `VBoxService`, `VBoxTray` and `VBoxControl`.

The full list, build by build, is in About R5 → What's new ([`distro/WHATSNEW`](distro/WHATSNEW)). The technical details and the known limits are in [`CHANGELOG.md`](CHANGELOG.md).

## Not there yet

- No GPU acceleration for the desktop. The desktop is still drawn by the CPU. Copying it to the screen through the GPU works on the reference laptop, as an option for testing only.
- Wi-Fi on the reference laptop's own MediaTek card is in progress.
- USB webcams: the camera is found and its stream starts, but a picture is not confirmed yet.
- One graphical session at a time. There is no fast user switching and no guest account.
- The bundled browser (WebPositive) is weak for today's web.
- Sleep, backlight and HiDPI on the reference laptop are still open.

Where the project is heading: [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Hardware

R5 Operating System is **64-bit only**.

| Architecture | Status |
|---|---|
| AMD64 (`x86_64`) | Primary target. The reference machine is a laptop with a Ryzen 5 5625U. Also runs in VirtualBox and QEMU. |
| ARM64 (`aarch64`) | Planned. QEMU first, then real hardware. |
| RISC-V 64 | Planned, after AMD64 and ARM64. |

## Trying it

R5 Operating System is built from source; there are no downloads yet. After a build (see below) you can:

- **Run it in VirtualBox.** Use [`scripts/run-vbox-r5.ps1`](scripts/run-vbox-r5.ps1) on Windows, and see [`docs/vbox.md`](docs/vbox.md). With a bridged network adapter this is the way to test real home-network features such as IPv6 and printer discovery.
- **Run it in QEMU.** Use [`scripts/run-qemu-r5.sh`](scripts/run-qemu-r5.sh). QEMU's default user-mode network is not a real home network.
- **Boot it from USB.** Build the `r5-anyboot` image and write it to a USB stick. Check the target disk twice before writing.

## Building

Haiku does not build natively on Windows. Use Linux, or WSL with Ubuntu on Windows. Keep the checkout on the Linux file system (for example `~/Projects/R5OS`), not under `/mnt/c`: the Windows file system is case-insensitive and slow for this.

Install the build tools:

```bash
sudo apt update
sudo apt install git nasm autoconf automake texinfo flex bison gawk \
  build-essential unzip wget zip xorriso mtools python3 \
  zlib1g-dev libzstd-dev
```

The full list of what Haiku needs is in [`haiku/ReadMe.Compiling.md`](haiku/ReadMe.Compiling.md).

Get the sources and Haiku's build tools, and install `jam`:

```bash
git clone https://github.com/r5-operating-system/r5os.git ~/Projects/R5OS
cd ~/Projects/R5OS
git clone https://review.haiku-os.org/buildtools
make -C buildtools/jam
(cd buildtools/jam && ./jam0 -sBINDIR="$HOME/bin" install)
export PATH="$HOME/bin:$PATH"
```

Build an image:

```bash
./scripts/build-image.sh r5-raw
```

`build-image.sh` copies the R5 Operating System changes from `distro/` into the Haiku tree, configures the build on the first run, and runs `jam`. The result lands in `haiku/generated.x86_64/`.

| Profile | Use it for |
|---|---|
| `r5-raw` | A disk image for QEMU, or for converting to a VirtualBox disk |
| `r5-anyboot` | A bootable ISO or USB stick |
| `r5-vmware` | VMware |

## How the repository is organized

| Path | What it holds |
|---|---|
| `distro/` | Everything R5 Operating System adds or changes: version, What's new, artwork, apps, drivers and system components. These files are copied into the Haiku tree at build time. |
| `haiku/` | The Haiku source tree that R5 Operating System builds on. |
| `docs/r5book/` | The R5Book, a developer guide to the R5 additions (LibR5, the Appearance Kit, MIDI 2.0, Vulkan), in the layout of the Be Book. |
| `docs/` | Design notes and contracts: [roadmap](docs/ROADMAP.md), [graphics](docs/GRAPHICS.md), [multi-user](docs/MULTIUSER.md), [Settings](docs/SETTINGS.md), [icons](docs/ICONS.md), [shared libraries](docs/LIBR5.md), [licensing](docs/LICENSING.md), and more. |
| `scripts/` | Build, image, virtual-machine and tracker scripts. |
| `tracker/` | Which open Haiku tickets R5 Operating System has fixed. The overview is [`tracker/STATUS.md`](tracker/STATUS.md). |
| `tools/r5-befs/` | A small read-only BeFS reader for Windows. |
| `LICENSES/` | The unmodified license texts. |

## Relationship with Haiku

Haiku is the foundation, and its source is where R5 gets many of its fixes. R5 does not send changes back. Haiku does not accept AI-generated or AI-modified contributions, and R5 Operating System work is AI-assisted. So:

- Do not open pull requests against Haiku. Do not push to Haiku's servers.
- Haiku's repositories are only fetched from.
- Open Haiku bugs are tracked in [`tracker/`](tracker/) and as GitHub issues with the `haiku` label. The Haiku ticket number is only used as an id.

## Licensing

R5 Operating System is **multi-license**:

- Files that come from Haiku keep their original license and copyright, mostly MIT.
- R5's own code is MIT, so other Haiku-based systems can use it (since 12 Nov 2026). That includes the libraries and headers programs link: the R5 libraries (`libr5icons`, `libr5ui`, `libr5wifi`), the MIDI kit, the R5 GPU interface header and the R5 DRM library.
- The R5 artwork (logo, wave pebble, splash) stays GPL-3.0-or-later for now.
- Third-party components keep their own licenses. They are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). Examples are the VirtualBox Guest Additions (GPL-3.0-only), the AMD firmware and Mesa.

The full policy is in [`docs/LICENSING.md`](docs/LICENSING.md). About R5 shows the licenses and credits on the system itself.

## Contributing

AI-assisted work is welcome here. It is held to the same bar as any other change: it has to build, boot and be tested before it counts as done. Before you start, read [`AGENTS.md`](AGENTS.md). It covers the development loop, the fixed parts of the system, versioning, and how to record changes.

- Pull requests target `main`.
- User-visible changes get a What's new entry in plain language. Technical details go in `CHANGELOG.md`.
- Use the name **R5 Operating System**, short **R5** (see "Name" in `AGENTS.md`). Do not use Haiku or BeOS names or logos on the image.

## Versions

Releases will be called **R5 Operating System 2026.1**, **2026.2**, and so on; a new year starts again at `.1`. Development trees carry `-dev`, and About R5 shows the version together with an increasing build number. The sources for those are [`distro/VERSION`](distro/VERSION) and [`distro/BUILD`](distro/BUILD).
