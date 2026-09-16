#!/usr/bin/env bash
# Host conformance test for the Prospector encoder.
# No cross toolchain needed: the encoder is deliberately free of QMK and
# hardware dependencies.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QMK="${QMK_HOME:-$HOME/qmk_firmware}"
SRC="$QMK/keyboards/keychron/common/prospector"

cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I"$HERE" \
   -I"$SRC" \
   "$HERE/prospector_conformance.c" \
   "$SRC/prospector_status.c" \
   -o "${TMPDIR:-/tmp}/prospector_conformance"

"${TMPDIR:-/tmp}/prospector_conformance"
