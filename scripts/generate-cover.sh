#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_FILE="${1:-$ROOT_DIR/docs/assets/oosh-cover.png}"
TMP_DIR="$(mktemp -d)"
BASE="$TMP_DIR/base.png"
GLOW="$TMP_DIR/glow.png"
SHADOW="$TMP_DIR/shadow.png"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

TITLE_FONT="/System/Library/Fonts/Monaco.ttf"
CODE_FONT="/System/Library/Fonts/Menlo.ttc"

if [[ ! -f "$TITLE_FONT" ]]; then
  TITLE_FONT="$CODE_FONT"
fi

if [[ ! -f "$CODE_FONT" ]]; then
  CODE_FONT="$TITLE_FONT"
fi

mkdir -p "$(dirname "$OUT_FILE")"

magick -size 1280x640 gradient:'#040c14-#0b1624' "$BASE"

magick -size 1280x640 xc:none \
  -fill 'rgba(0,214,255,0.04)' -draw 'circle 185,125 320,125' \
  -fill 'rgba(255,131,44,0.04)' -draw 'circle 1110,120 1245,120' \
  -fill 'rgba(120,255,198,0.03)' -draw 'circle 700,610 860,610' \
  -blur 0x120 \
  "$GLOW"

magick "$BASE" "$GLOW" -compose screen -composite "$BASE"

magick "$BASE" \
  -fill 'rgba(0,0,0,0.34)' -draw 'rectangle 0,0 1280,640' \
  "$BASE"

magick -size 1280x640 xc:none \
  -fill 'rgba(0,0,0,0.60)' \
  -draw 'roundrectangle 192,101 1108,523 30,30' \
  -blur 0x28 \
  "$SHADOW"

magick "$BASE" "$SHADOW" -compose over -composite "$BASE"

cmd=(magick "$BASE" -gravity NorthWest)

for x in $(seq 0 80 1280); do
  cmd+=(-draw "stroke rgba(103,166,190,0.04) stroke-width 1 line $x,0 $x,640")
done

for y in $(seq 0 80 640); do
  cmd+=(-draw "stroke rgba(103,166,190,0.03) stroke-width 1 line 0,$y 1280,$y")
done

cmd+=(
  -draw "stroke rgba(255,156,79,0.08) stroke-width 2 line 880,0 1280,270"
  -draw "stroke rgba(0,214,255,0.07) stroke-width 2 line 0,470 390,230"
  -draw "fill rgba(8,14,24,0.90) stroke rgba(132,235,255,0.26) stroke-width 2 roundrectangle 180,90 1095,510 28,28"
  -draw "fill rgba(15,27,40,0.98) stroke rgba(0,0,0,0) roundrectangle 180,90 1095,136 28,28"
  -draw "fill rgba(255,107,107,0.95) circle 218,113 226,113"
  -draw "fill rgba(255,205,84,0.95) circle 246,113 254,113"
  -draw "fill rgba(83,211,125,0.95) circle 274,113 282,113"
  -draw "fill rgba(9,19,30,0.84) stroke rgba(126,232,255,0.26) stroke-width 1.5 roundrectangle 78,124 300,194 18,18"
  -draw "fill rgba(9,19,30,0.84) stroke rgba(255,172,96,0.28) stroke-width 1.5 roundrectangle 973,118 1198,196 18,18"
  -draw "fill rgba(9,19,30,0.84) stroke rgba(126,232,255,0.26) stroke-width 1.5 roundrectangle 78,390 292,470 18,18"
  -draw "fill rgba(9,19,30,0.84) stroke rgba(255,172,96,0.28) stroke-width 1.5 roundrectangle 947,454 1198,536 18,18"
  -draw "stroke rgba(126,232,255,0.20) stroke-width 2 line 300,160 180,182"
  -draw "stroke rgba(255,172,96,0.20) stroke-width 2 line 973,158 1095,185"
  -draw "stroke rgba(126,232,255,0.20) stroke-width 2 line 292,430 180,420"
  -draw "stroke rgba(255,172,96,0.20) stroke-width 2 line 947,492 1095,442"
  -font "$TITLE_FONT" -pointsize 60 -fill '#f4fbff' -annotate +76+528 'oosh'
  -font "$CODE_FONT" -pointsize 21 -fill '#92ebff' -annotate +86+566 'OBJECT-ORIENTED SHELL IN C'
  -font "$CODE_FONT" -pointsize 16 -fill '#ffb46f' -annotate +88+596 'objects  blocks  plugins  pipelines  cross-platform'
  -font "$CODE_FONT" -pointsize 16 -fill '#7fe8ff' -annotate +98+148 'OBJECT FILE'
  -font "$CODE_FONT" -pointsize 12 -fill '#dbeaf5' -annotate +98+170 'methods and properties'
  -font "$CODE_FONT" -pointsize 16 -fill '#ffb56c' -annotate +994+145 'EXTENSION PLUGIN'
  -font "$CODE_FONT" -pointsize 12 -fill '#dbeaf5' -annotate +994+168 'commands, stages, resolvers'
  -font "$CODE_FONT" -pointsize 16 -fill '#7fe8ff' -annotate +98+414 'PIPELINE BLOCK'
  -font "$CODE_FONT" -pointsize 12 -fill '#dbeaf5' -annotate +98+437 'each  where  reduce  local'
  -font "$CODE_FONT" -pointsize 16 -fill '#ffb56c' -annotate +968+478 'DATA JSON'
  -font "$CODE_FONT" -pointsize 12 -fill '#dbeaf5' -annotate +968+501 'maps, lists, strings, numbers'
  -font "$CODE_FONT" -pointsize 18 -fill '#87dbff' -annotate +224+118 '[default] nicolo@oosh | /workspace'
  -font "$CODE_FONT" -pointsize 19 -fill '#7fe8ff' -annotate +224+177 'let files = . -> children()'
  -font "$CODE_FONT" -pointsize 19 -fill '#e6f5ff' -annotate +224+214 'files |> where([:it | it -> type == \"file\"])'
  -font "$CODE_FONT" -pointsize 19 -fill '#ffb56c' -annotate +224+251 '     |> each([:it | local name = it -> name ; name])'
  -font "$CODE_FONT" -pointsize 19 -fill '#87ffcb' -annotate +224+288 'plugin load build/oosh_sample_plugin.dylib'
  -font "$CODE_FONT" -pointsize 19 -fill '#e6f5ff' -annotate +224+325 'env() -> HOME    shell() -> plugins    proc() -> pid'
  -font "$CODE_FONT" -pointsize 19 -fill '#ffb56c' -annotate +224+362 'class Artifact extends Named, Printable do ... endclass'
  -font "$CODE_FONT" -pointsize 19 -fill '#87dbff' -annotate +224+399 'text(\"$(pwd)\") -> print()'
  -font "$CODE_FONT" -pointsize 18 -fill '#9eb5c5' -annotate +224+456 '> epic cover generated for the repo'
  -draw "fill rgba(8,18,28,0.94) stroke rgba(126,232,255,0.18) stroke-width 1.2 roundrectangle 842,556 944,594 18,18"
  -draw "fill rgba(8,18,28,0.94) stroke rgba(126,232,255,0.18) stroke-width 1.2 roundrectangle 956,556 1068,594 18,18"
  -draw "fill rgba(8,18,28,0.94) stroke rgba(126,232,255,0.18) stroke-width 1.2 roundrectangle 1080,556 1206,594 18,18"
  -font "$CODE_FONT" -pointsize 15 -fill '#dbeaf5' -annotate +870+581 'linux'
  -font "$CODE_FONT" -pointsize 15 -fill '#dbeaf5' -annotate +985+581 'macos'
  -font "$CODE_FONT" -pointsize 15 -fill '#dbeaf5' -annotate +1107+581 'windows'
)

"${cmd[@]}" "$OUT_FILE"

magick "$OUT_FILE" -resize 1280x640\! "$OUT_FILE"
magick identify -format '%wx%h\n' "$OUT_FILE"
