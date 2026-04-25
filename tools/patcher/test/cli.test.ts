import { createHash } from "node:crypto";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { afterEach, describe, expect, it } from "vitest";

import { createBpsPatch } from "../src/core/patch.js";
import { runCli } from "../src/cli.js";

const tempDirs: string[] = [];

function digest(algorithm: "md5" | "sha256", bytes: Uint8Array): string {
  return createHash(algorithm).update(bytes).digest("hex");
}

async function makeTempDir(): Promise<string> {
  const dir = await mkdtemp(join(tmpdir(), "sf64-patcher-"));
  tempDirs.push(dir);
  return dir;
}

describe("runCli", () => {
  afterEach(async () => {
    await Promise.all(tempDirs.splice(0).map((dir) => rm(dir, { recursive: true, force: true })));
  });

  it("patches a user ROM with explicit manifest and patch paths", async () => {
    const dir = await makeTempDir();
    const source = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4);
    const target = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 9);
    const patch = createBpsPatch(source, target);
    const romPath = join(dir, "base.z64");
    const patchPath = join(dir, "practice.bps");
    const manifestPath = join(dir, "manifest.json");
    const outputPath = join(dir, "practice.z64");
    const writes = new Map<string, Uint8Array | string>([
      [romPath, source],
      [patchPath, patch],
      [
        manifestPath,
        JSON.stringify({
          manifestVersion: 1,
          release: {
            name: "SF64 Practice ROM",
            version: "0.1.0",
            outputFileName: "practice.z64",
          },
          patch: {
            fileName: "practice.bps",
            size: patch.byteLength,
            sha256: digest("sha256", patch),
          },
          source: {
            description: "fixture",
            byteOrder: "z64",
            md5: digest("md5", source),
            sha256: digest("sha256", source),
            size: source.byteLength,
          },
          target: {
            byteOrder: "z64",
            md5: digest("md5", target),
            sha256: digest("sha256", target),
            size: target.byteLength,
          },
        }),
      ],
    ]);

    for (const [path, contents] of writes) {
      await import("node:fs/promises").then(({ writeFile }) => writeFile(path, contents));
    }

    const result = await runCli(["patch", romPath, "--patch", patchPath, "--manifest", manifestPath, "--out", outputPath]);

    expect(result.exitCode).toBe(0);
    await expect(readFile(outputPath)).resolves.toEqual(Buffer.from(target));
  });

  it("fails before writing output when the source ROM hash does not match", async () => {
    const dir = await makeTempDir();
    const source = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4);
    const target = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 9);
    const patch = createBpsPatch(source, target);
    const wrongSource = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 8, 8, 8, 8);
    const romPath = join(dir, "wrong.z64");
    const patchPath = join(dir, "practice.bps");
    const manifestPath = join(dir, "manifest.json");
    const outputPath = join(dir, "practice.z64");

    await import("node:fs/promises").then(async ({ writeFile }) => {
      await writeFile(romPath, wrongSource);
      await writeFile(patchPath, patch);
      await writeFile(
        manifestPath,
        JSON.stringify({
          manifestVersion: 1,
          release: {
            name: "SF64 Practice ROM",
            version: "0.1.0",
            outputFileName: "practice.z64",
          },
          patch: {
            fileName: "practice.bps",
            size: patch.byteLength,
            sha256: digest("sha256", patch),
          },
          source: {
            description: "fixture",
            byteOrder: "z64",
            md5: digest("md5", source),
            sha256: digest("sha256", source),
            size: source.byteLength,
          },
          target: {
            byteOrder: "z64",
            md5: digest("md5", target),
            sha256: digest("sha256", target),
            size: target.byteLength,
          },
        }),
      );
    });

    const result = await runCli(["patch", romPath, "--patch", patchPath, "--manifest", manifestPath, "--out", outputPath]);

    expect(result.exitCode).toBe(1);
    expect(result.stderr).toMatch(/wrong rom/i);
    await expect(readFile(outputPath)).rejects.toThrow();
  });
});
