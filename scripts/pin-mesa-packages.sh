#!/usr/bin/env bash
# Point the HaikuPorts pin and the image package solver at libglvnd + Mesa 25.
# Mesa 22 is not downloaded and not installed.
set -eu
HAIKU=/home/nickr/r5os/haiku
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKGDIR="$ROOT/distro/packages/x86_64"
DL="$HAIKU/generated.x86_64/download"
PIN="$HAIKU/build/jam/repositories/HaikuPorts/x86_64"
REPO_TOOL="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package_repo/package_repo"
CACHE="$HAIKU/generated.x86_64/objects/haiku/x86_64/packaging/repositories/HaikuPorts"
OUT="$DL/r5-haikuports.repo"

needed=(
	libglvnd-1.7.0-1-x86_64.hpkg
	llvm21_runtime-21.1.8-1-x86_64.hpkg
	mesa-25.3.6-1-x86_64.hpkg
	mesa_devel-25.3.6-1-x86_64.hpkg
	mesa_lavapipe-25.3.6-1-x86_64.hpkg
	vulkan_loader-1.4.367-1-x86_64.hpkg
	vulkan_tools-1.4.367-1-x86_64.hpkg
)
mkdir -p "$DL"
for name in "${needed[@]}"; do
	if [[ ! -f "$PKGDIR/$name" ]]; then
		echo "missing $PKGDIR/$name" >&2
		exit 1
	fi
	cp -f "$PKGDIR/$name" "$DL/$name"
done

python3 - "$PIN" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = (
    "\tmesa-22.0.5-3\n"
    "\tmesa_devel-22.0.5-3\n"
    "\tmesa_swpipe-22.0.5-3\n"
)
new = (
    "\tlibglvnd-1.7.0-1\n"
    "\tllvm21_runtime-21.1.8-1\n"
    "\tmesa-25.3.6-1\n"
    "\tmesa_devel-25.3.6-1\n"
    "\tmesa_lavapipe-25.3.6-1\n"
    "\tvulkan_loader-1.4.367-1\n"
)
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    sys.exit("mesa pin block not found")
if "mesa-22.0.5-3" in text or "mesa_swpipe-22" in text:
    sys.exit("mesa 22 still in the pin")
path.write_text(text, encoding="utf-8")
print("pin updated")
PY

list="$(mktemp)"
"$REPO_TOOL" list -f "$CACHE" \
	| sed -n 's/^[[:space:]]*\(.*\.hpkg\)$/\1/p' \
	| grep -v -E '^(mesa|mesa_devel|mesa_swpipe|mesa_source)-22\.0\.5-3-' \
	> "$list"
# Official packages stay in a Haiku Project repository. R5OS packages use
# another repository because package_repo rejects a mixed vendor.
cat > "$DL/r5-haikuports.repo.info" <<'EOF'
name HaikuPorts-BuildPackages
vendor "Haiku Project"
summary "The HaikuPorts/Build-Packages repository"
priority 1
baseurl https://eu.hpkg.haiku-os.org/haikuports/master/build-packages
identifier https://eu.hpkg.haiku-os.org/haikuports/master/build-packages/
architecture x86_64
EOF
"$REPO_TOOL" update -C "$DL" -q -t "$CACHE" "$OUT" "$list"
rm -f "$list"

cat > "$DL/r5-opengl.repo.info" <<'EOF'
name R5OpenGL
vendor "R5OS"
summary "R5OS OpenGL packages"
priority 1
baseurl https://eu.hpkg.haiku-os.org/haikuports/master/build-packages
identifier tag:r5os.local,2026:repositories/opengl/x86_64
architecture x86_64
EOF
# vulkan_loader is the HaikuPorts package (vendor Haiku Project). The others are R5OS.
r5pkgs=()
for name in "${needed[@]}"; do
	if [[ "$name" == vulkan_loader-* ]]; then
		continue
	fi
	r5pkgs+=("$DL/$name")
done
"$REPO_TOOL" create -q "$DL/r5-opengl.repo.info" "${r5pkgs[@]}"
# create always writes a file named "repo" next to the info file.
mv -f "$DL/repo" "$DL/r5-opengl.repo"
echo "repo $OUT"
echo "repo $DL/r5-opengl.repo"
"$REPO_TOOL" list -f "$OUT" | grep -E 'mesa|libglvnd|llvm21' || echo "no mesa 22 in haikuports cache"
"$REPO_TOOL" list -f "$DL/r5-opengl.repo" | sed -n 's/^[[:space:]]*\(.*\.hpkg\)$/\1/p'
