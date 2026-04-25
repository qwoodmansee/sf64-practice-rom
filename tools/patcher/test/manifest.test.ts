import { describe, expect, it } from "vitest";

import { parseReleaseManifest } from "../src/core/manifest.js";

const validManifest = {
  manifestVersion: 1,
  release: {
    name: "SF64 Practice ROM",
    version: "0.1.0",
    outputFileName: "starfox64-practice-v0.1.0.z64",
  },
  patch: {
    fileName: "sf64-practice-v0.1.0.bps",
    size: 128,
    sha256: "a".repeat(64),
  },
  source: {
    description: "Star Fox 64 US Rev 1.1",
    byteOrder: "z64",
    md5: "741a94eee093c4c8684e66b89f8685e8",
    sha256: "b".repeat(64),
    size: 12582912,
  },
  target: {
    byteOrder: "z64",
    md5: "c".repeat(32),
    sha256: "d".repeat(64),
    size: 12582912,
  },
} as const;

describe("parseReleaseManifest", () => {
  it("returns a typed manifest for the expected release schema", () => {
    const manifest = parseReleaseManifest(validManifest);

    expect(manifest.source.md5).toBe("741a94eee093c4c8684e66b89f8685e8");
    expect(manifest.release.outputFileName).toBe("starfox64-practice-v0.1.0.z64");
  });

  it("rejects manifests with malformed hashes", () => {
    const invalidManifest = {
      ...validManifest,
      source: {
        ...validManifest.source,
        md5: "not-a-hash",
      },
    };

    expect(() => parseReleaseManifest(invalidManifest)).toThrow(/source.md5/i);
  });
});
