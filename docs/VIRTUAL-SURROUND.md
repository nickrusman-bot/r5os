# Virtual surround

Virtual surround is off until Audio settings saves `virtual_surround on` in the user file `config/settings/kernel/drivers/virtual_surround`. Restart media services to apply it. Off writes `virtual_surround off`. The mixer reads that file when it starts.

The mix runs only when the output is stereo. A 5.1 or 7.1 device still receives its own channels. USB and HDA playback are unchanged; they receive the stereo mix the mixer already sends.

Each speaker is one impulse. The near ear is louder. The far ear is later by the Woodworth delay for a head radius of 8.75 cm and a speed of sound of 343 m/s. The level difference is 12 dB at 90 degrees. A speaker behind the listener also delays the near ear, and past 100 degrees both ears are a little quieter. Center, mono, and the back-center channel are equal in both ears. The low-frequency channel is equal and quieter. There is no extra lookahead. The longest delay at 48 kHz is a fraction of a millisecond. At 96 kHz the same delay still fits in the 128-sample history. A higher rate clamps that delay.

The speaker order is the low 12 channel types: left, right, center, sub, rear left, rear right, front left-of-center, front right-of-center, back center, side left, side right, and mono.

No head recording is shipped. The MIT KEMAR set is marked for research only. CIPIC is similar. A Creative Commons set was not fetched. This is not a measured head-related transfer function, and it is not Dolby.

`virtual_surround_test` checks the delays and one impulse on the host. It is not installed in the image. The menu was not opened in a guest, and no headphone mix was heard.
