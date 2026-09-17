#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

fail=0
for f in $(grep -Rl --include='*.cpp' 'BuildFeatures::' "$ROOT/src"); do
    if ! grep -Eq '^[[:space:]]*#include[[:space:]]+"buildfeatures\.h"' "$f"; then
        echo "FAIL: ${f#$ROOT/} uses BuildFeatures:: without #include \"buildfeatures.h\"" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi

grep -q '#include "buildfeatures.h"' "$ROOT/src/mainwindow.cpp"
grep -q 'BuildFeatures::Oscar' "$ROOT/src/mainwindow.cpp"
grep -q 'BuildFeatures::protocolEnabled' "$ROOT/src/mainwindow.cpp"

echo "PASS: BuildFeatures declarations are visible in all C++ users"
