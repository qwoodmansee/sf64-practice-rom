import { describe, expect, it } from "vitest";

import { detectByteOrder, normalizeRomToZ64 } from "../src/core/byte-order.js";

describe("detectByteOrder", () => {
  it("detects native z64 big-endian ROM bytes", () => {
    expect(detectByteOrder(Uint8Array.of(0x80, 0x37, 0x12, 0x40))).toBe("z64");
  });

  it("detects v64 byte-swapped ROM bytes", () => {
    expect(detectByteOrder(Uint8Array.of(0x37, 0x80, 0x40, 0x12))).toBe("v64");
  });

  it("detects n64 little-endian ROM bytes", () => {
    expect(detectByteOrder(Uint8Array.of(0x40, 0x12, 0x37, 0x80))).toBe("n64");
  });
});

describe("normalizeRomToZ64", () => {
  it("returns unchanged bytes when input is already z64", () => {
    const source = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 0xaa, 0xbb, 0xcc, 0xdd);
    const result = normalizeRomToZ64(source);

    expect(result.byteOrder).toBe("z64");
    expect([...result.bytes]).toEqual([...source]);
  });

  it("normalizes v64 bytes to z64 order", () => {
    const source = Uint8Array.of(0x37, 0x80, 0x40, 0x12, 0xbb, 0xaa, 0xdd, 0xcc);
    const result = normalizeRomToZ64(source);

    expect(result.byteOrder).toBe("v64");
    expect([...result.bytes]).toEqual([0x80, 0x37, 0x12, 0x40, 0xaa, 0xbb, 0xcc, 0xdd]);
  });

  it("normalizes n64 bytes to z64 order", () => {
    const source = Uint8Array.of(0x40, 0x12, 0x37, 0x80, 0xdd, 0xcc, 0xbb, 0xaa);
    const result = normalizeRomToZ64(source);

    expect(result.byteOrder).toBe("n64");
    expect([...result.bytes]).toEqual([0x80, 0x37, 0x12, 0x40, 0xaa, 0xbb, 0xcc, 0xdd]);
  });

  it("rejects files that do not look like an N64 ROM", () => {
    expect(() => normalizeRomToZ64(Uint8Array.of(0x00, 0x01, 0x02, 0x03))).toThrow(
      /recognized n64 rom/i,
    );
  });
});
