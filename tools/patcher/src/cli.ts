#!/usr/bin/env node
import { existsSync } from "node:fs";
import { readFile, writeFile } from "node:fs/promises";
import { basename, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { normalizeRomToZ64 } from "./core/byte-order.js";
import { parseReleaseManifest, type ReleaseManifest } from "./core/manifest.js";
import { applyBpsPatch } from "./core/patch.js";
import { readManifest, readPackagedAssets } from "./node/assets.js";
import { digestBytes } from "./node/hash.js";

export type CliResult = {
  readonly exitCode: 0 | 1;
  readonly stdout: string;
  readonly stderr: string;
};

type PatchArgs = {
  readonly romPath: string;
  readonly patchPath: string | undefined;
  readonly manifestPath: string | undefined;
  readonly outPath: string | undefined;
};

function usage(error?: string): CliResult {
  const stderr = error ? `${error}\n\n` : "";
  return {
    exitCode: error ? 1 : 0,
    stdout:
      "Usage: sf64-practice-patcher patch <rom-path> [--out <output.z64>] [--patch <patch.bps> --manifest <manifest.json>]\n",
    stderr,
  };
}

function readOption(args: readonly string[], index: number, option: string): string {
  const value = args[index + 1];
  if (!value || value.startsWith("--")) {
    throw new Error(`${option} requires a value.`);
  }
  return value;
}

function parsePatchArgs(args: readonly string[]): PatchArgs {
  if (args[0] !== "patch") {
    throw new Error("Only the patch command is supported.");
  }
  const romPath = args[1];
  if (!romPath || romPath.startsWith("--")) {
    throw new Error("Missing ROM path.");
  }

  let patchPath: string | undefined;
  let manifestPath: string | undefined;
  let outPath: string | undefined;
  for (let index = 2; index < args.length; index += 1) {
    const arg = args[index];
    if (arg === "--patch") {
      patchPath = readOption(args, index, arg);
      index += 1;
    } else if (arg === "--manifest") {
      manifestPath = readOption(args, index, arg);
      index += 1;
    } else if (arg === "--out") {
      outPath = readOption(args, index, arg);
      index += 1;
    } else {
      throw new Error(`Unknown argument: ${arg ?? ""}`);
    }
  }

  if ((patchPath && !manifestPath) || (!patchPath && manifestPath)) {
    throw new Error("--patch and --manifest must be provided together.");
  }

  return { romPath, patchPath, manifestPath, outPath };
}

function outputPathFor(args: PatchArgs, manifest: ReleaseManifest): string {
  if (args.outPath) {
    return args.outPath;
  }
  return join(dirname(args.romPath), manifest.release.outputFileName);
}

async function loadPatchAssets(args: PatchArgs): Promise<{
  readonly manifest: ReleaseManifest;
  readonly patch: Uint8Array;
}> {
  if (args.patchPath && args.manifestPath) {
    return {
      manifest: await readManifest(args.manifestPath),
      patch: await readFile(args.patchPath),
    };
  }
  return readPackagedAssets();
}

function assertHash(label: string, expected: string, actual: string): void {
  if (expected !== actual) {
    throw new Error(`${label} hash mismatch.`);
  }
}

async function patchRom(args: PatchArgs): Promise<CliResult> {
  const { manifest, patch } = await loadPatchAssets(args);
  assertHash("Patch SHA-256", manifest.patch.sha256, digestBytes("sha256", patch));

  const sourceFile = await readFile(args.romPath);
  const normalized = normalizeRomToZ64(sourceFile);
  const sourceMd5 = digestBytes("md5", normalized.bytes);
  if (sourceMd5 !== manifest.source.md5) {
    throw new Error(
      `Wrong ROM: expected ${manifest.source.description} MD5 ${manifest.source.md5}, got ${sourceMd5}.`,
    );
  }

  const target = applyBpsPatch(normalized.bytes, patch);
  assertHash("Target MD5", manifest.target.md5, digestBytes("md5", target));
  assertHash("Target SHA-256", manifest.target.sha256, digestBytes("sha256", target));

  const outPath = outputPathFor(args, manifest);
  await writeFile(outPath, target);

  const converted = normalized.byteOrder === "z64" ? "" : ` Converted from ${normalized.byteOrder} byte order.`;
  return {
    exitCode: 0,
    stdout: `Wrote ${basename(outPath)}.${converted}\n`,
    stderr: "",
  };
}

export async function runCli(args: readonly string[]): Promise<CliResult> {
  try {
    if (args.length === 0 || args.includes("--help") || args.includes("-h")) {
      return usage();
    }
    return await patchRom(parsePatchArgs(args));
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return usage(message);
  }
}

const cliPath = fileURLToPath(import.meta.url);
if (existsSync(cliPath) && process.argv[1] === cliPath) {
  const result = await runCli(process.argv.slice(2));
  if (result.stdout) {
    process.stdout.write(result.stdout);
  }
  if (result.stderr) {
    process.stderr.write(result.stderr);
  }
  process.exitCode = result.exitCode;
}
