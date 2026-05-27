/* Read PRACTICE_VERSION from include/practice.h so the guide PDF always
 * reflects the ROM version the practice rom currently identifies as.
 * Parsed at module load time; the gen script regenerates the PDF on each
 * release so this stays in sync with the ROM. */
import fs from 'fs';
import path from 'path';

function readVersion(): string {
  const headerPath = path.join(__dirname, '..', '..', 'include', 'practice.h');
  const src = fs.readFileSync(headerPath, 'utf-8');
  const match = src.match(/#define\s+PRACTICE_VERSION\s+"([^"]+)"/);
  if (!match) {
    throw new Error(
      `Could not find PRACTICE_VERSION in ${headerPath}; guide PDF cannot tag itself`
    );
  }
  return match[1];
}

export const PRACTICE_VERSION = readVersion();
