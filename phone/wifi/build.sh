#!/bin/sh
# Build wland for iOS 16 arm64 into phone/wifi/out, signed ad hoc.
# macOS with Xcode (the userland workflow runs this on GitHub's macOS runners).
set -e
cd "$(dirname "$0")"
mkdir -p out
CC="${IOS_CC:-xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0}"
$CC -O2 -Wall -Wextra -Wno-unused-parameter -o out/wland wland.c smem.c pmem.c glink.c link.c qmi.c rmtfs.c pdmapper.c tftp.c scm.c wlfw.c radio.c ipnet.c bridge.c
codesign -f -s - out/wland
ls -la out/wland
