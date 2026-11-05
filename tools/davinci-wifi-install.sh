#!/bin/bash
# Put wland and the Wi-Fi firmware on the running phone over the kernel file
# server (tools/davinci-files.py, 192.168.7.2:2223, USB NCM).
#   tools/davinci-wifi-install.sh WLAND_BINARY
# Firmware comes from this phone only: research/fw/modem (modem partition,
# see docs/DAVINCI_DRIVERS.md) and backup/{modemst1,modemst2,fsg,fsc}.img
# for rmtfs (IMEI and calibration: phone and PC only, never a repository).
# The file server does not follow symlinks: /var is written as /private/var.
set -euo pipefail
# Git Bash would turn "/private/..." arguments into Windows paths.
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
wland=${1:?usage: davinci-wifi-install.sh WLAND_BINARY}
files() { python -I tools/davinci-files.py "$@"; }

for d in /private/var/local /private/var/local/bin /private/var/local/fw \
	/private/var/local/fw/modem /private/var/local/fw/rmtfs /private/var/local/fw/rw; do
	files mkdir "$d" 0755 || true     # exists already: fine
done
for f in research/fw/modem/*; do
	files put "$f" "/private/var/local/fw/modem/$(basename "$f")" 0644
done
for p in modemst1 modemst2 fsg fsc; do
	files put "backup/$p.img" "/private/var/local/fw/rmtfs/$p.img" 0600
done
files put "$wland" /private/var/local/bin/wland 0755
echo "installed: wland, $(ls research/fw/modem | wc -l) modem files, 4 rmtfs images"
