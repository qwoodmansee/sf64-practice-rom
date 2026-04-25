export type RomByteOrder = "z64" | "v64" | "n64";

export type NormalizedRom = {
  readonly byteOrder: RomByteOrder;
  readonly bytes: Uint8Array;
};

const z64Magic = [0x80, 0x37, 0x12, 0x40] as const;
const v64Magic = [0x37, 0x80, 0x40, 0x12] as const;
const n64Magic = [0x40, 0x12, 0x37, 0x80] as const;

function matchesMagic(bytes: Uint8Array, magic: readonly number[]): boolean {
  return magic.every((byte, index) => bytes[index] === byte);
}

export function detectByteOrder(bytes: Uint8Array): RomByteOrder {
  if (matchesMagic(bytes, z64Magic)) {
    return "z64";
  }
  if (matchesMagic(bytes, v64Magic)) {
    return "v64";
  }
  if (matchesMagic(bytes, n64Magic)) {
    return "n64";
  }
  throw new Error("Input is not a recognized N64 ROM byte order.");
}

export function normalizeRomToZ64(bytes: Uint8Array): NormalizedRom {
  const byteOrder = detectByteOrder(bytes);
  if (byteOrder === "z64") {
    return { byteOrder, bytes };
  }

  if (bytes.byteLength % 4 !== 0) {
    throw new Error("N64 ROM length must be divisible by 4 bytes.");
  }

  const normalized = new Uint8Array(bytes.byteLength);
  for (let offset = 0; offset < bytes.byteLength; offset += 4) {
    if (byteOrder === "v64") {
      normalized[offset] = bytes[offset + 1] ?? 0;
      normalized[offset + 1] = bytes[offset] ?? 0;
      normalized[offset + 2] = bytes[offset + 3] ?? 0;
      normalized[offset + 3] = bytes[offset + 2] ?? 0;
    } else {
      normalized[offset] = bytes[offset + 3] ?? 0;
      normalized[offset + 1] = bytes[offset + 2] ?? 0;
      normalized[offset + 2] = bytes[offset + 1] ?? 0;
      normalized[offset + 3] = bytes[offset] ?? 0;
    }
  }

  return { byteOrder, bytes: normalized };
}
