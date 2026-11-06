#!/bin/bash
# The audio DSP's firmware and the adsp program onto the running phone:
#   tools/davinci-adsp-install.sh USERLAND_DIR
# Firmware: research/fw/adsp (adsp.mdt, adsp.b00..b26, adsp*.jsn), taken out of the modem
# partition backup (7z e backup/modem.img "image/adsp*"); it is Qualcomm's and Xiaomi's, so it
# stays on this PC and the phone, never in the repository.
# Then: tools/davinci-run.py /var/local/bin/adsp 2   (reads only), ... 6 (starts the DSP).
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
U=${1:?usage: davinci-adsp-install.sh USERLAND_DIR}
F="python -I tools/davinci-files.py"
$F mkdir /private/var/local/fw 755 || true
$F mkdir /private/var/local/fw/adsp 755 || true
for f in research/fw/adsp/*; do
	$F put "$f" "/private/var/local/fw/adsp/$(basename "$f")" 644
done
$F put "$U/adsp" /private/var/local/bin/adsp 755
python -I tools/davinci-cmd.py sync
echo "adsp installed; first: tools/davinci-run.py /var/local/bin/adsp 2"
