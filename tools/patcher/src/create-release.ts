#!/usr/bin/env node
import { existsSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

import { normalizeRomToZ64 } from "./core/byte-order.js";
import { RELEASE_MANIFEST_VERSION, type ReleaseManifest } from "./core/manifest.js";
import { createBpsPatch } from "./core/patch.js";
import { digestBytes } from "./node/hash.js";

const DEFAULT_SOURCE_MD5 = "741a94eee093c4c8684e66b89f8685e8";

export type CreateReleaseAssetsOptions = {
  readonly sourcePath: string;
  readonly targetPath: string;
  readonly assetsDir: string;
  readonly version: string;
  readonly expectedSourceMd5?: string;
  readonly enforceExpectedSourceMd5?: boolean;
};

export type CreatedReleaseAssets = {
  readonly patchPath: string;
  readonly manifestPath: string;
  readonly manifest: ReleaseManifest;
};

type CliOptions = {
  sourcePath?: string;
  targetPath?: string;
  assetsDir?: string;
  version?: string;
};

function formatJson(value: unknown): string {
  return `${JSON.stringify(value, null, 2)}\n`;
}

export async function createReleaseAssets(options: CreateReleaseAssetsOptions): Promise<CreatedReleaseAssets> {
  const sourceFile = await readFile(options.sourcePath);
  const targetFile = await readFile(options.targetPath);
  const source = normalizeRomToZ64(sourceFile).bytes;
  const target = normalizeRomToZ64(targetFile).bytes;
  const sourceMd5 = digestBytes("md5", source);
  const expectedSourceMd5 = options.expectedSourceMd5 ?? DEFAULT_SOURCE_MD5;

  if ((options.enforceExpectedSourceMd5 ?? true) && sourceMd5 !== expectedSourceMd5) {
    throw new Error(`Base ROM MD5 mismatch: expected ${expectedSourceMd5}, got ${sourceMd5}.`);
  }

  const patch = createBpsPatch(source, target);
  const patchFileName = `sf64-practice-v${options.version}.bps`;
  const manifest: ReleaseManifest = {
    manifestVersion: RELEASE_MANIFEST_VERSION,
    release: {
      name: "SF64 Practice ROM",
      version: options.version,
      outputFileName: `starfox64-practice-v${options.version}.z64`,
    },
    patch: {
      fileName: patchFileName,
      size: patch.byteLength,
      sha256: digestBytes("sha256", patch),
    },
    source: {
      description: "Star Fox 64 US Rev 1.1",
      byteOrder: "z64",
      md5: sourceMd5,
      sha256: digestBytes("sha256", source),
      size: source.byteLength,
    },
    target: {
      byteOrder: "z64",
      md5: digestBytes("md5", target),
      sha256: digestBytes("sha256", target),
      size: target.byteLength,
    },
  };

  await mkdir(options.assetsDir, { recursive: true });
  const patchPath = join(options.assetsDir, patchFileName);
  const manifestPath = join(options.assetsDir, "manifest.json");
  await writeFile(patchPath, patch);
  await writeFile(manifestPath, formatJson(manifest));

  return { patchPath, manifestPath, manifest };
}

function readOption(args: readonly string[], index: number, option: string): string {
  const value = args[index + 1];
  if (!value || value.startsWith("--")) {
    throw new Error(`${option} requires a value.`);
  }
  return value;
}

function parseArgs(args: readonly string[]): CreateReleaseAssetsOptions {
  const parsed: CliOptions = {};
  for (let index = 0; index < args.length; index += 1) {
    const arg = args[index];
    if (arg === "--source") {
      parsed.sourcePath = readOption(args, index, arg);
      index += 1;
    } else if (arg === "--target") {
      parsed.targetPath = readOption(args, index, arg);
      index += 1;
    } else if (arg === "--assets-dir") {
      parsed.assetsDir = readOption(args, index, arg);
      index += 1;
    } else if (arg === "--version") {
      parsed.version = readOption(args, index, arg);
      index += 1;
    } else {
      throw new Error(`Unknown argument: ${arg ?? ""}`);
    }
  }

  return {
    sourcePath: parsed.sourcePath ?? "baserom.us.rev1.z64",
    targetPath: parsed.targetPath ?? "build/starfox64.us.rev1.z64",
    assetsDir: parsed.assetsDir ?? "tools/patcher/src/assets",
    version: parsed.version ?? "0.0.0",
    expectedSourceMd5: DEFAULT_SOURCE_MD5,
    enforceExpectedSourceMd5: true,
  };
}

const cliPath = fileURLToPath(import.meta.url);
if (existsSync(cliPath) && process.argv[1] === cliPath) {
  try {
    const result = await createReleaseAssets(parseArgs(process.argv.slice(2)));
    process.stdout.write(`Wrote ${result.patchPath}\nWrote ${result.manifestPath}\n`);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    process.stderr.write(`${message}\n`);
    process.exitCode = 1;
  }
}
