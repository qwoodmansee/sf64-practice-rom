/**
 * SF64 Practice ROM — User Guide Generator
 * Usage: npm install && npx tsx tools/gen_practice_guide.ts
 */

import PDFDocument from 'pdfkit';
import fs from 'fs';
import path from 'path';

// ─── Color palette ────────────────────────────────────────────────────────────
const C = {
  bg:        '#0A0A14',   // space black — used for ALL pages now
  panelDark: '#1F254E',
  panelMid:  '#2C3269',
  border:    '#3D42A0',
  greenTop:  '#0F5E1F',
  greenBot:  '#7FC224',
  red:       '#FB1A32',
  grey:      '#B5B89C',
  white:     '#FFFFFF',
  textMain:  '#D0CEC4',   // light warm text (dark bg)
  textDim:   '#9090B0',   // muted blue-grey (dark bg)
  rowAlt:    '#13152A',   // subtle dark-navy alt row
};

// ─── Page geometry ────────────────────────────────────────────────────────────
const W    = 595.28;
const H    = 841.89;
const ML   = 44;
const PW   = W - ML - ML;
const HBAR = 28;
const MT   = HBAR + 14;
const MB   = HBAR + 14;

// ─── Color helpers ────────────────────────────────────────────────────────────
// PDFKit fill() does NOT understand CSS rgb() strings — always convert to hex
function rgbToHex(r: number, g: number, b: number): string {
  return '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('');
}

function linearGrad(
  doc: PDFKit.PDFDocument,
  x1: number, y1: number, x2: number, y2: number,
  stops: [number, string][]
) {
  const g = doc.linearGradient(x1, y1, x2, y2);
  for (const [pos, color] of stops) g.stop(pos, color);
  return g as unknown as string;
}

// Relative luminance (WCAG formula) — returns 0 (black) … 1 (white)
function luminance(r: number, g: number, b: number): number {
  return [r, g, b].reduce((acc, v, i) => {
    const s = v / 255;
    const lin = s <= 0.03928 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
    return acc + lin * [0.2126, 0.7152, 0.0722][i];
  }, 0);
}

// Returns '#FFFFFF' or '#1A1A2E' depending on which has better contrast
function contrastText(r: number, g: number, b: number): string {
  return luminance(r, g, b) > 0.35 ? '#1A1A2E' : '#FFFFFF';
}

// ─── Page state ───────────────────────────────────────────────────────────────
let pageNum = 0;
let doc: PDFKit.PDFDocument;

function pageHeader(section: string) {
  doc.rect(0, 0, W, HBAR).fill(C.panelDark);
  doc.moveTo(0, HBAR).lineTo(W, HBAR).strokeColor(C.grey).lineWidth(0.4).stroke();
  doc.fillColor(C.greenBot).font('Courier-Bold').fontSize(8.5)
     .text('SF64 PRACTICE ROM', ML, 9, { lineBreak: false });
  if (section) {
    doc.fillColor(C.grey).font('Helvetica').fontSize(7.5)
       .text(section.toUpperCase(), ML, 10, { width: PW, align: 'right', lineBreak: false });
  }
}

function pageFooter() {
  doc.rect(0, H - HBAR, W, HBAR).fill(C.panelDark);
  doc.moveTo(0, H - HBAR).lineTo(W, H - HBAR).strokeColor(C.grey).lineWidth(0.4).stroke();
  doc.fillColor(C.grey).font('Helvetica').fontSize(7.5)
     .text(`sageraces.com  -  page ${pageNum}`, ML, H - 17, { width: PW, align: 'center', lineBreak: false });
}

function newPage(section = '') {
  pageNum++;
  doc.addPage();
  doc.rect(0, 0, W, H).fill(C.bg);   // dark background on every page
  pageHeader(section);
  pageFooter();
  doc.y = MT;
}

// ─── Typography helpers ───────────────────────────────────────────────────────

// Section header: green gradient bg, Helvetica-Bold (not Courier), larger, letter-spaced
function sectionHeader(title: string) {
  const y = doc.y + 3;
  const h = 27;
  const g = linearGrad(doc, ML, y, ML, y + h, [[0, C.greenTop], [1, C.greenBot]]);
  doc.rect(ML, y, PW, h).fill(g);
  doc.rect(ML, y + h - 1, PW, 1).fill(C.greenBot);
  doc.fillColor(C.white).font('Helvetica-Bold').fontSize(14)
     .text(title, ML + 10, y + 7, {
       width: PW - 20,
       lineBreak: false,
       characterSpacing: 1.4,
     });
  doc.y = y + h + 8;
}

function subHeader(title: string) {
  const y = doc.y + 2;
  doc.fillColor(C.greenBot).font('Helvetica-Bold').fontSize(10)
     .text(title, ML, y, { lineBreak: false });
  doc.y += 14;
  doc.moveTo(ML, doc.y).lineTo(ML + PW, doc.y).strokeColor(C.greenBot).lineWidth(0.35).stroke();
  doc.y += 5;
}

function bodyText(text: string, indent = 0) {
  doc.fillColor(C.textMain).font('Helvetica').fontSize(9.5)
     .text(text, ML + indent, doc.y, { width: PW - indent });
  doc.y += 3;
}

function vspace(pts = 6) { doc.y += pts; }

function hRule(color = C.grey) {
  doc.moveTo(ML, doc.y).lineTo(ML + PW, doc.y).strokeColor(color).lineWidth(0.35).stroke();
  doc.y += 4;
}

function bullet(items: string[], indent = 0) {
  doc.font('Helvetica').fontSize(9.5);
  for (const item of items) {
    const bx = ML + indent + 5;
    const ty = doc.y;
    doc.circle(bx, ty + 5.5, 1.8).fill(C.border);
    doc.fillColor(C.textMain).text(item, bx + 8, ty, { width: PW - indent - 14 });
    doc.y += 1;
  }
}

// Standard option/reference table (no button icons)
function ctrlTable(rows: { key: string; val: string }[]) {
  const rowH = 17;
  const keyW = 135;
  for (let i = 0; i < rows.length; i++) {
    const y = doc.y;
    if (i % 2 === 0) doc.rect(ML, y, PW, rowH).fill(C.rowAlt);
    doc.fillColor(C.greenBot).font('Courier-Bold').fontSize(8.5)
       .text(rows[i].key, ML + 6, y + 4, { width: keyW, lineBreak: false });
    doc.fillColor(C.textMain).font('Helvetica').fontSize(8.5)
       .text(rows[i].val, ML + keyW + 8, y + 4, { width: PW - keyW - 14, lineBreak: false });
    doc.y = y + rowH;
  }
  doc.y += 5;
}

function callout(text: string, type: 'warning' | 'info' | 'tip' = 'info') {
  const cfg = {
    warning: { stripe: C.red,      bg: '#2A080C', text: '#FFB0B8' },
    info:    { stripe: C.border,   bg: '#0E1030', text: '#B4C8FF' },
    tip:     { stripe: C.greenBot, bg: '#0E2010', text: '#C0F0A0' },
  }[type];
  const y0 = doc.y + 3;
  doc.font('Helvetica').fontSize(9);
  const th = doc.heightOfString(text, { width: PW - 18 });
  const bh = th + 14;
  doc.rect(ML, y0, PW, bh).fill(cfg.bg);
  doc.rect(ML, y0, 4, bh).fill(cfg.stripe);
  doc.fillColor(cfg.text).font('Helvetica').fontSize(9)
     .text(text, ML + 12, y0 + 7, { width: PW - 18 });
  doc.y = y0 + bh + 7;
}

// ─── N64 Controller Button Drawing ────────────────────────────────────────────
// Each draw fn positions at (x, y) and returns width consumed.
// Text/stroke colors respect luminance contrast automatically.

const BTN_SZ = 12;   // base size for round buttons

function drawBtnA(x: number, y: number, sz = BTN_SZ): number {
  const r = sz / 2;
  doc.circle(x + r, y + r, r).fill('#007830');
  doc.fillColor('#FFFFFF').font('Helvetica-Bold').fontSize(sz * 0.62)
     .text('A', x, y + sz * 0.19, { width: sz, align: 'center', lineBreak: false });
  return sz + 2;
}

function drawBtnB(x: number, y: number, sz = BTN_SZ - 2): number {
  const r = sz / 2;
  doc.circle(x + r, y + r, r).fill('#B81818');
  doc.fillColor('#FFFFFF').font('Helvetica-Bold').fontSize(sz * 0.62)
     .text('B', x, y + sz * 0.19, { width: sz, align: 'center', lineBreak: false });
  return sz + 2;
}

function drawBtnZ(x: number, y: number): number {
  const w = 20, h = 11, py = y + 2;
  doc.roundedRect(x, py, w, h, 3).fill('#383848');
  doc.fillColor('#C0C0D0').font('Helvetica-Bold').fontSize(7.5)
     .text('Z', x, py + 2, { width: w, align: 'center', lineBreak: false });
  return w + 2;
}

function drawBtnR(x: number, y: number): number {
  const w = 22, h = 9, py = y + 4;
  // Shoulder shape: wider on one side
  doc.roundedRect(x, py, w, h, 3).fill('#484858');
  doc.fillColor('#D0D0E0').font('Helvetica-Bold').fontSize(7)
     .text('R', x, py + 1.5, { width: w, align: 'center', lineBreak: false });
  return w + 2;
}

function drawBtnL(x: number, y: number): number {
  const w = 22, h = 9, py = y + 4;
  doc.roundedRect(x, py, w, h, 3).fill('#484858');
  doc.fillColor('#D0D0E0').font('Helvetica-Bold').fontSize(7)
     .text('L', x, py + 1.5, { width: w, align: 'center', lineBreak: false });
  return w + 2;
}

function drawBtnStart(x: number, y: number, sz = BTN_SZ): number {
  const r = sz / 2;
  doc.circle(x + r, y + r, r).fill('#7A1020');
  doc.fillColor('#FFFFFF').font('Helvetica-Bold').fontSize(4.5)
     .text('START', x - 2, y + sz * 0.32, { width: sz + 4, align: 'center', lineBreak: false });
  return sz + 2;
}

// D-pad: cross shape, optional direction highlight (which arm to accent in green)
function drawBtnDPad(x: number, y: number, dir?: 'U' | 'D' | 'L' | 'R', sz = 13): number {
  const arm = Math.round(sz / 3);
  const base = '#303038';
  const hi   = C.greenBot;
  // Vertical bar
  doc.rect(x + arm, y, arm, sz).fill(base);
  // Horizontal bar
  doc.rect(x, y + arm, sz, arm).fill(base);
  // Direction highlight
  if (dir === 'U') doc.rect(x + arm, y,          arm, arm).fill(hi);
  if (dir === 'D') doc.rect(x + arm, y + arm * 2, arm, arm).fill(hi);
  if (dir === 'L') doc.rect(x,       y + arm,     arm, arm).fill(hi);
  if (dir === 'R') doc.rect(x + arm * 2, y + arm, arm, arm).fill(hi);
  return sz + 2;
}

// ─── Button-row control table (for pages with controller shortcuts) ───────────
// Each row: array of "tokens" that draw button icons or connector text, plus description.
type BtnToken = 'A' | 'B' | 'Z' | 'R' | 'L' | 'START' | 'DU' | 'DD' | 'DL' | 'DR' | '+' | '/';

function drawTokens(tokens: BtnToken[], x: number, y: number): number {
  let ox = x;
  const mid = y + BTN_SZ / 2 - 4;
  for (const tok of tokens) {
    switch (tok) {
      case 'A':     ox += drawBtnA(ox, y); break;
      case 'B':     ox += drawBtnB(ox, y); break;
      case 'Z':     ox += drawBtnZ(ox, y); break;
      case 'R':     ox += drawBtnR(ox, y); break;
      case 'L':     ox += drawBtnL(ox, y); break;
      case 'START': ox += drawBtnStart(ox, y); break;
      case 'DU':    ox += drawBtnDPad(ox, y, 'U'); break;
      case 'DD':    ox += drawBtnDPad(ox, y, 'D'); break;
      case 'DL':    ox += drawBtnDPad(ox, y, 'L'); break;
      case 'DR':    ox += drawBtnDPad(ox, y, 'R'); break;
      case '+':
        doc.fillColor(C.grey).font('Helvetica-Bold').fontSize(9)
           .text('+', ox, mid, { lineBreak: false });
        ox += 9;
        break;
      case '/':
        doc.fillColor(C.grey).font('Helvetica').fontSize(9)
           .text('/', ox, mid, { lineBreak: false });
        ox += 7;
        break;
    }
  }
  return ox - x;
}

const ICON_COL = 110;   // reserved width for button icons in icon tables

function iconCtrlTable(rows: { btns: BtnToken[]; val: string; note?: string }[]) {
  const rowH = 22;
  for (let i = 0; i < rows.length; i++) {
    const y = doc.y;
    if (i % 2 === 0) doc.rect(ML, y, PW, rowH).fill(C.rowAlt);
    drawTokens(rows[i].btns, ML + 5, y + 5);
    doc.fillColor(C.textMain).font('Helvetica').fontSize(8.5)
       .text(rows[i].val, ML + ICON_COL, y + 6, { width: PW - ICON_COL - 4, lineBreak: false });
    if (rows[i].note) {
      doc.fillColor('#FFB0B8').font('Helvetica').fontSize(7.5)
         .text(rows[i].note!, ML + ICON_COL, y + 14, { width: PW - ICON_COL - 4, lineBreak: false });
    }
    doc.y = y + rowH;
  }
  doc.y += 5;
}

// ─── Radial menu diagram ─────────────────────────────────────────────────────
function radialDiagram(cx: number, cy: number, R = 88) {
  const slices: { angle: number; label: string; rgb: [number, number, number] }[] = [
    { angle: 270, label: 'RESTART',  rgb: [180,  60,  60] },
    { angle: 315, label: 'DISPLAY',  rgb: [ 60, 160, 160] },
    { angle:   0, label: 'SAVE',     rgb: [ 60, 140, 180] },
    { angle:  45, label: 'LOAD',     rgb: [ 60, 180, 100] },
    { angle:  90, label: 'LEVELS',   rgb: [180, 140,  60] },
    { angle: 135, label: 'LOADOUT',  rgb: [140,  60, 180] },
    { angle: 180, label: 'CHEATS',   rgb: [200,  80,  80] },
    { angle: 225, label: 'SD CARD',  rgb: [ 80, 200, 120] },
  ];

  doc.circle(cx, cy, R + 12).fillAndStroke(C.panelDark, C.border);

  for (const s of slices) {
    const rad = (s.angle * Math.PI) / 180;
    doc.moveTo(cx + Math.cos(rad) * 28, cy + Math.sin(rad) * 28)
       .lineTo(cx + Math.cos(rad) * (R - 2), cy + Math.sin(rad) * (R - 2))
       .strokeColor(C.border).lineWidth(0.5).stroke();
  }

  const PW2 = 52, PH = 16;
  for (const s of slices) {
    const rad = (s.angle * Math.PI) / 180;
    const px  = cx + Math.cos(rad) * R;
    const py  = cy + Math.sin(rad) * R;
    const hex = rgbToHex(...s.rgb);
    doc.roundedRect(px - PW2 / 2, py - PH / 2, PW2, PH, 2.5).fill(hex);
    // Contrast-safe text: auto dark/light based on pill color
    doc.fillColor(contrastText(...s.rgb)).font('Courier-Bold').fontSize(6.2)
       .text(s.label, px - PW2 / 2 + 2, py - 4.5, { width: PW2 - 4, align: 'center', lineBreak: false });
  }

  doc.circle(cx, cy, 28).fillAndStroke(C.panelMid, C.border);
  doc.fillColor(C.grey).font('Courier-Bold').fontSize(5.5);
  doc.text('STICK', cx - 14, cy - 8, { width: 28, align: 'center', lineBreak: false });
  doc.text('THEN A', cx - 14, cy - 1, { width: 28, align: 'center', lineBreak: false });

  // ASCII compass labels (Unicode arrows not supported in built-in PDF fonts)
  doc.fillColor(C.grey).font('Helvetica').fontSize(5.5);
  doc.text('^ UP',   cx - 8,       cy - R - 20, { lineBreak: false });
  doc.text('v DOWN', cx - 10,      cy + R + 10, { lineBreak: false });
  doc.text('>',      cx + R + 14,  cy - 3,      { lineBreak: false });
  doc.text('<',      cx - R - 18,  cy - 3,      { lineBreak: false });
}

// ─── Page: Cover ─────────────────────────────────────────────────────────────
function pageCover() {
  pageNum++;
  doc.addPage();
  doc.rect(0, 0, W, H).fill(C.bg);

  doc.rect(0, 0, W, HBAR).fill(C.panelDark);
  doc.rect(0, HBAR, W, 1).fill(C.border);
  doc.fillColor(C.greenBot).font('Courier-Bold').fontSize(9)
     .text('SF64 PRACTICE ROM  -  BETA', ML, 9, { width: PW, align: 'center', lineBreak: false });

  doc.rect(0, H - HBAR, W, HBAR).fill(C.panelDark);
  doc.rect(0, H - HBAR, W, 1).fill(C.border);
  doc.fillColor(C.grey).font('Helvetica').fontSize(8)
     .text('sageraces.com  -  staging.sageraces.com (beta)', ML, H - 17, { width: PW, align: 'center', lineBreak: false });

  const ruleY1 = 90;
  doc.rect(ML, ruleY1, PW, 1.5).fill(C.border);
  doc.rect(ML + 50, ruleY1 - 3, PW - 100, 1).fill(C.panelMid);

  const ruleY2 = H - 90;
  doc.rect(ML, ruleY2, PW, 1.5).fill(C.border);
  doc.rect(ML + 50, ruleY2 + 2, PW - 100, 1).fill(C.panelMid);

  // Title
  const titleY = 130;
  doc.fillColor(C.white).font('Courier-Bold').fontSize(50)
     .text('STAR FOX 64', ML, titleY, { width: PW, align: 'center', lineBreak: false });
  doc.fillColor(C.grey).font('Courier-Bold').fontSize(19)
     .text('PRACTICE ROM', ML, titleY + 63, { width: PW, align: 'center', lineBreak: false });

  const dotY = titleY + 98;
  for (let i = 0; i < 5; i++) {
    const dx = W / 2 - 24 + i * 12;
    doc.circle(dx, dotY, i === 2 ? 3.5 : 2).fill(i === 2 ? C.greenBot : C.border);
  }
  doc.fillColor(C.border).font('Helvetica').fontSize(11)
     .text('BETA USER GUIDE', ML, dotY + 14, { width: PW, align: 'center', lineBreak: false });

  // SD warning
  const wY = 338;
  const wH = 96;
  doc.rect(ML, wY, PW, wH).fill('#280008');
  doc.rect(ML, wY, PW, wH).strokeColor(C.red).lineWidth(0.9).stroke();
  doc.rect(ML, wY, 5, wH).fill(C.red);
  doc.fillColor(C.red).font('Courier-Bold').fontSize(10.5)
     .text('(!) SD CARD SAVING: NOT WORKING ON EVERDRIVE', ML + 14, wY + 11, { width: PW - 20, lineBreak: false });
  doc.moveTo(ML + 14, wY + 28).lineTo(ML + PW - 8, wY + 28).strokeColor('#6B0010').lineWidth(0.5).stroke();
  doc.fillColor('#FFB0B8').font('Helvetica').fontSize(9.5)
     .text(
       'SD card save states are in development and do NOT function on EverDrive hardware. ' +
       'RAM save states (D-pad shortcuts) work on all hardware but are cleared on power-off. ' +
       'See the SD Card section for full details.',
       ML + 14, wY + 36, { width: PW - 22 }
     );

  // Feature grid
  const featY = 454;
  doc.fillColor(C.border).font('Courier-Bold').fontSize(9)
     .text('WHAT IS INCLUDED', ML, featY, { width: PW, align: 'center', lineBreak: false });
  doc.moveTo(ML + 60, featY + 14).lineTo(W - ML - 60, featY + 14).strokeColor(C.border).lineWidth(0.5).stroke();

  const features = [
    ['SAVE STATES',   'Save + load your exact game position instantly'],
    ['FRAME ADVANCE', 'Pause and step forward one frame at a time'],
    ['CHEATS',        'Infinite health, bombs, lives, and boost meter'],
    ['HUD OVERLAYS',  'Hit counter, speed, lag frames, charge timing'],
    ['VISUALIZERS',   'Colored hitboxes, spawn zones, live enemy HP bars'],
    ['LEVEL SELECT',  'Warp to any stage or checkpoint directly'],
    ['LOADOUT',       'Laser, bombs, lives, wings, wingmen — editable mid-run'],
    ['INPUT MACROS',  'Record and replay button sequences'],
  ];

  const fY = featY + 24;
  const colW = PW / 2 - 6;
  for (let i = 0; i < features.length; i++) {
    const [name, desc] = features[i];
    const col = i % 2;
    const row = Math.floor(i / 2);
    const fx = ML + col * (colW + 12);
    const fy = fY + row * 26;
    doc.fillColor(C.greenBot).font('Courier-Bold').fontSize(8)
       .text(name, fx, fy, { width: colW, lineBreak: false });
    doc.fillColor(C.grey).font('Helvetica').fontSize(7.5)
       .text(desc, fx, fy + 11, { width: colW, lineBreak: false });
  }

  // Quick start
  const qY = fY + 4 * 26 + 20;
  doc.moveTo(ML, qY).lineTo(ML + PW, qY).strokeColor(C.panelMid).lineWidth(0.5).stroke();
  const qTy = qY + 6;   // text baseline row
  const qIy = qY + 4;   // icon row (slightly higher to center with text)
  let qx = ML;
  doc.fillColor(C.grey).font('Helvetica').fontSize(9);
  doc.text('QUICK START', qx, qTy, { lineBreak: false });
  qx += doc.widthOfString('QUICK START') + 8;
  doc.fillColor(C.white).font('Helvetica').fontSize(9);
  doc.text('Press', qx, qTy, { lineBreak: false });
  qx += doc.widthOfString('Press') + 5;
  qx += drawTokens(['Z', '+', 'DR'], qx, qIy);
  doc.fillColor(C.white).font('Helvetica').fontSize(9);
  doc.text('during gameplay to open the practice menu.', qx + 3, qTy, { lineBreak: false });
}

// ─── Page: Welcome ────────────────────────────────────────────────────────────
function pageWelcome() {
  newPage('Welcome');

  doc.fillColor(C.white).font('Helvetica-Bold').fontSize(17)
     .text('Thank You', ML, doc.y);
  doc.y += 5;
  hRule(C.greenBot);
  vspace(2);

  bodyText(
    'Thank you sincerely for testing the beta version of the Star Fox 64 Practice ROM. ' +
    'This ROM attempts to maintain as much vanilla code as possible while unlocking helpful ' +
    'practice tools similar to those found in other N64 practice ROMs.'
  );
  vspace(2);
  bodyText(
    'A huge thank you to the Star Fox 64 decomp team -- this project would not exist without ' +
    'their incredible work reverse-engineering the original game. And to the HIT64 community: ' +
    'thank you for being so welcoming and for all the help and feedback that has shaped this ROM.'
  );
  vspace(2);
  bodyText(
    "This early on, there are bound to be bugs and crashes. Please send over anything you " +
    "find and I'll get it fixed and released as a new version at sageraces.com as soon as possible. " +
    "For future betas, keep an eye on staging.sageraces.com/practice-roms and check the Discord."
  );
  vspace(4);

  callout(
    '(!) SD card save states do NOT work on EverDrive hardware yet. All other features ' +
    '-- RAM save states, frame advance, cheats, HUD overlays, macros, level select -- work normally.',
    'warning'
  );

  vspace(2);
  sectionHeader("WHAT'S IN THIS ROM");

  const features: [string, string][] = [
    ['SAVE STATES',    'Save and instantly reload your exact position and game state at any point.'],
    ['FRAME ADVANCE',  'Pause gameplay and step forward one frame at a time for precise analysis.'],
    ['CHEATS',         'INF HEALTH, INF BOMBS, INF LIVES, INF BOOST, and auto charge-shot.'],
    ['HUD OVERLAYS',   'Hit counter, speed, lag frames, charge timing, and missed input tracking.'],
    ['VISUALIZERS',    'Colored hitboxes on enemies, scenery, items, and the player ship.'],
    ['ENEMY HP BARS',  'Live health bars on enemies and bosses, sortable and filterable.'],
    ['LEVEL SELECT',   'Warp to any stage or checkpoint directly from the practice menu.'],
    ['LOADOUT EDITOR', 'Change laser, bombs, lives, rings, health bar, and wingman status mid-run.'],
    ['INPUT MACROS',   'Record and replay button sequences for routing analysis.'],
    ['SD CARD SAVES',  '(In development) Save states to SD card for persistent run history.'],
  ];

  for (const [name, desc] of features) {
    const y0 = doc.y;
    const bh = 18;
    if (y0 + bh > H - MB) { newPage('Welcome'); }
    doc.fillColor(C.greenBot).font('Courier-Bold').fontSize(8.5)
       .text(name, ML + 4, y0 + 4, { width: 100, lineBreak: false });
    doc.fillColor(C.textMain).font('Helvetica').fontSize(8.5)
       .text(desc, ML + 108, y0 + 4, { width: PW - 112, lineBreak: false });
    doc.moveTo(ML, y0 + bh).lineTo(ML + PW, y0 + bh).strokeColor('#282840').lineWidth(0.3).stroke();
    doc.y = y0 + bh;
  }

  vspace(8);
  sectionHeader('REPORTING BUGS');
  bullet([
    'Note the level you were on, what you were doing, and what happened.',
    'Video or screenshot clips are very helpful.',
    'Send reports directly to the project maintainer.',
    'Crashes on specific levels, unexpected menu behavior, and broken saves are top priority.',
  ]);
}

// ─── Page: Controls ──────────────────────────────────────────────────────────
function pageControls() {
  newPage('Controls');

  sectionHeader('OPENING THE MENU');
  bodyText('Press  Z + D-pad Right  at any time during active gameplay to open the radial practice menu.');
  vspace(4);

  sectionHeader('DURING GAMEPLAY  (menu CLOSED)');
  callout(
    'These D-pad shortcuts ONLY work while the radial menu is closed. ' +
    'Once the menu is open, D-pad directional inputs do nothing.',
    'info'
  );

  iconCtrlTable([
    { btns: ['Z', '+', 'DR'],   val: 'Open the radial practice menu' },
    { btns: ['DD'],             val: 'Pause / unpause frame advance' },
    { btns: ['DU'],             val: 'Advance one frame (only while paused)' },
    { btns: ['DL'],             val: 'Save state to the active slot' },
    { btns: ['DR'],             val: 'Load state from the active slot' },
  ]);

  vspace(4);
  sectionHeader('WHILE MENU IS OPEN');
  callout(
    'D-pad directional shortcuts are disabled while the menu is open. ' +
    'Stick controls the menu. Only these controls apply:',
    'info'
  );

  iconCtrlTable([
    { btns: ['A'],              val: 'Confirm the highlighted option' },
    { btns: ['START'],          val: 'Return to the title screen  (hold)' },
    { btns: ['L', '/', 'R'],    val: 'Cycle the active save slot (shown in HUD)' },
    { btns: ['Z'],              val: 'Save to SD card', note: '(!) NOT WORKING ON EVERDRIVE' },
    { btns: ['Z', '+', 'B'],    val: 'Load from SD card', note: '(!) NOT WORKING ON EVERDRIVE' },
  ]);

  vspace(4);
  sectionHeader('SUBMENU NAVIGATION');
  bodyText('After selecting a menu option (e.g. DISPLAY or LOADOUT), a list submenu opens:');
  vspace(2);

  iconCtrlTable([
    { btns: ['DU'],             val: 'Move cursor up' },
    { btns: ['DD'],             val: 'Move cursor down' },
    { btns: ['DL'],             val: 'Decrease / previous value' },
    { btns: ['DR'],             val: 'Increase / next value' },
    { btns: ['A'],              val: 'Toggle option on/off or enter a sub-submenu' },
    { btns: ['B'],              val: 'Go back / close submenu' },
  ]);
}

// ─── Page: Radial Menu ────────────────────────────────────────────────────────
function pageRadialMenu() {
  newPage('Radial Menu');

  sectionHeader('THE RADIAL PRACTICE MENU');
  bodyText(
    'Press Z + D-pad Right to open the menu. Tilt the control stick toward any option, then press A to confirm. ' +
    'Options are arranged like a compass -- stick up selects RESTART, stick right selects SAVE, etc.'
  );
  vspace(6);

  const diagCY = doc.y + 108;
  radialDiagram(W / 2, diagCY, 88);
  doc.y = diagCY + 88 + 26;

  vspace(4);
  sectionHeader('MENU OPTIONS');

  const menuOpts = [
    { name: 'RESTART', dir: 'stick up',    desc: 'Restart the current level from the beginning.' },
    { name: 'DISPLAY', dir: 'up-right',     desc: 'Toggle HUD overlays: hit counter, speed, lag frames, input display, minimap, and more.' },
    { name: 'SAVE',    dir: 'stick right',  desc: 'Open save state manager -- browse slots, save to a specific slot.' },
    { name: 'LOAD',    dir: 'down-right',   desc: 'Instantly load the most recent state in the active slot.' },
    { name: 'LEVELS',  dir: 'stick down',   desc: 'Warp to any level or checkpoint.' },
    { name: 'LOADOUT', dir: 'down-left',    desc: 'Change laser, bombs, lives, rings, health bar, wings, and wingman status.' },
    { name: 'CHEATS',  dir: 'stick left',   desc: 'Toggle cheats: INF HP, INF BOMBS, INF LIVES, INF BOOST, auto charge-shot.' },
    { name: 'SD CARD', dir: 'up-left',      desc: '(!) NOT WORKING ON EVERDRIVE -- SD card save state I/O (in development).' },
  ];

  for (const opt of menuOpts) {
    const y0 = doc.y;
    const bh = 18;
    if (y0 + bh > H - MB) { newPage('Radial Menu'); }
    doc.fillColor(C.greenBot).font('Courier-Bold').fontSize(8.5)
       .text(opt.name, ML + 4, y0 + 3, { width: 68, lineBreak: false });
    doc.fillColor(C.textDim).font('Helvetica').fontSize(7.5)
       .text(opt.dir, ML + 76, y0 + 4, { width: 70, lineBreak: false });
    doc.fillColor(C.textMain).font('Helvetica').fontSize(8.5)
       .text(opt.desc, ML + 150, y0 + 3, { width: PW - 154, lineBreak: false });
    doc.moveTo(ML, y0 + bh).lineTo(ML + PW, y0 + bh).strokeColor('#282840').lineWidth(0.3).stroke();
    doc.y = y0 + bh;
  }
}

// ─── Page: Save States + Frame Advance ───────────────────────────────────────
function pageSaveStates() {
  newPage('Save States');

  sectionHeader('SAVE STATES');
  bodyText(
    'Save states capture your exact game position, ship state, enemy layout, and level progress. ' +
    'Load them instantly to retry difficult sections without replaying from the start.'
  );
  vspace(4);

  subHeader('QUICK SAVE / LOAD  (D-pad, menu closed)');
  iconCtrlTable([
    { btns: ['DL'], val: 'Save state to the active slot' },
    { btns: ['DR'], val: 'Load state from the active slot' },
  ]);

  subHeader('SLOT MANAGEMENT  (menu open)');
  iconCtrlTable([
    { btns: ['L', '/', 'R'], val: 'Cycle to the previous / next save slot' },
  ]);
  ctrlTable([
    { key: 'SAVE option', val: 'Open full save menu -- browse slots, save to a specific slot' },
    { key: 'LOAD option', val: 'Load from the active slot directly' },
  ]);

  vspace(2);
  callout(
    'The active slot number is shown in the HUD overlay. ' +
    'Cycle slots with L/R while the menu is open, then save/load with D-pad LEFT/RIGHT during play.',
    'tip'
  );
  vspace(6);

  sectionHeader('FRAME ADVANCE');
  bodyText(
    'Pause the game at any moment and step forward one frame at a time. ' +
    'Useful for analyzing inputs, routing, and hitbox interactions.'
  );
  vspace(4);

  iconCtrlTable([
    { btns: ['DD'], val: 'Toggle pause / unpause' },
    { btns: ['DU'], val: 'Advance exactly one frame (only while paused)' },
    { btns: ['DL'], val: 'Save state (works while paused)' },
    { btns: ['DR'], val: 'Load state (works while paused)' },
  ]);

  callout(
    'Frame advance and save states work together -- pause with D-DOWN, step to the exact frame, ' +
    'then D-LEFT to snapshot that moment.',
    'tip'
  );

  vspace(6);
  sectionHeader('SD CARD SAVES  (!)');
  callout(
    '(!) SD card saving is NOT functional on EverDrive hardware. ' +
    'The SD CARD option is visible in the menu but saving/loading to SD will silently fail. ' +
    'Use RAM save slots instead -- they work on all hardware but are cleared on power-off.',
    'warning'
  );
  vspace(2);
  bodyText('SD card saves will eventually allow:', 4);
  bullet([
    'Persistent save states that survive power cycles (RAM saves do not)',
    'Sharing save files between consoles',
    'Multiple named save files browsable from the menu',
  ], 8);
}

// ─── Page: Cheats + Loadout ───────────────────────────────────────────────────
function pageCheats() {
  newPage('Cheats & Loadout');

  sectionHeader('CHEATS');
  bodyText(
    'Select CHEATS from the radial menu. A secondary radial dial appears -- ' +
    'tilt the stick toward a cheat and press A to toggle it on or off. ' +
    'Active cheats stay on until toggled off or the game restarts.'
  );
  vspace(6);

  const cheats: { name: string; full: string; color: [number, number, number]; desc: string }[] = [
    { name: 'INF HP',    full: 'INF HEALTH',      color: [220, 190,  40], desc: 'Player cannot take damage. Health bar locked at maximum.' },
    { name: 'INF BOMB',  full: 'INF BOMBS',        color: [200,  55,  55], desc: 'Smart Bomb count never depletes. Fire as many bombs as you want without running out.' },
    { name: 'INF LIFE',  full: 'INF LIVES',        color: [120, 120, 140], desc: 'Lives counter is locked. Game over screens are prevented -- you always continue.' },
    { name: 'INF BST',   full: 'INF BOOST',        color: [ 90, 190, 240], desc: 'Boost meter never depletes. Boost and brake freely without penalty. Laser never overheats.' },
    { name: 'AUTO SHOT', full: 'AUTO CHARGE SHOT', color: [ 60, 180,  80], desc: 'Automatically fires a charge shot when the charge meter is full, without releasing A.' },
  ];

  for (const cheat of cheats) {
    doc.font('Helvetica').fontSize(9);
    const descH = doc.heightOfString(cheat.desc, { width: PW - 82 });
    const bh = Math.max(44, descH + 22);

    if (doc.y + bh > H - MB) { newPage('Cheats & Loadout'); }

    const y0  = doc.y;
    const hex = rgbToHex(...cheat.color);
    const txt = contrastText(...cheat.color);   // auto contrast text on pill

    doc.rect(ML, y0, PW, bh).fill('#0F1026');
    doc.rect(ML, y0, 5, bh).fill(hex);

    const pillX = ML + 12;
    const pillY = y0 + 8;
    doc.roundedRect(pillX, pillY, 54, 16, 2.5).fill(hex);
    doc.fillColor(txt).font('Courier-Bold').fontSize(7)
       .text(cheat.name, pillX + 2, pillY + 5, { width: 50, align: 'center', lineBreak: false });

    doc.fillColor(C.textDim).font('Helvetica').fontSize(8)
       .text(cheat.full, ML + 72, y0 + 7, { lineBreak: false });
    doc.fillColor(C.textMain).font('Helvetica').fontSize(9)
       .text(cheat.desc, ML + 72, y0 + 20, { width: PW - 82 });

    doc.y = y0 + bh + 4;
  }

  vspace(8);
  sectionHeader('LOADOUT');
  bodyText("Select LOADOUT from the radial menu to customize your ship's equipment mid-run:");
  vspace(4);

  ctrlTable([
    { key: 'LASER',   val: 'Cycle laser type: SINGLE -> TWIN -> HYPER' },
    { key: 'BOMBS',   val: 'Set Smart Bomb count (0-9)' },
    { key: 'LIVES',   val: 'Set extra life count' },
    { key: 'RINGS',   val: 'Set ring (shield) count' },
    { key: 'HEALTH',  val: 'Toggle health bar length: SHORT (3 hits) or LONG (full)' },
    { key: 'R WING',  val: 'Toggle right wing: NONE / BROKEN / INTACT' },
    { key: 'L WING',  val: 'Toggle left wing: NONE / BROKEN / INTACT' },
    { key: 'FALCO',   val: 'Toggle Falco: ALIVE or DOWN' },
    { key: 'SLIPPY',  val: 'Toggle Slippy: ALIVE or DOWN' },
    { key: 'PEPPY',   val: 'Toggle Peppy: ALIVE or DOWN' },
    { key: 'EXPERT',  val: 'Toggle expert mode on / off' },
    { key: 'HITS',    val: 'Set current hit count (score counter)' },
    { key: 'PLANETS', val: 'Configure which previous planets count as cleared' },
  ]);
}

// ─── Page: Display & Visualizers ─────────────────────────────────────────────
function pageDisplay() {
  newPage('Display & HUD');

  sectionHeader('DISPLAY OVERLAYS');
  bodyText(
    'Select DISPLAY from the radial menu to toggle HUD information overlays. ' +
    'These render over the normal game HUD without affecting gameplay.'
  );
  vspace(4);

  subHeader('STATS submenu');
  ctrlTable([
    { key: 'HUD',    val: 'Master toggle for the entire practice HUD overlay' },
    { key: 'LAG',    val: 'Frame lag counter -- frames where the game dropped below 60 fps' },
    { key: 'SPEED',  val: 'Current ship speed (base + boost modifier)' },
    { key: 'CHARGE', val: 'Charge shot timing -- shows charge level and lock-on state' },
    { key: 'MISSED', val: 'Counts inputs buffered but not registered by the engine' },
    { key: 'HITS',   val: 'Real-time hit count synced with the scoring engine' },
  ]);

  vspace(4);
  subHeader('Other display options');
  ctrlTable([
    { key: 'SCENES',  val: 'Cutscene handling: PLAY (normal) or SKIP (auto-advance)' },
    { key: 'INPUT',   val: 'Show on-screen input display (button presses visualized)' },
    { key: 'MINIMAP', val: 'Show a minimap overlay when the game is paused' },
  ]);

  vspace(6);
  sectionHeader('VISUALIZERS');
  bodyText(
    'Inside the DISPLAY submenu, VISUALIZERS draws colored overlays on in-game objects ' +
    'to show hitbox boundaries and spawn zones.'
  );
  vspace(4);

  subHeader('HITBOXES submenu');
  ctrlTable([
    { key: 'HITBOXES',  val: 'Master toggle for all hitbox visualization' },
    { key: '  ACTORS',  val: 'Hitboxes on enemies and bosses' },
    { key: '  SCENERY', val: 'Hitboxes on level geometry and obstacles' },
    { key: '  ITEMS',   val: 'Hitboxes on collectible items (rings, bombs, etc.)' },
    { key: '  PLAYER',  val: "The player ship's own hitbox" },
    { key: '  FLASH',   val: 'Flash hitboxes on hit detection events' },
  ]);

  subHeader('SPAWN ZONES submenu');
  ctrlTable([
    { key: 'SPAWN ZONES', val: 'Master toggle for spawn zone visualization' },
    { key: '  ENEMIES',   val: 'Enemy spawn trigger zones (red)' },
    { key: '  ITEMS',     val: 'Item spawn trigger zones (green)' },
    { key: '  SCENERY',   val: 'Scenery spawn trigger zones (blue)' },
  ]);

  vspace(4);
  sectionHeader('ENEMY HP DISPLAY');
  bodyText('The ENEMY HP submenu (inside VISUALIZERS) shows live health bars:');
  vspace(2);
  ctrlTable([
    { key: 'SHOW',   val: 'Enable enemy HP bar overlay' },
    { key: 'SORT',   val: 'Sort bars by: NEAREST enemy or HIGH HP first' },
    { key: 'MIN HP', val: 'Hide enemies below this HP threshold (or OFF)' },
    { key: 'FILTER', val: 'Show ALL enemies or BOSSES only' },
    { key: 'MODELS', val: 'ON = normal 3-D models, OFF = ghost mode (models hidden)' },
  ]);
}

// ─── Page: Macros + Level Select ─────────────────────────────────────────────
function pageMacros() {
  newPage('Input Macros');

  sectionHeader('INPUT MACROS');
  bodyText(
    'Input macros let you record a sequence of button inputs and replay them with perfect timing. ' +
    'Useful for routing analysis. Access via the MACRO option inside the DISPLAY submenu.'
  );
  vspace(6);

  subHeader('RECORDING');
  ctrlTable([
    { key: 'RECORD: ARMED', val: 'Armed -- will auto-start on next input, or at boot if SAVE START is ON' },
    { key: 'RECORD: START',  val: 'Active -- every button press is being captured' },
    { key: 'RECORD: STOP',   val: 'Stop recording and save the macro' },
  ]);

  vspace(4);
  subHeader('PLAYBACK');
  ctrlTable([
    { key: 'PLAY: START', val: 'Begin playing back the recorded macro' },
    { key: 'PLAY: STOP',  val: 'Stop playback' },
    { key: 'REWIND',      val: 'Rewind playback to the beginning' },
    { key: 'TRIM',        val: 'Trim the macro to the current playback position' },
  ]);

  vspace(4);
  subHeader('OPTIONS');
  ctrlTable([
    { key: 'SAVE START', val: 'ON = recording auto-arms at game startup (TAS-style)' },
    { key: 'LOOP',       val: 'ON = macro loops continuously after reaching the end' },
    { key: 'LEN',        val: 'Shows recorded macro length in frames and seconds' },
  ]);

  vspace(4);
  callout(
    'Macros are stored in RAM and cleared on power cycle. ' +
    'They work best with save states -- load a state, arm the macro, replay from the exact same position.',
    'tip'
  );

  vspace(8);
  sectionHeader('LEVEL SELECT');
  bodyText(
    'Select LEVELS from the radial menu to warp directly to any stage or checkpoint. ' +
    'Navigate the planet map the same way as the in-game end-of-level route selection.'
  );
  vspace(4);
  callout(
    'The level select warps to the chosen stage starting checkpoint. ' +
    'Wingman status and planet clear flags can be adjusted in the LOADOUT menu.',
    'info'
  );
}

// ─── Page: SD Card ────────────────────────────────────────────────────────────
function pageSDCard() {
  newPage('SD Card (In Development)');

  const y0 = doc.y;
  const wH = 76;
  doc.rect(ML, y0, PW, wH).fill('#280008');
  doc.rect(ML, y0, PW, wH).strokeColor(C.red).lineWidth(1.2).stroke();
  doc.rect(ML, y0, 6, wH).fill(C.red);
  doc.fillColor(C.red).font('Courier-Bold').fontSize(13)
     .text('(!) SD CARD SAVING NOT AVAILABLE', ML + 14, y0 + 10, { width: PW - 20, lineBreak: false });
  doc.fillColor('#FFB0B8').font('Helvetica').fontSize(9.5)
     .text(
       'SD card save states are in active development and DO NOT work on EverDrive hardware. ' +
       'The SD CARD option appears in the menu, but any save or load will silently fail. ' +
       'Do not rely on SD card saves for practice sessions.',
       ML + 14, y0 + 30, { width: PW - 22 }
     );
  doc.y = y0 + wH + 10;

  sectionHeader('CURRENT STATUS');
  bullet([
    'SD card interface: implemented and visible in the menu',
    'EverDrive X7 / X8 support: written but UNVERIFIED on real hardware',
    'Status: not safe to use -- data loss or crash possible',
    'RAM save states are fully functional -- use those instead (cleared on power-off)',
  ]);

  vspace(6);
  sectionHeader('WHAT SD SAVES WILL DO (WHEN READY)');
  bullet([
    'Save states to the SD card inside your EverDrive -- no Controller Pak needed',
    'States persist across power cycles',
    'Multiple save files per game, browsable from the menu',
    'Shareable save files -- copy to PC and share with other runners',
  ]);

  vspace(6);
  sectionHeader('IN THE MEANTIME: RAM SAVES');
  bodyText('RAM save states work fully right now (cleared on power-off):');
  vspace(2);
  bullet([
    'D-PAD LEFT to save, D-PAD RIGHT to load -- instant, every frame',
    'Multiple slots -- cycle with L/R while the menu is open',
    'Works alongside frame advance -- save/load while paused',
    'Fast and reliable on all hardware configurations',
  ]);

  vspace(6);
  callout(
    'For updates on SD card support, check staging.sageraces.com. ' +
    'When SD saving goes live, a new ROM version will be released at sageraces.com.',
    'info'
  );

  vspace(6);
  sectionHeader('TIPS & NOTES');
  bullet([
    'The practice menu can be opened on any frame, even the first frame of a level.',
    'All cheats and overlays are per-session. Resetting the N64 restores vanilla defaults.',
    'Save states include the current hit count -- snapshot mid-level scores.',
    'Cutscene skip (SCENES: SKIP) auto-advances through level intros and dialog.',
    'The auto charge shot cheat fires on charge completion without releasing A.',
    'Lag frame counter shows how often the engine dropped a frame -- useful for routing.',
    'Hitbox visualization works in all render modes with frame advance for precise analysis.',
  ]);
}

// ─── Main ─────────────────────────────────────────────────────────────────────
function main() {
  const outPath = path.join(path.dirname(__dirname), 'sf64_practice_guide.pdf');
  doc = new PDFDocument({ size: 'A4', margin: 0, autoFirstPage: false });
  const stream = fs.createWriteStream(outPath);
  doc.pipe(stream);

  pageCover();
  pageWelcome();
  pageControls();
  pageRadialMenu();
  pageSaveStates();
  pageCheats();
  pageDisplay();
  pageMacros();
  pageSDCard();

  doc.end();
  stream.on('finish', () => {
    console.log(`\n  OK  ${outPath}  (${pageNum} pages)`);
  });
}

main();
