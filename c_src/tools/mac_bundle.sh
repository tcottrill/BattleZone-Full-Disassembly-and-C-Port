#!/bin/sh
# Assemble build/mac/Battlezone.app from bz_mac (run via `make -f Makefile.mac app`).
set -e
OUT=${1:-build/mac}
APP="$OUT/Battlezone.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/samples" "$APP/Contents/Frameworks"
cp "$OUT/bz_mac" "$APP/Contents/MacOS/Battlezone"
cp samples/*.WAV "$APP/Contents/Resources/samples/"
cat > "$APP/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>Battlezone</string>
<key>CFBundleDisplayName</key><string>Battlezone</string>
<key>CFBundleIdentifier</key><string>local.battlezone.cport</string>
<key>CFBundleExecutable</key><string>Battlezone</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleShortVersionString</key><string>1.0</string>
<key>LSMinimumSystemVersion</key><string>11.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>LSApplicationCategoryType</key><string>public.app-category.games</string>
</dict></plist>
PL
# bundle libSDL2 and repoint the binary at it
LIB=$(otool -L "$APP/Contents/MacOS/Battlezone" | awk '/libSDL2.*dylib/ {print $1; exit}')
if [ -n "$LIB" ]; then
  cp -L "$LIB" "$APP/Contents/Frameworks/libSDL2.dylib"
  chmod u+w "$APP/Contents/Frameworks/libSDL2.dylib"
  install_name_tool -id @rpath/libSDL2.dylib "$APP/Contents/Frameworks/libSDL2.dylib"
  install_name_tool -change "$LIB" @executable_path/../Frameworks/libSDL2.dylib "$APP/Contents/MacOS/Battlezone"
fi
# Homebrew's sdl2 is sdl2-compat, a shim that dlopens libSDL3.dylib from its own
# folder at load time (no libSDL3 there = a modal error dialog and a hung launch)
if strings "$APP/Contents/Frameworks/libSDL2.dylib" | grep -q '@loader_path/libSDL3.dylib'; then
  SDL3=$(brew --prefix sdl3)/lib/libSDL3.dylib
  cp -L "$SDL3" "$APP/Contents/Frameworks/libSDL3.dylib"
  chmod u+w "$APP/Contents/Frameworks/libSDL3.dylib"
  install_name_tool -id @rpath/libSDL3.dylib "$APP/Contents/Frameworks/libSDL3.dylib" 2>/dev/null
fi
codesign --force --deep -s - "$APP" 2>/dev/null || true
echo "built $APP"
