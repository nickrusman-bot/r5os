# HDMI and DisplayPort audio

An HDMI pin with no device type was labeled `HDMI <NULL>`. The location string was "HDMI" and the device string was missing, and that missing string was printed. The output is now named `HDMI`. A digital-other input is `HDMI in`. Any other pin without a known type is `Output`. DisplayPort uses the same pin type as HDMI on these codecs, so it shows under that name.

Analog converters still choose the sample format. An HDMI converter is added to that stream only when it supports the same format. If the combined mask would be empty, the driver keeps the analog converters and leaves the HDMI converter out. A codec with only a digital converter uses that converter's own format.

The digital converter is enabled with DigEn. The PCM format stays on the converter-format register. Sound still needs the GPU modesetting driver to put those samples on the link. The VirtualBox HDA codec has no HDMI pin, so this path was not heard.
