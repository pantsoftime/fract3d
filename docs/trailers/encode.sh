#!/bin/bash
# encode.sh IN OUT KBPS WxH : two-pass H.264 for sharing / the web
set -e
in=$1; out=$2; br=$3; size=$4
common=(-vf "scale=${size}:flags=lanczos" -c:v libx264 -preset slow -b:v ${br}k -maxrate $((br*2))k -bufsize $((br*4))k -passlogfile /tmp/fract3d-p2x-$$)
ffmpeg -loglevel error -y -i "$in" "${common[@]}" -pass 1 -an -f mp4 /dev/null
ffmpeg -loglevel error -y -i "$in" "${common[@]}" -pass 2 -profile:v high -pix_fmt yuv420p -movflags +faststart -an "$out"
rm -f /tmp/fract3d-p2x-$$*
