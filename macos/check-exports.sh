#!/bin/bash
# After the link: the three RED4ext entry points and <plugin>_registerNatives are exported, and nothing is looked up dynamically
# (the dylib is linked without -undefined dynamic_lookup: game functions are reached by address, never by name).
# Usage: check-exports.sh <dylib> <registerNatives name>
set -eu
DYLIB="$1"
for sym in _Main _Query _Supports "_$2"; do
  nm -gU "$DYLIB" | grep -q " T $sym\$" || { echo "check-exports: FAILED. export $sym is missing" >&2; exit 1; }
done
BAD=$(nm -m -u "$DYLIB" | grep "dynamically looked up" || true)
if [[ -n "$BAD" ]]; then
  echo "check-exports: FAILED. Symbols looked up dynamically at load:" >&2; echo "$BAD" | c++filt >&2; exit 1
fi
echo "check-exports: ok ($(nm -gU "$DYLIB" | grep -c ' T ') exported functions)"
