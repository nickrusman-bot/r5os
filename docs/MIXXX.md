# Mixxx op R5OS (side quest)

GitHub: [#55](https://github.com/nickrusman-bot/r5os/issues/55) (#56–#62 in volgorde).

Niet tijdens multi-user. Mixxx gaat **niet** in de basisimage. Eerst HaikuPorts, daarna hardware. QEMU/VirtualBox is geen DJ-test (geen USB-controller, geen echte latency).

Upstream-app: [mixxxdj/mixxx](https://github.com/mixxxdj/mixxx). Haiku-port: HaikuPorts `media-sound/mixxx` **2.5.4** (Begasus, 2025-09). Mixxx praat met PortAudio (Media Kit), PortMidi (Midi Kit), hidapi/libusb, Qt6, FFmpeg6.

Doel per niveau: **installeren → speakers → MIDI → USB-audio → HID → UAC2/cue**. Club-ready (`<10 ms`, vinyl/timecode) is het laatste, niet het eerste.

## Klaar (op Haiku, niet op R5OS gezien)

- [x] Recipe Mixxx 2.5.4 in HaikuPorts (skins `/boot/system/data/mixxx`, settings `config/settings/mixxx`, `BATTERY=OFF`).
- [x] PortAudio 19.07 met Haiku Media Kit host API.
- [x] PortMidi via Midi Kit (class-compliant USB MIDI op Haiku gemeld: Roland UM2 learn-wizard, Reloop RMX2 mapping).
- [x] hidapi 0.15.0 + libusb in HaikuPorts (geen native HID-backend).

## Open, in deze volgorde

- [ ] **1. Pakket op R5OS AMD64** ([#56](https://github.com/nickrusman-bot/r5os/issues/56))  
  `pkgman` of `haikuporter mixxx`. Qt6 + FFmpeg6 mee. Start, LateNight-skin, MP3/FLAC uit de bibliotheek, mixen met toetsenbord op de default output. Niet in `distro/`, geen eigen fork tot een Haiku-patch vastloopt.

- [ ] **2. Interne audio (Envy HDA)** ([#57](https://github.com/nickrusman-bot/r5os/issues/57))  
  Media-preflet toont een device. Mixxx xruns bij buffer 1024 / 512 / 256. Alleen speakers of één koptelefoon: geen aparte cue. ACP vs HDA op deze 5625U vastleggen in syslog.

- [ ] **3. Class-compliant USB MIDI** ([#58](https://github.com/nickrusman-bot/r5os/issues/58))  
  Controller **vóór** Mixxx steken (geen hotplug-scan). Mapping + learn. Endpointnamen zijn vaak `/dev/midi/usb/*`.

- [ ] **4. USB-audio UAC1, 2 kanalen** ([#59](https://github.com/nickrusman-bot/r5os/issues/59))  
  Stock `usb_audio`. Na plug: Media-preflet, media services herstarten (Haiku #12776 / #12777). Output in Mixxx. Nog geen 4-kanaals cue/master.

- [ ] **5. Eén HID-deck** ([#60](https://github.com/nickrusman-bot/r5os/issues/60))  
  Pioneer/NI-achtig dat geen standaard MIDI is, tegen hidapi/libusb. Werkt het niet: afschrijven tot er een native HID-pad is. Geen collectie mappings gokken.

- [ ] **6. UAC2 + meer dan 2 kanalen** ([#61](https://github.com/nickrusman-bot/r5os/issues/61))  
  Kernel: `usb_audio` + xHCI isochroon. Stock weigert UAC2. Community-fork (`rations/haiku-kernel-usb`) is **niet** R5OS tot die in onze tree zit, op onze hrev bouwt, en op echte hardware is gezien. Pas daarna cue + master in Mixxx.

- [ ] **7. Latency / vinyl** ([#62](https://github.com/nickrusman-bot/r5os/issues/62))  
  PortAudio abort/complete is deels stub. Timecode en `<10 ms` pas ná stap 6 en een gemeten callback. JACK2 in HaikuPorts staat uit (`ARCHITECTURES=!all`); Mixxx gebruikt het in dit recipe niet.

## Expres niet

- Mixxx in de nightly-image of About/What’s new als “zit erin”.
- `StartSession` of extra `app_server` “voor audio”.
- GPU-waveforms forceren vóór Barcelo `15e7` in `radeon_hd` (software-GL is verwacht).
- Claimen dat USB in VirtualBox een LAN- of clubtest is.
