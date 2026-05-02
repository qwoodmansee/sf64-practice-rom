import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { parseReleaseManifest, type ReleaseManifest } from "../core/manifest.js";

const currentDir = dirname(fileURLToPath(import.meta.url));
const defaultAssetsDir = join(currentDir, "assets");

export type PackagedAssets = {
  readonly manifest: ReleaseManifest;
  readonly patch: Uint8Array;
};

export async function readJsonFile(path: string): Promise<unknown> {
  return JSON.parse(await readFile(path, "utf8"));
}

export async function readManifest(path: string): Promise<ReleaseManifest> {
  return parseReleaseManifest(await readJsonFile(path));
}

export async function readPackagedAssets(): Promise<PackagedAssets> {
  const manifestPath = join(defaultAssetsDir, "manifest.json");
  const manifest = await readManifest(manifestPath);
  const patch = await readFile(join(defaultAssetsDir, manifest.patch.fileName));
  return { manifest, patch };
}
