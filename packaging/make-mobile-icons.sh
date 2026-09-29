#!/usr/bin/env bash
# Launcher icons for Android (res/mipmap-*) and iPad (Assets.xcassets) from lumen-notes.svg.
# Needs ImageMagick. Re-run after changing the SVG; the PNGs it writes are committed.
set -euo pipefail
cd "$(dirname "$0")"
svg=lumen-notes.svg

for pair in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
  dpi=${pair%%:*} px=${pair##*:}
  mkdir -p "android/res/mipmap-$dpi"
  magick -background none -density 600 "$svg" -resize "${px}x${px}" -depth 8 "android/res/mipmap-$dpi/ic_launcher.png"
done

# iOS masks the icon itself and shows transparency as black: cut the rounded corners off and fill
# the square with the icon's own two blues (top half lighter, as in the SVG).
set_dir=ios/Assets.xcassets/AppIcon.appiconset
mkdir -p "$set_dir"
magick -size 1024x512 xc:'rgb(59,99,230)' -size 1024x512 xc:'rgb(47,85,212)' -append \
  \( -background none -density 600 "$svg" -resize 1180x1180 -gravity center -extent 1024x1024 \) \
  -composite -alpha off +repage -depth 8 "$set_dir/icon-1024.png"
cat > "$set_dir/Contents.json" <<'JSON'
{
  "images" : [ { "filename" : "icon-1024.png", "idiom" : "universal", "platform" : "ios", "size" : "1024x1024" } ],
  "info" : { "author" : "xcode", "version" : 1 }
}
JSON
cat > ios/Assets.xcassets/Contents.json <<'JSON'
{ "info" : { "author" : "xcode", "version" : 1 } }
JSON
