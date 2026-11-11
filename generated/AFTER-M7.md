# After M7

Open after the window. Stick 40 passed the window on RADV.

- The isolation stage hangs the GPU until the M10 reset. Stick 40 did not run that stage. Detail below.
- `DRM_IOCTL_PRIME` returns ENOSYS. Stick 40 boot 1 logged it during the clear, and the clear still passed. The blit uses a host pointer. Detail below.

One line each. These showed up in sticks 35, 35-b, 36, 37 and 38 and are outside the Vulkan window. Known hardware lines are listed so they are not treated as new.

- sshd cannot create `/packages/openssh-10.4p1-1/.self/var/run/sshd.pid` (stick 35-b). The package has no `var/run`. Left.
- packagefs and package_daemon open `activated-packages` and it is missing on first boot (all four sticks). Haiku then loads every package in the directory. Left.
- `/etc/shadow` is missing, so the registrar cannot open the shadow password file (all four). Left.
- `module_init_post_boot_device` cannot normalize the path of the `fat` and `ntfs` module images (all four). Left.
- Module `file_cache/launch_speedup/v1` is not found (all four). Left.
- `remove_memory_type_range(): no range known for area` while RADV areas are freed (stick 37, after the swapchain assert killed the team). The window stop is the assert, not this line. Left.
- `DRM_IOCTL_PRIME` returns ENOSYS during `r5-wsi-test` (stick 37). Swapchain creation continued to the indirect-buffer assert, so this call was not the stop. The buffer blit uses a host pointer. Left until a stick fails on it.
- xhci `invalid max_burst_payload` (all four). Known hardware, left.
- `usb_disk` operation `0x35` / request sense invalid field in CDB (all four). Known hardware, left.
- `pch_i2c` `enable_device failed` on the two buses whose version reads `0xffffffff` (all four). Known hardware, left.
- `ps2_dev: reset failed` (all four). Known hardware, left.
- `hda` input tree and record widgets fail (all four). Known hardware, left.
- bluetooth connection thread error (all four). Known hardware, left.
- Isolation-stage VM fault `pasid 32770 vmid 7 addr 0x80000000`, then `fence timeout` / `gpu lost` with fault `00700c10` and no reset (sticks 35 and 35-b). The later packet stages print `skipped, gpu lost`, and stick 35-b's `R1ab fail` is that same lost GPU. Known, M10, left.
