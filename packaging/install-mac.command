#!/bin/bash
# Installs artempidor (VST3 + AU) for the current user.
# Double-click this file. If macOS refuses: right-click -> Open -> Open.
cd "$(dirname "$0")" || exit 1

VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3" "$AU"

for p in *.vst3; do
  [ -e "$p" ] || continue
  rm -rf "$VST3/$p" && cp -R "$p" "$VST3/" && xattr -dr com.apple.quarantine "$VST3/$p" 2>/dev/null
  echo "VST3 installed: $p"
done
for p in *.component; do
  [ -e "$p" ] || continue
  rm -rf "$AU/$p" && cp -R "$p" "$AU/" && xattr -dr com.apple.quarantine "$AU/$p" 2>/dev/null
  echo "AU installed:   $p"
done

killall -9 AudioComponentRegistrar 2>/dev/null
echo
echo "Done! Restart your DAW and rescan plugins."
echo "Готово! Перезапустите DAW и пересканируйте плагины."
