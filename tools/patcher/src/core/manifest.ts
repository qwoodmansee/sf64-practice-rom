import type { RomByteOrder } from "./byte-order.js";

export const RELEASE_MANIFEST_VERSION = 1;

export type ReleaseManifest = {
  readonly manifestVersion: typeof RELEASE_MANIFEST_VERSION;
  readonly release: {
    readonly name: string;
    readonly version: string;
    readonly outputFileName: string;
  };
  readonly patch: {
    readonly fileName: string;
    readonly size: number;
    readonly sha256: string;
  };
  readonly source: {
    readonly description: string;
    readonly byteOrder: RomByteOrder;
    readonly md5: string;
    readonly sha256: string;
    readonly size: number;
  };
  readonly target: {
    readonly byteOrder: "z64";
    readonly md5: string;
    readonly sha256: string;
    readonly size: number;
  };
};

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function field(record: Record<string, unknown>, key: string): unknown {
  return record[key];
}

function readRecord(record: Record<string, unknown>, key: string): Record<string, unknown> {
  const value = field(record, key);
  if (!isRecord(value)) {
    throw new Error(`manifest.${key} must be an object.`);
  }
  return value;
}

function readString(record: Record<string, unknown>, key: string, path = key): string {
  const value = field(record, key);
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`manifest.${path} must be a non-empty string.`);
  }
  return value;
}

function readNumber(record: Record<string, unknown>, key: string, path = key): number {
  const value = field(record, key);
  if (typeof value !== "number" || !Number.isInteger(value) || value < 0) {
    throw new Error(`manifest.${path} must be a non-negative integer.`);
  }
  return value;
}

function readMd5(record: Record<string, unknown>, key: string, path = key): string {
  const value = readString(record, key, path);
  if (!/^[0-9a-f]{32}$/i.test(value)) {
    throw new Error(`manifest.${path} must be a 32-character MD5 hash.`);
  }
  return value.toLowerCase();
}

function readSha256(record: Record<string, unknown>, key: string, path = key): string {
  const value = readString(record, key, path);
  if (!/^[0-9a-f]{64}$/i.test(value)) {
    throw new Error(`manifest.${path} must be a 64-character SHA-256 hash.`);
  }
  return value.toLowerCase();
}

function readSourceByteOrder(record: Record<string, unknown>, key: string, path = key): RomByteOrder {
  const value = readString(record, key, path);
  if (value !== "z64" && value !== "v64" && value !== "n64") {
    throw new Error(`manifest.${path} must be z64, v64, or n64.`);
  }
  return value;
}

function readTargetByteOrder(record: Record<string, unknown>, key: string, path = key): "z64" {
  const value = readString(record, key, path);
  if (value !== "z64") {
    throw new Error(`manifest.${path} must be z64.`);
  }
  return value;
}

export function parseReleaseManifest(value: unknown): ReleaseManifest {
  if (!isRecord(value)) {
    throw new Error("manifest must be an object.");
  }
  if (value.manifestVersion !== RELEASE_MANIFEST_VERSION) {
    throw new Error(`manifest.manifestVersion must be ${RELEASE_MANIFEST_VERSION}.`);
  }

  const release = readRecord(value, "release");
  const patch = readRecord(value, "patch");
  const source = readRecord(value, "source");
  const target = readRecord(value, "target");

  return {
    manifestVersion: RELEASE_MANIFEST_VERSION,
    release: {
      name: readString(release, "name", "release.name"),
      version: readString(release, "version", "release.version"),
      outputFileName: readString(release, "outputFileName", "release.outputFileName"),
    },
    patch: {
      fileName: readString(patch, "fileName", "patch.fileName"),
      size: readNumber(patch, "size", "patch.size"),
      sha256: readSha256(patch, "sha256", "patch.sha256"),
    },
    source: {
      description: readString(source, "description", "source.description"),
      byteOrder: readSourceByteOrder(source, "byteOrder", "source.byteOrder"),
      md5: readMd5(source, "md5", "source.md5"),
      sha256: readSha256(source, "sha256", "source.sha256"),
      size: readNumber(source, "size", "source.size"),
    },
    target: {
      byteOrder: readTargetByteOrder(target, "byteOrder", "target.byteOrder"),
      md5: readMd5(target, "md5", "target.md5"),
      sha256: readSha256(target, "sha256", "target.sha256"),
      size: readNumber(target, "size", "target.size"),
    },
  };
}
