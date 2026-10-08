# catmd

`cat` for markdown. Output is your file, verbatim — except where `catmd` can
render something better in the terminal.

**Feature #1: mermaid.** ` ```mermaid ` fences become actual diagram images
inline, in place of the fence source.

![catmd rendering demo/demo.md: a typed command followed by the document with its mermaid diagrams shown as images](https://raw.githubusercontent.com/Verano-20/catmd/main/demo/demo.gif)

```
npm install -g @verano-20/cat-md
```

```
catmd DESIGN.md
```

(The npm package is scoped — `catmd` was squatted years ago — but the command
it installs is plain `catmd`.)

## Why

Terminal markdown renderers (`glow`, `mdcat`) print mermaid fences as code
blocks — mermaid needs a browser engine to lay out. Browser previews take you
out of the terminal. `catmd` renders each fence headlessly (via
[mermaid-cli](https://github.com/mermaid-js/mermaid-cli)) and paints the
result straight into the terminal with the kitty graphics protocol.

- **Uniform, readable text**: every diagram is displayed so mermaid's base
  font matches your terminal's text height — computed live from your
  terminal's actual cell size — regardless of how big the diagram is.
- **Zoom**: each diagram gets a dim `⌕ zoom` caption; cmd/ctrl+click opens
  the full-resolution PNG in your image viewer.
- **Fast after the first look**: renders are cached in `~/.cache/catmd`.
  One browser instance renders every diagram in a doc; fully-cached docs
  never launch a browser at all.
- **Safe in pipes**: when stdout isn't a supported TTY, output is
  byte-identical to the input — it degrades to plain `cat`, so `catmd x.md |
  grep …` just works.
- **Terminal-limit aware**: kitty/Ghostty silently drop images over 10000px
  a side or over the image storage budget; catmd caps renders under both
  with a quality-preserving vector re-render.

## Usage

```
catmd [options] [file]         # reads stdin when no file is given
catmd viewer [file]            # open the standalone file viewer

--theme <name>       mermaid theme: default, dark, forest, neutral
--text-size <n>      diagram text height in terminal rows (default 1.0)
--force              emit images even when stdout is not a supported TTY
```

`catmd viewer` opens the native `catmd-viewer` application, independent of
terminal graphics support. Build `src/Viewer/Viewer.cpp` with GLUT and GLUI,
then place the executable beside the installed `catmd` CLI as `catmd-viewer`
(`catmd-viewer.exe` on Windows), or set `CATMD_VIEWER` to its path. Pass a
file path to open it in the preview when the viewer starts.

Invalid mermaid prints a warning to stderr and passes the original fence
through; the rest of the document still renders.

## Requirements

- **Node >= 22.12.** Installing pulls in puppeteer, which downloads a
  headless Chromium (mermaid rendering needs a browser engine — there is no
  lighter way).
- **A terminal that speaks the kitty graphics protocol**:
  [Ghostty](https://ghostty.org) or [kitty](https://sw.kovidgoyal.net/kitty/)
  are detected automatically. Other terminals degrade to plain `cat` with a
  hint on stderr. WezTerm can work if you've set `enable_kitty_graphics =
  true` — run with `--force` (it's not auto-detected because that support is
  off by default).

## Roadmap

Candidate features, in the same spirit — verbatim output except where the
terminal can do better:

- LaTeX/KaTeX blocks as rendered images
- Linked/embedded raster images inline
- Opt-in prose styling (headers, emphasis) that stays pipe-safe

## Alternatives

- [`glow`](https://github.com/charmbracelet/glow) / `mdcat` — beautiful
  markdown, but mermaid fences stay source code.
- [`presenterm`](https://github.com/mfontanini/presenterm) — renders mermaid
  inline, but it's a slide presenter and wants slide-structured input.
- `mmdc` + an image viewer — works, but per-invocation browser launches and
  no document flow.

## Development

```
npm test                    # unit suite (fast, no browser)
npm run test:integration    # real renders through headless Chromium
npm run typecheck
npm run build
node scripts/make-demo.mjs  # regenerate demo/demo.gif from demo/demo.md
```

MIT.
