#!/bin/sh
# On-phone tools from ios-flame (MIT), for iOS 16 arm64 with Xcode's iOS SDK, signed ad hoc
# (with NAME-ents.plist when there is one). Output: phone/tools/out/.
# There is no shell on davinci: run one as a launchd job,
#   MSYS_NO_PATHCONV=1 tools/davinci-launchd-job.py add NAME /var/local/bin/NAME ARGS...
# (output in /var/local/NAME.out). Usage is at the top of each source:
#   psx      processes without /proc; psx -k NAME kills     pstack   PID: thread backtraces
#   openapp  BUNDLE_ID [URL]: launch an app                   lsreg    LaunchServices registration (lsreg -r: rebuild)
#   netsvc   SystemConfiguration network service for an IF    nettest  DNS, TCP, then HTTPS
#   oslog    stream the unified log                           prefs    CFPreferences read/write (no defaults)
#   imgtest  does ImageIO decode real pixels                  membw    CPU memory bandwidth
#   expt, exq: probes of ExtensionFoundation (extension points and records)
#   delayrun SECS PROGRAM [ARG...]: run once, SECS seconds after boot (ours, not flame's)
#   runq     [DIR]: runs NAME.cmd files dropped in /var/local/runq (tools/davinci-run.py; ours)
#   bright   [LEVEL]: display brightness through BackBoardServices, like the slider (ours)
set -eu
cd "$(dirname "$0")"
CC="xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0"
mkdir -p out
build() {	# NAME SOURCE [FLAGS...]
	name=$1 src=$2
	shift 2
	$CC -O2 -Wall "$@" -o "out/$name" "$src"
	if [ -f "$name-ents.plist" ]; then
		codesign -f -s - --entitlements "$name-ents.plist" "out/$name"
	else
		codesign -f -s - "out/$name"
	fi
}
for c in delayrun membw pstack psx runq; do build $c $c.c; done
for m in bright expt exq nettest openapp oslog prefs; do build $m $m.m -fobjc-arc -framework Foundation; done
build imgtest imgtest.m -fobjc-arc -framework Foundation -framework ImageIO -framework CoreGraphics
build lsreg lsreg.m -fobjc-arc -framework Foundation -framework MobileCoreServices
build netsvc netsvc.m -fobjc-arc -framework Foundation -framework SystemConfiguration
ls -l out
