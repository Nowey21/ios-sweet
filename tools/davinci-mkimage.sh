#!/bin/bash
# Boot image for davinci from a kernel: tools/davinci-mkimage.sh KERNEL NAME [EXTRA_BOOT_ARGS...]
# -> shim/out/shim-boot-NAME.img. Runs in WSL (python3, aarch64 toolchain for shim/build.sh):
#   wsl.exe -d Ubuntu -u root -- bash tools/davinci-mkimage.sh xnu/out-0209/kernel.development.sweet 0209mf8v
# Base boot-args: the daily set (GPU + Metal UI, OSM top frequencies, 8 cores). -v is needed with
# kernels built by Xcode 16 (they freeze on the boot progress bar without it).
set -euo pipefail
cd "$(dirname "$0")/.."
K=${1:?kernel}; N=${2:?name}; shift 2
CL="debug=0xc serial=3 keepsyms=1 -enable_kprintf_spam -noprogress -v rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30 sweetboard=davinci sweetusb=2 sweetusbncm=1 sweetgpu=1 sweetmtl=2 flamecpufreq=1 cpus=8 $*"
python3 -I tools/mkpayload.py --cmdline "$CL" --gpufw research/fw/gpu "$K" shim/out/davinci-flame.adt "shim/out/payload-$N.bin" >/dev/null
(cd shim && bash build.sh "out/payload-$N.bin" >/dev/null 2>&1 && cp out/shim-boot.img "out/shim-boot-$N.img")
sha256sum "shim/out/shim-boot-$N.img"
