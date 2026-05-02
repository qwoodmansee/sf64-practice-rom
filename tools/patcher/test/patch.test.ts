import { describe, expect, it } from "vitest";

import { applyBpsPatch, createBpsPatch } from "../src/core/patch.js";

describe("BPS patch helpers", () => {
  it("creates and applies a BPS patch for binary data", () => {
    const source = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4, 5, 6);
    const target = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 9, 4, 5, 10);

    const patch = createBpsPatch(source, target);
    const result = applyBpsPatch(source, patch);

    expect([...result]).toEqual([...target]);
  });

  it("rejects applying a BPS patch to the wrong source bytes", () => {
    const source = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4);
    const target = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 1, 2, 3, 9);
    const wrongSource = Uint8Array.of(0x80, 0x37, 0x12, 0x40, 9, 9, 9, 9);
    const patch = createBpsPatch(source, target);

    expect(() => applyBpsPatch(wrongSource, patch)).toThrow(/source/i);
  });
});
