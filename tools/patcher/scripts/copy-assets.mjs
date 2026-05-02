import { cp, mkdir } from "node:fs/promises";
import { existsSync } from "node:fs";

const source = new URL("../src/assets/", import.meta.url);
const target = new URL("../dist/node/assets/", import.meta.url);

if (existsSync(source)) {
  await mkdir(target, { recursive: true });
  await cp(source, target, { recursive: true });
}
