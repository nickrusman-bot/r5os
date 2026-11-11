#!/bin/bash
# Plant boot 1 settings and the home scripts into the anyboot.
set -eu
HAIKU=/home/nickr/r5os/haiku
IMG="$HAIKU/generated.x86_64/r5-os-anyboot.iso"
BFS="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE=/tmp/m8-plant
rm -rf "$STAGE"
mkdir -p "$STAGE"
for name in m8-run m8-boot2 m8-show check.sh radeon_hd; do
	git -C "$ROOT" show "HEAD:distro/graphics/boot/$name" > "$STAGE/$name"
done
"$BFS" --start-offset 6291456 "$IMG" <<EOF
mkdir myfs/home/config
mkdir myfs/home/config/settings
mkdir myfs/home/config/settings/kernel
mkdir myfs/home/config/settings/kernel/drivers
put $STAGE/m8-run myfs/home/m8-run
put $STAGE/m8-boot2 myfs/home/m8-boot2
put $STAGE/m8-show myfs/home/m8-show
put $STAGE/check.sh myfs/home/check.sh
put $STAGE/radeon_hd myfs/home/config/settings/kernel/drivers/radeon_hd
chmod 755 myfs/home/m8-run
chmod 755 myfs/home/m8-boot2
chmod 755 myfs/home/m8-show
chmod 755 myfs/home/check.sh
quit
EOF
echo planted
