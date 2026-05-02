import { createHash } from "node:crypto";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { afterEach, describe, expect, it } from "vitest";

import { applyBpsPatch } from "../src/core/patch.js";
import { createReleaseAssets } from "../src/create-release.js";

const tempDirs: string[] = [];

function sha256(bytes: Uint8Array): string {
  return createHash("sha256").update(bytes).digest("hex");
}

async function makeTempDir(): Promise<string> {
  const dir = await mkdtemp(join(tmpdir(), "sf64-release-"));
  tempDirs.push(dir);
  return dir;
}

describe("createReleaseAssets", () => {
  afterEach(async () => {
    await Promise.all(tempDirs.splice(0).map((dir) => rm(dir, { recursive: true, force: true })));
  });

  it("writes a BPS patch and manifest that reproduce the target bytes", async () => {
    const dir = await makeTempDir();
    const source = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4);
    const target = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 9);
    const sourcePath = join(dir, "baserom.us.rev1.z64");
    const targetPath = join(dir, "starfox64.us.rev1.z64");
    const assetsDir = join(dir, "assets");

    await writeFile(sourcePath, source);
    await writeFile(targetPath, target);

    const assets = await createReleaseAssets({
      sourcePath,
      targetPath,
      assetsDir,
      version: "0.1.0",
      expectedSourceMd5: "d41d8cd98f00b204e9800998ecf8427e",
      enforceExpectedSourceMd5: false,
    });

    const patch = await readFile(assets.patchPath);
    const result = applyBpsPatch(source, patch);
    const manifest = JSON.parse(await readFile(assets.manifestPath, "utf8")) as {
      patch: { sha256: string };
      target: { sha256: string };
    };

    expect([...result]).toEqual([...target]);
    expect(manifest.patch.sha256).toBe(sha256(patch));
    expect(manifest.target.sha256).toBe(sha256(target));
  });
});
