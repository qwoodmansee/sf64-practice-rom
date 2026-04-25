import { apply, build, parse, serialize } from "bps";

type BpsInstructions = ReturnType<typeof build>;
type SerializedBpsPatch = ReturnType<typeof serialize>;

function toUint8Array(bytes: Uint8Array | number[]): Uint8Array {
  return bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
}

function serializeToBytes(serialized: SerializedBpsPatch): Uint8Array {
  return toUint8Array(serialized.buffer);
}

export function createBpsPatch(source: Uint8Array, target: Uint8Array): Uint8Array {
  const instructions: BpsInstructions = build(source, target);
  return serializeToBytes(serialize(instructions));
}

export function applyBpsPatch(source: Uint8Array, patch: Uint8Array): Uint8Array {
  const parsed = parse(patch);
  return apply(parsed.instructions, source);
}
