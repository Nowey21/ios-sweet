#!/bin/sh
# RedwoodAudio.driver: the HAL plug-in through which iOS's audio server sees a speaker, a
# microphone and a Taptic Engine on this phone (audio_plugin.c, redwood_audio.h, Info.plist:
# HackiOS user space, MIT License, Copyright (c) 2026 OOSEX GANG, as carried by ios-topaz;
# unchanged here). It writes to /dev/redwood-audio, reads /dev/redwood-mic and writes the haptics
# to /dev/redwood-haptic: on davinci kernel patch 0212 (bsd/dev/davinci_audio.c).
# Output: phone/audio/out/RedwoodAudio.driver (install into /Library/Audio/Plug-Ins/HAL).
set -eu
cd "$(dirname "$0")"
B=out/RedwoodAudio.driver
rm -rf out && mkdir -p "$B"
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -O2 -Wall -bundle \
	-framework CoreFoundation -o "$B/RedwoodAudio" audio_plugin.c
cp Info.plist "$B/Info.plist"
codesign -f -s - "$B"
ls -lR out
