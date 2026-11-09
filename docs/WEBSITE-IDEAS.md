# R5 website: brainstorm (8 Nov 2026)

Ideas, not decisions. The aim: catchy and lively, not dry like Haiku's site, and every claim true.

## Taglines (chosen)

- **"Same soul. Fresh pixels."** (the main line)
- **"Yellow tabs. Modern everything."**

## Hero candidates

- "Remember BeOS's speed? Watch what it does on modern hardware."
- "The speed you remember. The hardware you own."
- "BeOS-fast. 2026-ready."
- "Old soul. New silicon."
- "Instant like 1999. Powerful like now."
- "Remember BeOS's speed? Now with a GUI from this decade."
- "The classic feel, without the classic look."
- "BeOS-fast. 2026-pretty."
- "Nostalgia, minus the 1999 pixels."
- "BeOS soul. Modern heart."
- "Yellow tabs. Frosted glass. Zero lag." (only once frosted glass exists, after M8)

## Vulkan and GPU lines

- "R5 speaks Vulkan." / "Now speaking Vulkan. 🖖"
- "Our own driver. Real Vulkan. No Linux inside."
- "Native GPU. Native R5."
- "Small driver, real GPU." / "Small driver. Big teapot."
- "Live long and render."
- "Fascinating. The teapot is on the GPU."
- "Beam me up, RADV."
- "Highly illogical to use the CPU."
- "R5: where no Haiku fork has rendered before."
- "BeOS dreamed it. R5 renders it."
- "The teapot is spinning on silicon now."
- Dutch: "R5 rijdt nu op de GPU."

Haiku-style (5-7-5):

> *Silicon awakes —*
> *a teapot spins on the GPU,*
> *R5 speaks Vulkan.*

> *Ten sticks, one zero —*
> *the range was empty all along.*
> *Now the teapot spins.*

## Hero, the first screen

- The main line large, with "Now speaking Vulkan. 🖖" beneath it.
- A short loop of the spinning teapot on the 5625U.
- One button: "Try R5", or "Follow the build" while there is no download.
- The wallpaper blue, with the Wave Pebble large and faint behind it.

## Tone

- R5 talks like a friend who has just built something great. Haiku's site reads like a manual.
- Short headings with a wink. Technical detail one click deeper.
- Real screen recordings instead of dry lists.
- Some humour, but the facts are always right.

## Sections

1. **"Feels instant"**: desktop, Tracker and Deskbar, with a GIF of windows responding at once.
2. **"Speaks Vulkan"**: a native GPU driver of R5's own, with RADV. Labelled experimental.
3. **"Yours, privately"**: accounts, the sign-in screen, a private home folder.
4. **"Plays nice"**: Wi-Fi, AirPrint, AirPlay, SMB shares, MIDI 2.0.
5. **"Built in the open"**: the roadmap as a visual timeline (M4 ✓, M5 →, …), plus a short blog.
6. **"The story"**: a featured post, "Ten sticks, one zero" (or "29 sticks to one register"), the detective story of the GPU bug.

## Evidence instead of promises

A strip of tested devices, as badges, kept up to date from the tracker:
- AMD Ryzen 5 5625U: Vulkan on the GPU ✓, Wi-Fi ✓, sound ✓
- VirtualBox and QEMU: full desktop ✓
- Lenovo X1 Carbon Gen 8: in progress →
- Raspberry Pi 4 / ARM64: planned →

Then a link: "See every tested device".

A speed moment: R5 booting to the desktop with a visible timer, or Tracker opening a folder of thousands of files. Show it, don't claim it. Use only R5's own measurements, and say which device.

## Look and feel

- Plenty of white, the Pebble's blue and gold, and Manrope everywhere (one identity with the logo, see `docs/BRAND-GUIDE.md`).
- Small animations: the Pebble "waves" on hover, counters count up.
- A dark mode, because R5 has one.

## Promotion, later

- A video of 20–30 seconds: boot, `vulkaninfo` showing `AMD Radeon Graphics (RADV RENOIR)`, the teapot, then the logo and a tagline.
- One image for social media: the About header with the `vulkaninfo` line.
- Where: r/osdev, Hacker News ("Show HN"), Mastodon (fosstodon), and a tip to Phoronix. Do not announce it on the Haiku forums: Haiku does not accept AI contributions, so it is sensitive there.

## Honesty rules (always)

- Vulkan and the GPU: "experimental · tested on Ryzen 5 5625U". Vulkan programs run on the GPU, and the desktop is still drawn by the CPU. Per-program address spaces come with M5.
- Name the tested devices. Never "works on all modern laptops".
- "Vulkan" is a Khronos trademark. Say "runs Vulkan through RADV", never "Vulkan conformant".
- Spock and "live long…" are a wink in posts only, never in the logo, merchandise or official branding (Paramount).
- BeOS is history ("remember BeOS?"), not a claim that R5 is BeOS. R5 is independent and is not the official Haiku distribution, as the README says. No Haiku or BeOS logos.
