#!/bin/bash
# CyPort (Cyberpunk 2077 2.5.0 Steam Mac build on iOS): build the plugin for iOS, sign it, stage what goes on the phone.
#   macos/ios-stage.sh   -> build-ios/, deploy/Cyberpunk 2077/red4ext/plugins/<Name>/{<Name>.dylib, LICENSE, Scripts/...}
# deploy/Cyberpunk 2077/ has the layout of the phone's Documents/Cyberpunk 2077/. Nothing is copied to a device.
# The phone compiles the scripts of an enabled plugin from red4ext/plugins/<Name>/Scripts.
# Environment: SIGN_ID (codesign identity), RED4EXT_SDK_DIR (RED4ext.SDK-macos, branch mac-next).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="$(sed -n 's/^set(REDMAC_NAME \(.*\))$/\1/p' "$ROOT/macos/CMakeLists.txt")"
SIGN_ID="${SIGN_ID:--}"   # "-" = ad-hoc: iOS loads a plugin from Documents only when it is signed by the app's team
SDK="${RED4EXT_SDK_DIR:-$ROOT/../RED4ext.SDK-macos}"
B="$ROOT/build-ios"
if [ ! -f "$B/build.ninja" ]; then
  cmake -S "$ROOT/macos" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DRED4EXT_SDK_DIR="$SDK"
fi
ninja -C "$B"
python3 -I "$ROOT/macos/hashscan.py" "$B/$NAME.dylib" "$SDK" "$B" | tail -1

D="$ROOT/deploy/Cyberpunk 2077"; P="$D/red4ext/plugins/$NAME"
rm -rf "$ROOT/deploy"; mkdir -p "$P/Scripts"
cp "$B/$NAME.dylib" "$ROOT/LICENSE" "$P/"
codesign -f -s "$SIGN_ID" "$P/$NAME.dylib"
# The script API, without the Test/ folders (they need the RedTest framework).
(cd "$ROOT/scripts/$NAME" && find . -name '*.reds' -not -path './Test/*' -print0 | while IFS= read -r -d '' f; do
   mkdir -p "$P/Scripts/$(dirname "$f")"; cp "$f" "$P/Scripts/$f"; done)
if [ -x "$ROOT/macos/ios-stage-extra.sh" ]; then "$ROOT/macos/ios-stage-extra.sh" "$D"; fi
(cd "$ROOT/deploy" && find . -type f | sort | sed 's#^\./#    #')
codesign -dv "$P/$NAME.dylib" 2>&1 | grep -E "Authority|TeamIdentifier|Identifier=" | sed 's/^/    /'
