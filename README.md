# whiz — Kana–Kanji LLM (Training → C/WASM/Input Method Inference)

**English** | [日本語](README.ja.md)

A compact BitNet LLM for bidirectional hiragana–kanji conversion, together with its inference frontends.

```text
engine/         Dictionary lattice + three-stage learned cost model (current kana–kanji converter)
                  whiz.py         ... training, conversion, and export
                  whiz.c          ... C / WASM inference
kana2kanji/     Python training and GGUF export
llm/            C inference core
                  catseye_llm.h   ... generic GGUF / transformer support
                  catseye_kana.h  ... dedicated kana–kanji API
                  llama2.c        ... CLI
im-wayland.c    Luna Desktop / Wayland input method (recommended)
im.c            GTK input method (compatibility)
kana2kanji_*.{c,html,js}   Browser-based WASM
```

## engine — Three-stage cost model

This is the converter used by the input method. It learns three stages from the four dictionaries in `dic/` (words, connection costs, conjugation forms, and parts of speech).

| Stage | Network | Purpose |
|-------|---------|---------|
| stage1 | MLP | **Word costs** for words segmented using longest-prefix matching, accounting for inflectional endings |
| stage2 | MLP | **Connection costs** between adjacent words |
| stage3 | Transformer | **Relationships between words** in the context of the entire sentence |

Beam-search Viterbi decoding with stage1 + stage2 produces N-best candidates, which stage3 then reranks.

See [`engine/README.md`](engine/README.md) for details and training instructions.

## Quick start

```bash
cd apps/whiz

# 1) Train locally in a few minutes using the bundled sample
make smoke-train

# 2) Install the outputs where the C/WASM frontends expect them
make install-model
#   → llm/models/model_best.gguf
#   → ./model_best.gguf

# 3) Check the CLI
make llama2
make test
```

For production-quality accuracy, run `kana2kanji/kana2kanji_colab.ipynb` in Colab (with `--preset base`), then place the downloaded `model_best.gguf` in the same location.

## Usage by component

### 1. Training (Python)

| Purpose | Command |
|---------|---------|
| Local test (a few minutes) | `cd kana2kanji && python3 kana2kanji.py smoke` |
| Pipeline-only test (~30 seconds) | `python3 kana2kanji.py all --preset quick` |
| Production training in Colab | `data corpus.txt` → `train --preset base` |

Important output: `kana2kanji/out/model_best.gguf`

The vocabulary is embedded in the GGUF file. `.bin` and `tokenizer.bin` files are also included.

### 2. CLI inference (llama2.c)

```bash
make -C llm
make install-model

# Simple mode (automatically wraps the prompt in <h2k>…<sep>)
./llm/llama2 -m llm/models/model_best.gguf -a kana -t 0 -n 32 -p "とうきょう"

# Raw prompts
./llm/llama2 -m llm/models/model_best.gguf -t 0 -n 32 -p "<h2k>とうきょう<sep>"
./llm/llama2 -m llm/models/model_best.gguf -t 0 -n 32 -p "<k2h>東京<sep>"

# Legacy .bin format
./llm/llama2 -c llm/models/model_best.bin -z llm/models/tokenizer.bin \
  -t 0 -n 32 -p "<h2k>とうきょう<sep>"
```

`-t 0` enables deterministic greedy decoding, which is recommended for kana–kanji conversion.

### 3. Browser (WASM)

```bash
# Activate emsdk first (adjust the path to your environment)
source ~/emsdk/emsdk_env.sh
make wasm

python3 -m http.server 8080
# http://localhost:8080/kana2kanji.html
# Click "モデルを読み込む" (Load Model), then select ./model_best.gguf
```

Required files: `kana2kanji.html` / `kana2kanji.js` / `kana2kanji.wasm` / `model_best.gguf`

### 4. Luna Desktop / Wayland IM (recommended)

This input method uses the standard Wayland `input-method-v2` protocol and a Luna UI OpenGL candidate panel, without relying on GTK, Qt, or GLFW. The Luna compositor exposes `text-input-v3` to applications and forwards preedit/commit events.

```bash
# Fedora-based distributions
sudo dnf install wayland-devel mesa-libEGL-devel mesa-libGL-devel \
  libxkbcommon-devel

# Debian/Ubuntu-based distributions
sudo apt install libwayland-dev libegl-dev libgl-dev libxkbcommon-dev

make im-wayland
make test-ime
WHIZ_MODEL=$PWD/engine/out/model.wcnn \
WHIZ_DIC=$PWD/engine/out/whiz.web.dic \
./whiz-im-wayland
```

After installation on Luna Desktop, `luna-session` starts it automatically.

```bash
sudo make install PREFIX=/usr/local
# Temporarily disable it
LUNA_INPUT_METHOD=none luna-session
```

Key bindings:

| Key | Action |
|-----|--------|
| `Shift+Space` / `Ctrl+Space` / Hankaku/Zenkaku | Toggle Japanese input mode |
| Selected text + Henkan (Convert) key | Convert selected kanji back to their reading and reconvert |
| `Space` | Convert kana to kanji / select the next candidate for the active segment |
| `Tab` / `↑` / `↓` | Switch candidates for the active segment |
| `←` / `→` | Move to the previous/next segment |
| `Shift+←` / `Shift+→` | Shrink/expand the segment boundary |
| `1`–`9`, `0` | Directly commit a candidate during conversion in the Wayland version |
| `Enter` | Commit the selected candidate or reading |
| `Esc` | Restore the reading; press again to cancel preedit |

Shortcuts and navigation keys that the Japanese IME does not handle are forwarded to the target application through `virtual-keyboard-v1`, so shortcuts such as `Ctrl+C` continue to work normally. When idle, the UI displays only a small mode indicator; when typing starts, it expands into a candidate panel.

UI files are searched in this order: `WHIZ_UI_DIR`, the build-time source directory, `/usr/share/whiz`, and `/usr/local/share/whiz`. You can explicitly specify a Japanese font with `LUNA_FONT_CJK=/path/to/font.ttf`.

### 5. GTK IM (compatibility, im.c)

```bash
make im
# Model search order: WHIZ_MODEL → models/model_best.gguf → llm/models/… → kana2kanji/out/…
export WHIZ_MODEL=$PWD/llm/models/model_best.gguf
```

Use Space to convert segments, Space/Tab/Up/Down to switch candidates, and Left/Right to move between segments. `Shift+←/→` also adjusts segment boundaries. GTK module registration depends on the environment.

## Model locations

| Location | Used by |
|----------|---------|
| `kana2kanji/out/model_best.gguf` | Training output (source of truth) |
| `llm/models/model_best.gguf` | Default for `llama2` / `im.c` |
| `./model_best.gguf` | Default URL for the WASM HTML frontend |

`make install-model` copies the output to the two inference locations. You can override the model path using `WHIZ_MODEL`.

## Prompt format

Training and inference use the same tags. The C implementation tokenizes using longest-match tokenization.

| Direction | Prompt |
|-----------|--------|
| Kana → kanji | `<h2k>よみ<sep>` |
| Kanji → kana | `<k2h>漢字<sep>` |

## Troubleshooting

- **CLI outputs only `<pad>`** → Build with the default `-O3` in `llm/Makefile` instead of `-Ofast` (an interaction between Flash Attention and fast-math).
- **No model found** → Run `make smoke-train && make install-model`.
- **WASM model loading fails** → Put `model_best.gguf` in the same directory as the HTML file and serve it using a local server without CORS issues.
- **Wayland IM does not start** → Check the Luna compositor logs for `zwp_input_method_manager_v2` and set `WHIZ_MODEL` / `WHIZ_DIC` to absolute paths.
- **GTK IM does not work** → Set `WHIZ_MODEL` to an absolute path and refresh the GTK IM module cache.

## Directory overview

```text
apps/whiz/
├── README.md              ← this file (English)
├── README.ja.md           ← Japanese documentation
├── Makefile               ← smoke-train / install-model / test / wasm
├── im-wayland.c           ← Luna Desktop / Wayland IM
├── im-wayland.{html,css}  ← Luna UI candidate panel
├── im.c                   ← GTK IM (compatibility)
├── kana2kanji_wasm.c      ← WASM entry point
├── kana2kanji.html        ← browser UI
├── build_wasm.sh
├── kana2kanji/            ← training
│   ├── kana2kanji.py
│   ├── kana2kanji_colab.ipynb
│   └── out/model_best.gguf
└── llm/
    ├── llama2.c
    ├── catseye_llm.h      ← generic GGUF / BitNet b1.58
    ├── catseye_kana.h     ← dedicated API, including cats_kana2kanji()
    └── models/model_best.gguf   ← after make install-model
```

### Example: calling from C

```c
#include "catseye_llm.h"
#include "catseye_kana.h"

cats_llm_model m;
cats_kana2kanji_load(&m, NULL);          // WHIZ_MODEL / default paths
char out[512];
cats_kana2kanji(&m, "とうきょう", out, sizeof(out));
cats_kanji2kana(&m, "東京", out, sizeof(out));
```
