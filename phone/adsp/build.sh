#!/bin/sh
# Build adsp (the audio DSP's bring-up) for iOS 16 arm64 into phone/adsp/out, signed ad hoc.
# It is built out of wland's files (phone/wifi) with the DSP's numbers: GLINK doorbell bit 24 in SMEM
# host 2, QRTR node 5, pd-mapper with the DSP's domains.
set -e
cd "$(dirname "$0")"
mkdir -p out
CC="${IOS_CC:-xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0}"
W=../wifi
$CC -O2 -Wall -Wextra -Wno-unused-parameter -I$W -DIPC_GLINK=24 -DGLINK_HOST=SMEM_HOST_ADSP -DQRTR_NODE_MODEM=5 \
	-DPDMAPPER_ADSP -o out/adsp adsp.c $W/smem.c $W/pmem.c $W/glink.c $W/link.c $W/qmi.c $W/pdmapper.c
codesign -f -s - out/adsp
ls -la out/adsp
