#!/bin/bash
# Fill the EFI system partition: rEFInd as the removable-media loader,
# the Haiku loader beside it, and the black theme next to rEFInd.
set -euo pipefail

if [[ $# -ne 7 ]]; then
	echo "usage: populate-esp.sh image fatshell icon loader stage efiname keys" >&2
	exit 1
fi

IMAGE=$1
FATFS=$2
EFIICON=$3
LOADER=$4
STAGE=$5
EFINAME=$6
EFIKEYS=$7

for need in "$FATFS" "$EFIICON" "$LOADER" "$STAGE/refind_x64.efi" \
	"$STAGE/refind.conf" "$STAGE/icons/os_haiku.png" \
	"$STAGE/themes/r5-black/background.png" \
	"$STAGE/themes/r5-black/theme.conf" \
	"$STAGE/COPYING.txt" "$STAGE/LICENSE.txt" \
	"$EFIKEYS/README.md"; do
	if [[ ! -f "$need" ]]; then
		echo "missing $need" >&2
		exit 1
	fi
done

rm -f "$IMAGE"
dd if=/dev/zero of="$IMAGE" bs=1024 count=16384 status=none

"$FATFS" --initialize "$IMAGE" "Haiku ESP"

run() {
	echo "$1" | "$FATFS" "$IMAGE"
}

run "mkdir myfs/EFI"
run "mkdir myfs/EFI/BOOT"
run "mkdir myfs/EFI/haiku"
run "mkdir myfs/KEYS"
run "cp :$LOADER myfs/EFI/haiku/haiku_loader.efi"
run "cp :$STAGE/refind_x64.efi myfs/EFI/BOOT/$EFINAME"
run "cp :$STAGE/refind.conf myfs/EFI/BOOT/refind.conf"
run "cp :$STAGE/COPYING.txt myfs/EFI/BOOT/COPYING.txt"
run "cp :$STAGE/LICENSE.txt myfs/EFI/BOOT/LICENSE.txt"
run "cp :$STAGE/CREDITS.txt myfs/EFI/BOOT/CREDITS.txt"
run "cp -r :$STAGE/icons myfs/EFI/BOOT/icons"
run "cp -r :$STAGE/themes myfs/EFI/BOOT/themes"
run "cp :$EFIICON myfs/.VolumeIcon.icns"
run "cp :$EFIKEYS/README.md myfs/KEYS/README.md"
for ext in auth cer crt; do
	run "cp :$EFIKEYS/DB.$ext myfs/KEYS/DB.$ext"
done
