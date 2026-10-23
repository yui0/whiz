#!/bin/bash
# ---------------------------------------------------------------
# build_wasm.sh — whiz ConnectNet WASM
#
# 前提: emcc が PATH にあること
#   source ~/emsdk/emsdk_env.sh
# 資産: python3 whiz.py export-web  → out/model.wcnn, out/whiz.web.dic
# ---------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if ! command -v emcc >/dev/null 2>&1; then
  EMSDK_ENV="${EMSDK_ENV:-/root/image/emsdk/emsdk_env.sh}"
  if [[ ! -f "$EMSDK_ENV" ]]; then
    echo "error: emcc not found and EMSDK_ENV does not exist: $EMSDK_ENV" >&2
    exit 1
  fi
  # emsdk_env.sh sets PATH plus the other variables needed by emcc/node.
  # shellcheck disable=SC1090
  source "$EMSDK_ENV" >/dev/null
fi

echo "=== whiz WASM build ==="
emcc --version | head -1

assets_match() {
  python3 - <<'PY'
from pathlib import Path
import struct

model = Path("out/model.wcnn")
dic = Path("out/whiz.web.dic")
if not model.is_file() or not dic.is_file():
    raise SystemExit(1)

mh = model.read_bytes()[:40]
dh = dic.read_bytes()[:16]
if len(mh) != 40 or mh[:4] != b"WCNN" or len(dh) != 16 or dh[:4] != b"WUTF":
    raise SystemExit(1)
model_words = struct.unpack_from("<I", mh, 36)[0]
dic_words = struct.unpack_from("<I", dh, 8)[0]
overlays = dic_words - model_words
print(
    f"assets: model={model_words} trained words, "
    f"dictionary={dic_words} words ({overlays} overlays)"
)
# WUTF appends a small index-only functional lexicon after the trained rows.
# Those rows deliberately use dictionary fallback costs in the runtime.
raise SystemExit(0 if 0 <= overlays <= 64 else 1)
PY
}

if ! assets_match; then
  echo "web assets are missing or inconsistent; exporting a matched pair…"
  python3 whiz.py export-web
fi

# Do not produce a browser bundle that can only fail later in whiz_init().
assets_match || {
  echo "error: model.wcnn and whiz.web.dic have inconsistent vocabularies" >&2
  exit 1
}

emcc whiz.c \
  -O2 \
  -D__EMSCRIPTEN__ \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=134217728 \
  -s MAXIMUM_MEMORY=1073741824 \
  -s STACK_SIZE=1048576 \
  -s EXPORTED_FUNCTIONS='["_whiz_init","_whiz_convert","_whiz_reverse","_whiz_predict","_whiz_is_ready","_whiz_set_context","_whiz_free","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8","HEAPU8"]' \
  -s MODULARIZE=0 \
  -s ENVIRONMENT='web,worker,node' \
  -lm \
  -o whiz.js

cp -f out/model.wcnn model.wcnn
cp -f out/whiz.web.dic whiz.web.dic

echo ""
echo "=== ビルド成功 ==="
ls -lh whiz.js whiz.wasm model.wcnn whiz.web.dic
echo ""
echo "  python3 -m http.server 8080"
echo "  http://localhost:8080/whiz.html"
