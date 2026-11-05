#!/bin/bash
# Build UniXNU's native Metal bridge (metal2vulkan runtime, Rust staticlib) and
# SweetMetal2.bundle (Metal on Turnip) for iOS 16 arm64, on a macOS runner.
#   phone/metal2/build-ci.sh WORKDIR TURNIP_DYLIB
# Same recipe as ios-flame's tools/build-m2v-bridge.sh + phone/metal2/build.sh:
# metal2vulkan 720ef1ce (UniXNU's pin; fetched, not copied) + bridge/patches.
# Needs rustup (toolchain 1.98.0, target aarch64-apple-ios) and Homebrew llvm (llvm-as).
# Output: phone/metal2/out/SweetMetal2.bundle, phone/metal2/out/m2vtest
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:?usage: build-ci.sh WORKDIR TURNIP_DYLIB}
TURNIP=${2:?usage: build-ci.sh WORKDIR TURNIP_DYLIB}
mkdir -p "$W"
W=$(cd "$W" && pwd)
M2V_SHA=720ef1ce
SRC=$W/metal2vulkan
OUT=$W/m2v-bridge

if [ ! -d "$SRC/.git" ]; then
	git clone -q https://github.com/steelbrain/metal2vulkan.git "$SRC"
	git -C "$SRC" checkout -q "$M2V_SHA"
	for p in "$HERE"/bridge/patches/*.patch; do
		echo "apply $(basename "$p")"
		git -C "$SRC" apply --whitespace=nowarn "$p"
	done
fi

mkdir -p "$OUT"
LLVM_AS=$(brew --prefix llvm)/bin/llvm-as
"$LLVM_AS" -o "$OUT/runtime_examples.air" "$SRC/crates/metal2vulkan-validation/fixtures/public/examples/runtime_examples.ll"
"$LLVM_AS" -o "$OUT/function_constant.air" "$SRC/crates/metal2vulkan-validation/fixtures/public/kernel_function_constant_gate_fold.ll"
cat > "$OUT/Cargo.toml" <<EOF
[package]
name = "native-metal-bridge"
version = "0.1.0"
edition = "2024"
rust-version = "1.98"
[workspace]
[lib]
crate-type = ["staticlib"]
path = "$HERE/bridge/NativeMetalBridge.rs"
[dependencies]
metal2vulkan = { path = "$SRC/crates/metal2vulkan", features = ["ash"] }
metal2vulkan-runtime = { path = "$SRC/crates/metal2vulkan-runtime" }
libc = "0.2"
sha2 = { version = "0.10", features = ["asm"] }
[profile.release]
panic = "unwind"
debug = 1
EOF
NMB_FIXTURE_AIR=$OUT/runtime_examples.air NMB_FC_AIR=$OUT/function_constant.air IPHONEOS_DEPLOYMENT_TARGET=16.0 \
	cargo +1.98.0 build --release --manifest-path "$OUT/Cargo.toml" --target aarch64-apple-ios \
	--target-dir "$OUT/target" > "$OUT/build.log" 2>&1 ||
	{ grep -E "^error" -A8 "$OUT/build.log" | head -n 80; exit 1; }
BRIDGE=$OUT/target/aarch64-apple-ios/release/libnative_metal_bridge.a

# IOKit's user headers are not public in the iPhoneOS SDK: take them from the macOS SDK
inc=$W/iokit-inc
mkdir -p "$inc"
[ -d "$inc/IOKit" ] || cp -R "$(xcrun --sdk macosx --show-sdk-path)/System/Library/Frameworks/IOKit.framework/Headers" "$inc/IOKit"
CC="xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -I $inc"
cd "$HERE"
B=out/SweetMetal2.bundle
rm -rf "$B" && mkdir -p "$B"
$CC -x objective-c++ -std=c++17 -fobjc-arc -O2 -g -Wall -Wno-deprecated-declarations -Wno-unused-function -bundle \
	-o "$B/SweetMetal2" SweetMetal2.mm NativeBridgeMetalFrontend.mm -x none "$BRIDGE" -Wl,-dead_strip -lc++ \
	-framework Foundation -framework Metal -framework IOSurface -framework IOKit -framework CoreFoundation
strip -x "$B/SweetMetal2"
cp Info.plist "$B/Info.plist"
cp "$TURNIP" "$B/libvulkan_freedreno.dylib"
strip -x "$B/libvulkan_freedreno.dylib"
codesign -f -s - "$B/libvulkan_freedreno.dylib"
codesign -f -s - "$B"
$CC -O2 -Wall -o out/m2vtest m2vtest.c "$BRIDGE" -Wl,-dead_strip
strip -x out/m2vtest && codesign -f -s - out/m2vtest
nm -m "$B/SweetMetal2" | grep "weak external" | grep -v "dynamically looked up" || true
ls -la "$B" out/m2vtest
