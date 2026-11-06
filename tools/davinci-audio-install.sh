#!/bin/bash
# iOS audio on davinci, step 1 (needs kernel 0212: /dev/redwood-audio, -mic, -haptic):
#   tools/davinci-audio-install.sh USERLAND_DIR          install
#   tools/davinci-audio-install.sh --undo                take it out again
# USERLAND_DIR = the phone-userland CI artifact (it has RedwoodAudio.driver/).
#   - RedwoodAudio.driver into /Library/Audio/Plug-Ins/HAL: the audio server's speaker
#     ("Speaker"), microphone ("Codec") and Taptic Engine ("Actuator")
#   - VirtualAudio.plugin back into HAL as a symlink to HAL.disabled/ (it was taken out because,
#     with no audio device at all, mediaserverd crashed in it: ios-topaz NOTES); the plug-in gives
#     it the devices it wants
# Reboot afterwards. If mediaserverd crash-loops (CRASH mediaserverd in the live log): --undo.
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
F="python -I tools/davinci-files.py"
H=/Library/Audio/Plug-Ins/HAL
if [ "${1:-}" = "--undo" ]; then
	$F rm "$H/VirtualAudio.plugin" || true
	for f in _CodeSignature/CodeResources Info.plist RedwoodAudio; do $F rm "$H/RedwoodAudio.driver/$f" || true; done
	python -I tools/davinci-cmd.py sync
	echo "audio plug-ins removed; reboot"
	exit 0
fi
U=${1:?usage: davinci-audio-install.sh USERLAND_DIR | --undo}
D="$U/RedwoodAudio.driver"
$F mkdir "$H/RedwoodAudio.driver" 755
$F mkdir "$H/RedwoodAudio.driver/_CodeSignature" 755
$F put "$D/RedwoodAudio" "$H/RedwoodAudio.driver/RedwoodAudio" 755
$F put "$D/Info.plist" "$H/RedwoodAudio.driver/Info.plist" 644
$F put "$D/_CodeSignature/CodeResources" "$H/RedwoodAudio.driver/_CodeSignature/CodeResources" 644
$F link ../HAL.disabled/VirtualAudio.plugin "$H/VirtualAudio.plugin"
python -I tools/davinci-cmd.py sync
echo "RedwoodAudio.driver + VirtualAudio installed; reboot (kernel 0212 or later, dvhaptic=3 for the motor)"
