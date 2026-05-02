import { createHash } from "node:crypto";

export type HashAlgorithm = "md5" | "sha256";

export function digestBytes(algorithm: HashAlgorithm, bytes: Uint8Array): string {
  return createHash(algorithm).update(bytes).digest("hex");
}
