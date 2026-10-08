import { spawn } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { zoomCaption } from "./core/caption.js";
import { kittyEscapes } from "./core/kitty.js";
import { parse } from "./core/parse.js";
import { pngDims } from "./core/png.js";
import {
  MAX_IMG_DIM,
  MAX_IMG_PIXELS,
  RENDER_SCALE,
  fitCells,
} from "./core/size.js";
import {
  cellMetrics,
  supportsKittyGraphics,
  type CellMetrics,
} from "./core/terminal.js";
import { cacheDir, cacheKey } from "./render/cache.js";
import { Renderer, mermaidConfigJson, rendererVersion } from "./render/renderer.js";

const USAGE = `Usage: catmd [options] [file]
  catmd viewer [file]

cat markdown, rendering \`\`\`mermaid fences as inline terminal images
(kitty graphics protocol: Ghostty, kitty). Reads stdin when no file is
given.

Options:
  --theme <name>     mermaid theme: default, dark, forest, neutral
  --text-size <n>    diagram text height in terminal rows (default 1.0)
  --force            emit images even when stdout is not a supported TTY
  -h, --help         show this help
`;

function launchViewer(file?: string): Promise<number> {
  const viewerName = process.platform === "win32" ? "catmd-viewer.exe" : "catmd-viewer";
  const bundledViewer = [
    join(import.meta.dirname, viewerName),
    join(import.meta.dirname, "..", "src", "Viewer", viewerName),
  ].find(existsSync);
  const executable = process.env.CATMD_VIEWER ??
    bundledViewer ?? viewerName;

  return new Promise((resolve) => {
    const child = spawn(executable, file ? [file] : [], {
      stdio: "inherit",
      env: {
        ...process.env,
        CATMD_NODE: process.execPath,
        CATMD_CLI: process.argv[1],
      },
    });
    child.once("error", (error: NodeJS.ErrnoException) => {
      if (error.code === "ENOENT") {
        process.stderr.write(
          `catmd: viewer executable not found; build Viewer.cpp as catmd-viewer or set CATMD_VIEWER\n`,
        );
      } else {
        process.stderr.write(`catmd: could not start viewer: ${error.message}\n`);
      }
      resolve(1);
    });
    child.once("exit", (code, signal) => {
      resolve(code ?? (signal ? 1 : 0));
    });
  });
}

async function main(): Promise<number> {
  let args;
  try {
    args = parseArgs({
      options: {
        theme: { type: "string", default: "default" },
        "text-size": { type: "string", default: "1.0" },
        force: { type: "boolean", default: false },
        help: { type: "boolean", short: "h", default: false },
      },
      allowPositionals: true,
    });
  } catch (error) {
    process.stderr.write(`catmd: ${(error as Error).message}\n${USAGE}`);
    return 2;
  }
  if (args.values.help) {
    process.stdout.write(USAGE);
    return 0;
  }
  if (args.positionals[0] === "viewer") {
    if (args.positionals.length > 2) {
      process.stderr.write(`catmd: viewer accepts at most one file path\n${USAGE}`);
      return 2;
    }
    return launchViewer(args.positionals[1]);
  }
  const textSize = Number(args.values["text-size"]);
  if (!Number.isFinite(textSize) || textSize <= 0) {
    process.stderr.write("catmd: --text-size must be a positive number\n");
    return 2;
  }

  const file = args.positionals[0];
  let text: string;
  try {
    text = file !== undefined ? await readFile(file, "utf8") : readFileSync(0, "utf8");
  } catch (error) {
    process.stderr.write(`catmd: ${(error as Error).message}\n`);
    return 1;
  }

  const tty = process.stdout.isTTY === true;
  const wantImages = args.values.force || (tty && supportsKittyGraphics(process.env));
  if (tty && !wantImages) {
    process.stderr.write(
      "catmd: terminal doesn't support kitty graphics, printing fences as-is (--force to override)\n",
    );
  }

  const segments = parse(text.split("\n"));
  const pieces: string[] = [];
  let renderer: Renderer | null = null;
  let metrics: CellMetrics | null = null;

  try {
    for (const segment of segments) {
      if (segment.kind === "text") {
        pieces.push(...segment.lines);
        continue;
      }
      if (!wantImages) {
        pieces.push(...segment.raw);
        continue;
      }
      try {
        const key = cacheKey({
          body: segment.body,
          theme: args.values.theme,
          renderScale: RENDER_SCALE,
          maxImgDim: MAX_IMG_DIM,
          maxImgPixels: MAX_IMG_PIXELS,
          mermaidConfig: mermaidConfigJson(),
          rendererVersion: rendererVersion(),
        });
        const pngPath = join(cacheDir(), `${key}.png`);
        let png: Uint8Array;
        if (existsSync(pngPath)) {
          png = await readFile(pngPath);
        } else {
          renderer ??= new Renderer(args.values.theme);
          png = await renderer.render(segment.body);
          await mkdir(cacheDir(), { recursive: true });
          await writeFile(pngPath, png);
        }
        metrics ??= await cellMetrics();
        const { width, height } = pngDims(png);
        const { cols, rows } = fitCells(
          width,
          height,
          metrics.cols,
          metrics.cellW,
          metrics.cellH,
          textSize,
        );
        pieces.push(kittyEscapes(png, cols, rows));
        pieces.push(zoomCaption(pngPath));
      } catch (error) {
        const message = String((error as Error).message ?? error).split("\n")[0];
        process.stderr.write(`catmd: diagram render failed: ${message}\n`);
        pieces.push(...segment.raw);
      }
    }
  } finally {
    await renderer?.close();
  }

  process.stdout.write(pieces.join("\n"));
  return 0;
}

process.exitCode = await main();
