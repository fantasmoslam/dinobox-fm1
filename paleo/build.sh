#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build PALEO's offline renderer for this computer: build/paleo/paleo-render
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd); OUT="$ROOT/build/paleo"; mkdir -p "$OUT"
${CXX:-c++} -O2 -std=c++17 -w -pthread -I"$ROOT/paleo" "$ROOT"/paleo/tools/render.cpp "$ROOT"/paleo/engine/paleo.cpp \
  "$ROOT"/paleo/faust/gen/[!he]*.cpp "$ROOT"/paleo/faust/gen/horn.cpp "$ROOT"/paleo/faust/gen/harmonium.cpp -o "$OUT/paleo-render"
${CXX:-c++} -O2 -std=c++17 -w -pthread -I"$ROOT/paleo" "$ROOT"/paleo/tools/play.cpp "$ROOT"/paleo/engine/paleo.cpp \
  "$ROOT"/paleo/faust/gen/[!he]*.cpp "$ROOT"/paleo/faust/gen/horn.cpp "$ROOT"/paleo/faust/gen/harmonium.cpp -o "$OUT/paleo-play" -ldl -lm
