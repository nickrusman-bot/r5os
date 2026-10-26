#!/usr/bin/env bash
# Boot r5-os.image with devices Haiku actually ships on AMD64:
# ATA/IDE disk (boot), virtio-net, USB tablet, VESA VGA, HDA audio.
# virtio-blk is now in the image too, but BIOS boot stays on IDE.
set -uo pipefail
killall -q qemu-system-x86_64 || true
sleep 1
export DISPLAY="${DISPLAY:-:0}"
cd /home/nickr/r5os/haiku/generated.x86_64
ls -lh r5-os.image

accel=tcg
cpu=qemu64
if [[ -e /dev/kvm ]] && [[ -r /dev/kvm ]]; then
	accel=kvm
	cpu=host
fi

setsid qemu-system-x86_64 \
	-name 'R5OS' \
	-machine pc,accel="${accel}" \
	-cpu "${cpu}" \
	-m 2048 \
	-smp 2 \
	-drive file=r5-os.image,format=raw,if=ide,index=0,media=disk \
	-vga std \
	-display gtk,zoom-to-fit=on \
	-usb \
	-device usb-tablet \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0 \
	-device virtio-rng-pci \
	-device intel-hda \
	-device hda-duplex \
	</dev/null >/tmp/r5-qemu.log 2>&1 &
echo qemu_pid=$! accel="${accel}" cpu="${cpu}"
sleep 2
ps -eo pid,cmd | grep -v grep | grep qemu-system-x86_64 || true
echo '--- log ---'
cat /tmp/r5-qemu.log 2>/dev/null | tail -n 30 || true
