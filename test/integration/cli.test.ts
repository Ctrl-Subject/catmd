import { execFile } from "node:child_process";
import { mkdtemp, readFile, readdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { promisify } from "node:util";
import { beforeAll, describe, expect, it } from "vitest";

const run = promisify(execFile);
const CLI = join(import.meta.dirname, "../../dist/cli.js");

const SAMPLE = `# Deposit flow

Money moves from the bank into the wallet.

\`\`\`mermaid
sequenceDiagram
  App->>API: POST /deposit
  API-->>App: confirmed
\`\`\`

A normal code block passes through:

\`\`\`java
int x = 1;
\`\`\`

The end.
`;

let dir: string;
let cacheDir: string;
let samplePath: string;

beforeAll(async () => {
  dir = await mkdtemp(join(tmpdir(), "catmd-e2e-"));
  cacheDir = join(dir, "cache");
  samplePath = join(dir, "sample.md");
  await writeFile(samplePath, SAMPLE);
});

function cli(args: string[], extraEnv: NodeJS.ProcessEnv = {}) {
  return run(process.execPath, [CLI, ...args], {
    env: { ...process.env, XDG_CACHE_HOME: cacheDir, ...extraEnv },
    maxBuffer: 64 * 1024 * 1024,
  });
}

describe("cli end-to-end", { timeout: 120_000 }, () => {
  it("piped output without --force is byte-identical to the input", async () => {
    const { stdout, stderr } = await cli([samplePath]);
    expect(stdout).toBe(SAMPLE);
    expect(stderr).toBe("");
  });

  it("--force renders the diagram, keeps prose, caches, and links zoom", async () => {
    const { stdout, stderr } = await cli(["--force", samplePath]);
    expect(stderr).toBe("");
    expect(stdout).toContain("\x1b_G");
    expect(stdout).not.toContain("sequenceDiagram");
    expect(stdout).toContain("Money moves from the bank");
    expect(stdout).toContain("int x = 1;");
    expect(stdout).toContain("\x1b]8;;file://");
    const cached = await readdir(join(cacheDir, "catmd"));
    expect(cached.filter((f) => f.endsWith(".png"))).toHaveLength(1);
  });

  it("second --force run hits the cache and is fast", async () => {
    const started = Date.now();
    const { stdout } = await cli(["--force", samplePath]);
    expect(stdout).toContain("\x1b_G");
    expect(Date.now() - started).toBeLessThan(3_000);
  });

  it("invalid mermaid falls back to the raw fence with a warning", async () => {
    const badPath = join(dir, "bad.md");
    await writeFile(badPath, "before\n\n```mermaid\nnot mermaid !!!\n```\n\nafter\n");
    const { stdout, stderr } = await cli(["--force", badPath]);
    expect(stderr).toContain("diagram render failed");
    expect(stdout).toContain("```mermaid");
    expect(stdout).toContain("not mermaid !!!");
    expect(stdout).toContain("after");
  });

  it("viewer launches the native app and forwards an optional file path", async () => {
    const capturePath = join(dir, "viewer-args.json");
    const filePath = join(dir, "notes with spaces.mjs");
    await writeFile(
      filePath,
      `import { writeFileSync } from "node:fs"; writeFileSync(process.env.CATMD_TEST_CAPTURE, JSON.stringify(process.argv[1]));`,
    );

    const { stderr } = await cli(["viewer", filePath], {
      CATMD_VIEWER: process.execPath,
      CATMD_TEST_CAPTURE: capturePath,
    });

    expect(stderr).toBe("");
    expect(await readFile(capturePath, "utf8")).toBe(JSON.stringify(filePath));
  });
});
