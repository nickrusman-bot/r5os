#!/bin/bash
# Host AddressSanitizer fuzz for the MIDI 2.0 parsers.
# Usage: fuzz-midi2.sh [seconds]
set -eu
cd "$(dirname "$0")/.."
seconds="${1:-600}"
g++ -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 \
	-I scripts/midi2-host \
	-I distro/media/headers/os/midi2 \
	-I distro/media/src/add-ons/kernel/drivers/midi/usb_midi \
	scripts/midi2-fuzz.cpp distro/media/src/kits/midi2/Midi2Library.cpp \
	-o /tmp/midi2-fuzz
mkdir -p generated/midi2
# Kernel parsers first, then the header parsers.
/tmp/midi2-fuzz kernel "$seconds" > "generated/midi2/fuzz-kernel.log" 2>&1
for target in payload property profile stream usb; do
	/tmp/midi2-fuzz "$target" "$seconds" > "generated/midi2/fuzz-${target}.log" 2>&1 &
done
wait
echo fuzz_done
