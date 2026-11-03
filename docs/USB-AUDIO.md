# USB audio

`usb_audio` speaks USB Audio Class 1.0 and 2.0. Class 1.0 is unchanged: the same alternate, the same three-byte sample-rate request on the endpoint, and the same one-millisecond packets. Class 2.0 is accepted when the control header version is `0x0200`. The streaming interface then takes the channel count from the class-specific general descriptor, and the subslot size and bit resolution from the format descriptor. Sample rates for that device are 44.1, 48, 88.2, and 96 kHz. The driver sends the chosen rate as a four-byte value to the clock source.

A stream may have 1 to 16 channels. Media settings, Audio, has **USB layout** next to the output device: Automatic, Stereo (2.0), 4 channels, 5.1, or 7.1. Automatic uses the widest count the device offers. A layout the device does not have falls back to the nearest count. The choice is `channel_layout` in `~/config/settings/kernel/drivers/usb_audio`. Restart media services after changing it. The system mixer stays stereo and connects to the selected channel pair. The other channels are further inputs on the same device.

24-bit samples in three bytes are packed. 24-bit samples in a four-byte container are left-justified: the low byte is padding. `usb_audiotest` checks those layouts and the descriptor bytes. It does not open a device.

Unplug stops the isochronous queue before the transfers are cancelled, and waits until the callback has returned. The EHCI host-controller locks in Haiku #18842, #18994, #18995, and #19472 are not changed here.

On the VirtualBox guest from build 10045, `usb_audiotest` printed `all checks passed` on 3 Nov 2026 (reported at 11:26 Europe/Amsterdam). Audio settings showed the USB layout menu. No USB audio device was attached.
