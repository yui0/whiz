# whiz — かな漢字 LLM（学習 → C/WASM/IM 推論）

ひらがな⇔漢字の小型 BitNet LLM と、それを使う推論フロントエンド一式です。

```
engine/         辞書格子 + 3 段学習コストモデル（現行のかな漢字変換）
                  whiz.py         … 学習・変換・書き出し
                  whiz.c          … C / WASM 推論
kana2kanji/     Python 学習・GGUF 書き出し
llm/            C 推論コア
                  catseye_llm.h   … 汎用 GGUF / transformer
                  catseye_kana.h  … かな漢字 API（専用）
                  llama2.c        … CLI
im-wayland.c    Luna Desktop / Wayland 入力メソッド（推奨）
im.c            GTK 入力メソッド（互換用）
kana2kanji_*.{c,html,js}   ブラウザ WASM
```

## engine — 3 段コストモデル

入力メソッドが実際に呼ぶ変換器。`dic/` の 4 つの辞書（単語・接続コスト・
活用形・品詞）を教師に、3 つの段を学習する。

| 段 | ネットワーク | 役割 |
|----|--------------|------|
| stage1 | MLP | 活用形で語尾を変化させつつ最長一致法で切り出した**単語のコスト** |
| stage2 | MLP | 左右の単語の**接続コスト** |
| stage3 | Transformer | 文全体での**単語と単語のつながり** |

stage1+stage2 のビーム Viterbi で N-best を出し、stage3 で並べ替える。
詳細と学習手順は [`engine/README.md`](engine/README.md)。

## いちばん短い手順

```bash
cd apps/whiz

# 1) ローカルで数分学習（内蔵サンプル）
make smoke-train

# 2) 成果物を C/WASM が見る場所へ配置
make install-model
#   → llm/models/model_best.gguf
#   → ./model_best.gguf

# 3) CLI で確認
make llama2
make test
```

本番精度が欲しければ Colab で `kana2kanji/kana2kanji_colab.ipynb`（`--preset base`）→ ダウンロードした `model_best.gguf` を同じ場所へ置きます。

## 役割ごとの使い方

### 1. 学習（Python）

| 用途 | コマンド |
|------|----------|
| ローカル数分テスト | `cd kana2kanji && python3 kana2kanji.py smoke` |
| 配管だけ (~30秒) | `python3 kana2kanji.py all --preset quick` |
| Colab 本番 | `data corpus.txt` → `train --preset base` |

成果物（重要）: `kana2kanji/out/model_best.gguf`
（語彙は GGUF 内包。`.bin` + `tokenizer.bin` も同梱）

### 2. CLI 推論（llama2.c）

```bash
make -C llm
make install-model

# かんたん（プロンプトを自動で <h2k>…<sep> に包む）
./llm/llama2 -m llm/models/model_best.gguf -a kana -t 0 -n 32 -p "とうきょう"

# 生プロンプト
./llm/llama2 -m llm/models/model_best.gguf -t 0 -n 32 -p "<h2k>とうきょう<sep>"
./llm/llama2 -m llm/models/model_best.gguf -t 0 -n 32 -p "<k2h>東京<sep>"

# 従来 .bin
./llm/llama2 -c llm/models/model_best.bin -z llm/models/tokenizer.bin \
  -t 0 -n 32 -p "<h2k>とうきょう<sep>"
```

`-t 0` は greedy（決定的）。かな漢字ではこれを推奨。

### 3. ブラウザ（WASM）

```bash
# emsdk を有効化してから
source ~/emsdk/emsdk_env.sh   # 環境に応じて
make wasm

python3 -m http.server 8080
# http://localhost:8080/kana2kanji.html
# 「モデルを読み込む」→ ./model_best.gguf
```

必要なファイル: `kana2kanji.html` / `kana2kanji.js` / `kana2kanji.wasm` / `model_best.gguf`

### 4. Luna Desktop / Wayland IM（推奨）

GTK/Qt/GLFWを使わず、Waylandの標準 `input-method-v2` とLuna UIの
OpenGL候補画面で動作します。Luna compositor側は `text-input-v3` を
アプリへ公開し、preedit/commitを中継します。

```bash
# Fedora系
sudo dnf install wayland-devel mesa-libEGL-devel mesa-libGL-devel \
  libxkbcommon-devel

# Debian/Ubuntu系
sudo apt install libwayland-dev libegl-dev libgl-dev libxkbcommon-dev

make im-wayland
make test-ime
WHIZ_MODEL=$PWD/engine/out/model.wcnn \
WHIZ_DIC=$PWD/engine/out/whiz.web.dic \
./whiz-im-wayland
```

Luna Desktopへインストールすると `luna-session` が自動起動します。

```bash
sudo make install PREFIX=/usr/local
# 一時的に無効化
LUNA_INPUT_METHOD=none luna-session
```

操作:

| キー | 動作 |
|------|------|
| `Shift+Space` / `Ctrl+Space` / 半角全角 | 日本語モード切替 |
| 選択文字列 + `変換` | 漢字を読みに戻して再変換 |
| `Space` | かな漢字変換／選択中の文節の次候補 |
| `Tab` / `↑` / `↓` | 選択中の文節の候補切替 |
| `←` / `→` | 前後の文節へ移動 |
| `Shift+←` / `Shift+→` | 文節境界を縮小／拡大 |
| `1`〜`9`, `0` | Wayland版の変換中候補を直接確定 |
| `Enter` | 選択候補または読みを確定 |
| `Esc` | 変換を読みに戻す、もう一度でpreedit取消 |

日本語IMEが処理しないショートカットやナビゲーションキーは
`virtual-keyboard-v1`経由で対象アプリへ転送されるため、`Ctrl+C`なども
通常どおり利用できます。未入力時は小さなモード表示だけになり、入力を
始めると候補パネルへ展開します。

UIファイルの探索順は `WHIZ_UI_DIR`、ビルド時ソースディレクトリ、
`/usr/share/whiz`、`/usr/local/share/whiz` です。日本語フォントは
`LUNA_FONT_CJK=/path/to/font.ttf` で明示できます。

### 5. GTK IM（互換用、im.c）

```bash
make im
# モデル探索順: WHIZ_MODEL → models/model_best.gguf → llm/models/… → kana2kanji/out/…
export WHIZ_MODEL=$PWD/llm/models/model_best.gguf
```

Space で文節変換し、Space/Tab/上下で候補、左右で文節を切り替えます。
`Shift+←/→` では文節境界も修正できます（GTK モジュール登録は環境依存）。

## モデル配置ルール

| 置き場所 | 誰が読むか |
|----------|------------|
| `kana2kanji/out/model_best.gguf` | 学習の出力（ソース・オブ・トゥルース） |
| `llm/models/model_best.gguf` | `llama2` / `im.c` 既定 |
| `./model_best.gguf` | WASM HTML の既定 URL |

`make install-model` が上 2 つへコピーします。上書きパスは `WHIZ_MODEL` でも指定可。

## プロンプト形式

学習・推論とも同じタグです（C 側は longest-match でトークン化）。

| 方向 | プロンプト |
|------|------------|
| かな→漢字 | `<h2k>よみ<sep>` |
| 漢字→かな | `<k2h>漢字<sep>` |

## トラブルシュート

- **CLI が `<pad>` ばかり** → `-Ofast` ではなく `llm/Makefile` 既定の `-O3` でビルド（Flash Attention と fast-math の相性）
- **モデル無し** → `make smoke-train && make install-model`
- **WASM で load 失敗** → `model_best.gguf` を HTML と同じディレクトリに置く / CORS のないローカルサーバを使う
- **Wayland IM が起動しない** → Luna compositorのログで`zwp_input_method_manager_v2`を確認し、`WHIZ_MODEL` / `WHIZ_DIC`を絶対パスで指定
- **GTK IM が動かない** → `WHIZ_MODEL`を絶対パスで指定し、GTK IMモジュールキャッシュを更新

## ディレクトリ早見

```
apps/whiz/
├── README.md              ← このファイル
├── Makefile               ← smoke-train / install-model / test / wasm
├── im-wayland.c           ← Luna Desktop / Wayland IM
├── im-wayland.{html,css}  ← Luna UI候補画面
├── im.c                   ← GTK IM（互換用）
├── kana2kanji_wasm.c      ← WASM エントリ
├── kana2kanji.html        ← ブラウザ UI
├── build_wasm.sh
├── kana2kanji/            ← 学習
│   ├── kana2kanji.py
│   ├── kana2kanji_colab.ipynb
│   └── out/model_best.gguf
└── llm/
    ├── llama2.c
    ├── catseye_llm.h      ← 汎用 GGUF / BitNet b1.58
    ├── catseye_kana.h     ← cats_kana2kanji() など専用 API
    └── models/model_best.gguf   ← make install-model 後
```

### C から使う例

```c
#include "catseye_llm.h"
#include "catseye_kana.h"

cats_llm_model m;
cats_kana2kanji_load(&m, NULL);          // WHIZ_MODEL / 既定パス
char out[512];
cats_kana2kanji(&m, "とうきょう", out, sizeof(out));
cats_kanji2kana(&m, "東京", out, sizeof(out));
```
