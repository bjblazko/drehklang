#!/usr/bin/env bash
# Converts a film into the pair Drehklang's Video menu plays (ADR 0031):
#
#   <title>.avi   picture: Motion-JPEG, at most 320x240, 25 fps
#   <title>.m4a   sound:   AAC-LC, 128 kbit/s, 44.1 kHz stereo
#
# Any file ffmpeg reads works as the source: MKV, MP4/M4V, MOV, AVI, WebM,
# MPEG-TS, a DVD's VOB. Interlaced pictures (DVDs, TV recordings) are
# deinterlaced, non-square pixels made square, and the picture is fitted
# into 320x240 so it shows whole inside the round display -- portrait
# phone videos too. Subtitles and chapters are left out.
#
# The picture stays under 1 GB: past that ffmpeg writes OpenDML, which the
# device refuses. A long film is encoded again at a lower JPEG quality
# until it fits (8 first; higher numbers are smaller and blockier).
#
# Copy both files into /Videos on the SD card.
#
# Usage: convert-video.sh <film> [--title <title>] [--lang <code>]
#                                [--audio <n>] [--out <dir>]
#   --title  the name shown on the device (default: the file's name)
#   --lang   the sound track's language, ISO 639-2 (default: ger); the
#            first track in it is taken, else the first track there is
#   --audio  a sound track by number, counted from 0, instead of --lang
#   --out    where the pair goes (default: the current directory)
# Needs ffmpeg and ffprobe (brew install ffmpeg).
set -euo pipefail

usage() { sed -n 2,27p "$0" | sed 's/^# \{0,1\}//'; exit 1; }

SRC="${1:-}"
[[ -n "$SRC" && -f "$SRC" ]] || usage
shift
TITLE="$(basename "${SRC%.*}")"
LANG_CODE="ger"
AUDIO=""
OUT="."
while [[ $# -gt 0 ]]; do
  case "$1" in
    --title) TITLE="$2"; shift 2 ;;
    --lang) LANG_CODE="$2"; shift 2 ;;
    --audio) AUDIO="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    *) usage ;;
  esac
done
for tool in ffmpeg ffprobe; do
  command -v "$tool" >/dev/null || { echo "$tool is needed (brew install ffmpeg)" >&2; exit 1; }
done
mkdir -p "$OUT"

# One language tag per sound track, in order.
languages=$(ffprobe -v error -select_streams a -show_entries stream_tags=language \
  -of csv=p=0 "$SRC" || true)
[[ -n "$(ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 "$SRC")" ]] ||
  { echo "$SRC has no sound track." >&2; exit 1; }
[[ -n "$(ffprobe -v error -select_streams v -show_entries stream=index -of csv=p=0 "$SRC")" ]] ||
  { echo "$SRC has no picture." >&2; exit 1; }

# German is tagged ger, deu or de, depending on the tool that wrote it.
if [[ -z "$AUDIO" ]]; then
  AUDIO=0
  aliases="$LANG_CODE"
  [[ "$LANG_CODE" == "ger" || "$LANG_CODE" == "deu" ]] && aliases="ger deu de"
  n=0
  while IFS= read -r tag; do
    for alias in $aliases; do
      if [[ "$tag" == "$alias" ]]; then AUDIO=$n; break 2; fi
    done
    n=$((n + 1))
  done <<<"$languages"
fi
echo "Sound: track $AUDIO ($(sed -n "$((AUDIO + 1))p" <<<"$languages" | sed 's/^$/no language tag/'))"

readonly LIMIT=1000000000  # OpenDML starts at 1 GiB; stay clear of it.
readonly FIT="yadif=deint=interlaced,scale=trunc(iw*sar/2)*2:ih,setsar=1,\
scale=w=320:h=240:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=lanczos,setsar=1"
picture="$OUT/$TITLE.avi"
sound="$OUT/$TITLE.m4a"

for q in 8 10 12 14 16 20; do
  echo "Picture, JPEG quality $q ..."
  ffmpeg -v error -stats -y -i "$SRC" -map 0:v:0 -an -sn -dn -map_chapters -1 -map_metadata -1 \
    -vf "$FIT" -c:v mjpeg -q:v "$q" -pix_fmt yuvj420p -r 25 "$picture"
  size=$(stat -f %z "$picture" 2>/dev/null || stat -c %s "$picture")
  if (( size < LIMIT )); then break; fi
  echo "  $((size / 1000000)) MB is over 1 GB, trying smaller."
  rm -f "$picture"
done
[[ -f "$picture" ]] || { echo "Could not fit the picture under 1 GB." >&2; exit 1; }

echo "Sound ..."
ffmpeg -v error -stats -y -i "$SRC" -map "0:a:$AUDIO" -vn -sn -dn -map_chapters -1 -map_metadata -1 \
  -c:a aac -b:a 128k -ar 44100 -ac 2 -movflags +faststart "$sound"

echo "Done: $picture"
echo "      $sound"
echo "Copy both into /Videos on the SD card."
