# 🍓 **Whiz** — The Future of Japanese Input

**Next-generation Japanese IME powered by lightweight neural networks**

[![Sponsor](https://img.shields.io/badge/Sponsor%20this%20project-%E2%9D%A4%EF%B8%8F-white?logo=githubsponsors&logoColor=EA4AAA&labelColor=EA4AAA)](https://github.com/sponsors/yui0)

![GTK](https://img.shields.io/badge/GTK-4%20%7C%203-brightgreen)
![License](https://img.shields.io/badge/License-MPL_2.0-blue)
![Japanese](https://img.shields.io/badge/Language-Japanese-red)

**"Faster. Smoother. Smarter."**
A modern Japanese Input Method Editor featuring **ConnectNet** a unified neural model for superior kana-to-kanji conversion.

## ✨ Highlights

- **⚡ Blazing Fast** — Real-time inference with a compact neural network
- **🧠 Intelligent Predictions** — Context-aware word suggestions
- **🎨 Beautiful UI** — Clean, minimal candidate window with Nocturne Violet theme
- **🔄 GTK3 & GTK4 Support** — Works perfectly on modern Linux desktops
- **⌨️ Familiar Controls** — Natural keybindings (Space, Tab, Arrow keys, etc.)
- **📦 Lightweight** — Excellent accuracy with a small model footprint
- **🌍 Open Source** — Fully customizable and extensible

## 🚀 Quick Start

### Prerequisites
```bash
# On Ubuntu/Debian
sudo apt install gtk-4-dev libgtk-3-dev libpango1.0-dev

# On Fedora
sudo dnf install gtk4-devel gtk3-devel pango-devel
```

### Installation
```bash
git clone https://github.com/yui0/whiz.git
cd whiz
make
sudo make install
```

Then go to **Settings → Region & Language → Input Sources** and add **"Berry Input Method (LLM)"**.

## 🎮 How to Use

- **Toggle Japanese/English**: `Half-width/Full-width` key or `Ctrl + Space`
- **Convert**: `Space` or `Henkan` key
- **Navigate Candidates**: `↑ ↓` or `Tab / Shift+Tab`
- **Move Segments**: `← →`
- **Adjust Segment Boundaries**: `Shift + ← →`
- **Commit**: `Enter`

**Prediction mode** activates automatically as you type!

## 🛠 Project Structure

```
berry-whiz/
├── im.c              # GTK3 IM module
├── im4.c             # GTK4 IM module
├── whiz.c            # Core conversion engine (ConnectNet + Lattice)
├── romaji.h          # Romaji to Kana converter
├── engine/           # Training scripts (Python)
├── model.wcnn        # Trained model
└── whiz.web.dic      # Dictionary
```

## 🧠 Technical Features

- **ConnectNet**: Jointly learned word and connection costs in a single model
- **Lattice + Beam Search**: Efficient best-path finding
- **WUTF Dictionary**: Fast, modern dictionary format
- **Advanced Romaji Conversion**: High-quality kana handling
- **Prediction & Reverse Conversion**: Built-in support for advanced workflows

## 📋 Roadmap

- [ ] Enhanced Wayland support
- [ ] Larger & faster models
- [ ] User dictionary & cloud sync
- [ ] macOS and Windows ports
- [ ] Custom theme support

## 🤝 Contributing

Contributions are warmly welcome!
Bug reports, feature ideas, model improvements, documentation feel free to open issues or PRs.

## 📄 License

**Mozilla Public License 2.0** — Flexible and business-friendly.

---

**Built with ❤️ for a better Japanese typing experience**

**Project Whiz** — © 2026 Yuichiro Nakada

