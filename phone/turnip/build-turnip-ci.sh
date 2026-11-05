#!/bin/bash
# Build Turnip (Mesa's Vulkan driver for Adreno) for iOS 16 arm64 with UniXNU's
# darwin kernel backend, on a macOS runner (workflow gpu-userland.yml).
#   phone/turnip/build-turnip-ci.sh WORKDIR
# Same recipe as ios-flame's tools/build-turnip-ios.sh: Mesa 85fda3b3 + UniXNU's
# patches/mesa in the order of scripts/data/adreno-compiler-inputs.json (pinned
# UniXNU commit below; fetched, not copied) + phone/turnip/patches. The backend
# (tu_knl_darwin.cc) speaks phone/turnip/NativeAdrenoABI.h, which equals the
# kernel's iokit/Kernel/arm/SweetGPUABI.h (ios-flame 0115).
# Output: WORKDIR/build-ios/src/freedreno/vulkan/libvulkan_freedreno.dylib
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${1:?usage: build-turnip-ci.sh WORKDIR}
mkdir -p "$W"
W=$(cd "$W" && pwd)
UNIXNU_SHA=2b88cfc914d17cce988761b5c3976128596d4d34	# 2026-10-21, 8 Mesa patches (ios-flame's set)
MESA_SHA=85fda3b36405bcaea8bffc64aa4f2651dc90770d

fetch() {	# dir url sha
	[ -d "$1/.git" ] && return 0
	git init -q "$1"
	git -C "$1" fetch -q --depth 1 "$2" "$3"
	git -C "$1" checkout -q FETCH_HEAD
}
fetch "$W/unixnu" https://github.com/UniXNU/UniXNU.git "$UNIXNU_SHA"
fetch "$W/mesa" https://gitlab.freedesktop.org/mesa/mesa.git "$MESA_SHA" ||
	fetch "$W/mesa" https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa "$MESA_SHA"

if [ ! -f "$W/mesa/.patched" ]; then
	python3 - "$W/unixnu/scripts/data/adreno-compiler-inputs.json" > "$W/patches.txt" <<'PY'
import json, sys
j = json.load(open(sys.argv[1]))
assert j['mesa']['commit'].startswith('85fda3b3'), j['mesa']['commit']
print('\n'.join(j['patches']))
PY
	while read -r p; do
		echo "apply $p"
		git -C "$W/mesa" apply --whitespace=nowarn "$W/unixnu/$p"
	done < "$W/patches.txt"
	for p in "$ROOT"/phone/turnip/patches/*.patch; do
		echo "apply $(basename "$p")"
		git -C "$W/mesa" apply --whitespace=nowarn "$p"
	done
	touch "$W/mesa/.patched"
fi

sdk=$(xcrun --sdk iphoneos --show-sdk-path)
cross=$W/ios16.cross
flags="'-target', 'arm64-apple-ios16.0', '-isysroot', '$sdk', '-fstack-protector-strong'"
cat > "$cross" <<EOF
[binaries]
c = '$(xcrun --sdk iphoneos --find clang)'
cpp = '$(xcrun --sdk iphoneos --find clang++)'
objc = '$(xcrun --sdk iphoneos --find clang)'
ar = '$(xcrun --sdk iphoneos --find ar)'
strip = '$(xcrun --sdk iphoneos --find strip)'
pkg-config = '/usr/bin/false'
[host_machine]
system = 'darwin'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = [$flags]
cpp_args = [$flags]
objc_args = [$flags]
c_link_args = [$flags]
cpp_link_args = [$flags]
objc_link_args = [$flags]
EOF
opts="-Dgallium-drivers= -Dvulkan-drivers=freedreno -Dfreedreno-kmds=darwin -Dtu-darwin-abi-dir=$ROOT/phone/turnip
 -Dplatforms=macos -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled
 -Dllvm=disabled -Dshared-glapi=disabled -Dxmlconfig=disabled -Dshader-cache=disabled -Dtools= -Dbuild-tests=false
 -Dvideo-codecs= -Dvulkan-layers= -Dlibunwind=disabled -Dlmsensors=disabled -Dvalgrind=disabled -Dexpat=disabled
 -Dzstd=disabled -Dbuildtype=release -Db_ndebug=true"
out=$W/build-ios
export CC=$(xcrun --sdk iphoneos --find clang) CXX=$(xcrun --sdk iphoneos --find clang++)
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
if [ ! -f "$out/build.ninja" ]; then
	# shellcheck disable=SC2086
	meson setup "$out" "$W/mesa" --wrap-mode=nodownload --cross-file "$cross" $opts > "$W/configure.log" 2>&1 ||
		{ tail -n 40 "$W/configure.log"; exit 1; }
fi
ninja -C "$out" src/freedreno/vulkan/libvulkan_freedreno.dylib > "$W/build.log" 2>&1 ||
	{ grep -E "error:|FAILED" "$W/build.log" | head -n 40; exit 1; }
lib=$out/src/freedreno/vulkan/libvulkan_freedreno.dylib
nm -gU "$lib" | grep -q " T _vkGetInstanceProcAddr" || { echo "no vkGetInstanceProcAddr export" >&2; exit 1; }
# iOS 16 lacks symbols newer SDKs declare: weak imports resolve to NULL there
nm -m "$lib" | grep "weak external" || true
codesign -f -s - "$lib"
ls -la "$lib"
