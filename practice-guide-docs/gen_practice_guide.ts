/**
 * SF64 Practice ROM — User Guide Generator
 * Usage: npm run guide
 *        (or: npx tsx practice-guide-docs/gen_practice_guide.ts)
 */

import fs from 'fs';
import path from 'path';
import { execSync } from 'child_process';
import { initDoc, doc, pageNum } from './src/renderer';
import { pageCover }      from './src/pages/cover';
import { pageWelcome }    from './src/pages/welcome';
import { pageControls }   from './src/pages/controls';
import { pageQuickRef }   from './src/pages/quick-ref';
import { pageRadialMenu } from './src/pages/radial-menu';
import { pageSaveStates } from './src/pages/save-states';
import { pageCheats }     from './src/pages/cheats';
import { pageDisplay }    from './src/pages/display';
import { pageMacros }     from './src/pages/macros';
import { pageSDCard }     from './src/pages/sd-card';

const repoRoot = path.dirname(__dirname);
const commit   = execSync('git rev-parse --short HEAD', { cwd: repoRoot }).toString().trim();
const outPath  = path.join(__dirname, `sf64_practice_guide_${commit}.pdf`);
const fontsDir = path.join(__dirname, 'fonts');

initDoc(fontsDir);
const stream = fs.createWriteStream(outPath);
doc.pipe(stream);

pageCover();
pageQuickRef();
pageWelcome();
pageControls();
pageRadialMenu();
pageSaveStates();
pageCheats();
pageDisplay();
pageMacros();
pageSDCard();

doc.end();
stream.on('finish', () => console.log(`\n  OK  ${outPath}  (${pageNum} pages)`));
