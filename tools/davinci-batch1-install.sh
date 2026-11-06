#!/bin/bash
# Batch 1 for davinci (2026-11-05 night): install on the running phone, then reboot.
#   tools/davinci-batch1-install.sh USERLAND_DIR
# USERLAND_DIR = the phone-userland CI artifact (wland, v2test, tools/).
#   - wland that starts at 60 s instead of 90 (Wi-Fi up ~30 s sooner)
#   - ios-flame's phone tools in /var/local/bin (lsreg, prefs, oslog, psx, delayrun, ...)
#   - /var/local/mtl2-clients = "* -chronod": every process gets the Metal device
#     (WebContent: Safari pages; PosterBoard/AegirPoster: the wallpaper), chronod
#     stays on CA's CPU path (its widgets are white with Metal, ios-flame)
#   - launchd jobs: v2test/vktest removed (vktest ran every 120 s);
#     lsreg: `lsreg -r` once, 150 s after boot (rebuilds LaunchServices'
#     database so the poster extensions register: ios-flame's wallpaper fix);
#     oslogwifi: wifid's unified log for 15 minutes from 30 s after boot
#     (why it does not rejoin by itself)
# Remove the lsreg and oslogwifi jobs after one boot: davinci-launchd-job.py remove NAME.
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
U=${1:?usage: davinci-batch1-install.sh USERLAND_DIR}
F="python -I tools/davinci-files.py"
J="python -I tools/davinci-launchd-job.py"
tmp=$(mktemp -d)
# Windows Python cannot open Git Bash's /tmp paths
command -v cygpath >/dev/null && tmp=$(cygpath -m "$tmp")

$F put "$U/wland" /private/var/local/bin/wland 755
for t in delayrun lsreg prefs oslog psx pstack nettest openapp exq; do
	$F put "$U/tools/$t" /private/var/local/bin/$t 755
done
printf '* -chronod\n' > "$tmp/mtl2-clients"
$F put "$tmp/mtl2-clients" /private/var/local/mtl2-clients 644

$J remove v2test
$J remove vktest
$J add lsreg /var/local/bin/delayrun 150 /var/local/bin/lsreg -r
$J add oslogwifi /var/local/bin/delayrun 30 /var/local/bin/oslog -t 900 wifid
python -I tools/davinci-cmd.py sync
rm -rf "$tmp"
echo "batch 1 installed; reboot the phone (tools/davinci-cmd.py reboot, then fastboot boot shim/out/shim-boot-0206mf.img)"
