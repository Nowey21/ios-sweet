#!/bin/bash
# LineageOS recovery from backup/recovery.img + this PC's adb key in /adb_keys,
# for a temporary `fastboot boot` (the recovery partition is not touched).
set -euo pipefail
R="/mnt/c/Users/ciaoc/Desktop/ios mi9t k20/ios-sweet"
rm -rf /tmp/recup /tmp/recrd && mkdir -p /tmp/recrd
python3 "$R/tools/unpack_bootv2.py" "$R/backup/recovery.img" /tmp/recup >/dev/null
ls -l /tmp/recup
fmt=$(file -b /tmp/recup/ramdisk)
echo "ramdisk: $fmt"
cd /tmp/recrd
case "$fmt" in
	gzip*) gzip -dc /tmp/recup/ramdisk | cpio -idm --quiet ;;
	LZ4*) lz4 -dc /tmp/recup/ramdisk | cpio -idm --quiet ;;
	*) echo "unknown ramdisk format"; exit 1 ;;
esac
ls -l adb_keys	# a link to /product/etc/security/adb_keys, unmounted in recovery
rm -f adb_keys
cat "/mnt/c/Users/ciaoc/.android/adbkey.pub" > adb_keys
chmod 0644 adb_keys
find . | sort | cpio -o -H newc -R 0:0 --quiet > /tmp/rec-ramdisk.cpio
case "$fmt" in
	gzip*) gzip -9 -c /tmp/rec-ramdisk.cpio > /tmp/rec-ramdisk.bin ;;
	LZ4*) lz4 -l -9 -c /tmp/rec-ramdisk.cpio > /tmp/rec-ramdisk.bin ;;
esac
dtbo=""; [ -s /tmp/recup/recovery_dtbo ] && dtbo="--recovery_dtbo /tmp/recup/recovery_dtbo"
mkbootimg --header_version 2 --kernel /tmp/recup/kernel --ramdisk /tmp/rec-ramdisk.bin \
	--dtb /tmp/recup/dtb $dtbo --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
	--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
	--os_version 16.0.0 --os_patch_level 2025-12 \
	--cmdline "console=ttyMSM0,115200n8 earlycon=msm_geni_serial,0x880000 androidboot.hardware=qcom androidboot.console=ttyMSM0 service_locator.enable=1 lpm_levels.sleep_disabled=1 loop.max_part=7 androidboot.super_partition=system" \
	-o "$R/recovery/recovery-adbkey.img"
ls -l "$R/recovery/recovery-adbkey.img"
