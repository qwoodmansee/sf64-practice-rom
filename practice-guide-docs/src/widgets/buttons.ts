import { doc } from '../renderer';
import { C, FONTS } from '../theme';

// ─── Button token type ────────────────────────────────────────────────────────
export type BtnToken = 'A' | 'B' | 'Z' | 'R' | 'L' | 'START' | 'CR' | 'DU' | 'DD' | 'DL' | 'DR' | '+' | '/';

export const BTN_SZ = 12;

// ─── Token width (exported for tests and inlineBody) ─────────────────────────
export function tokenWidth(tok: BtnToken): number {
  if (tok === 'Z') return 22;
  if (tok === 'R' || tok === 'L') return 24;
  if (tok === 'START') return 14;
  if (tok === 'CR') return 14;
  if (tok === 'A') return 14;
  if (tok === 'B') return 12;
  if (tok === '+') return 9;
  if (tok === '/') return 7;
  return 15; // DPad directions
}

// ─── Private draw functions ───────────────────────────────────────────────────
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

function drawBtnCRight(x: number, y: number, sz = BTN_SZ): number {
  const r = sz / 2;
  const cx = x + r, cy = y + r;
  doc.circle(cx, cy, r).fill('#E8B800');
  // right-pointing arrow, vector-drawn (text glyphs garble in PDFKit)
  doc.polygon([cx - 2.5, cy - 3], [cx + 3.5, cy], [cx - 2.5, cy + 3]).fill('#403000');
  return sz + 2;
}

function drawBtnDPad(x: number, y: number, dir?: 'U' | 'D' | 'L' | 'R', sz = 13): number {
  const arm = Math.round(sz / 3);
  const base = '#303038';
  const hi   = C.greenBot;
  doc.rect(x + arm, y, arm, sz).fill(base);
  doc.rect(x, y + arm, sz, arm).fill(base);
  if (dir === 'U') doc.rect(x + arm, y,          arm, arm).fill(hi);
  if (dir === 'D') doc.rect(x + arm, y + arm * 2, arm, arm).fill(hi);
  if (dir === 'L') doc.rect(x,       y + arm,     arm, arm).fill(hi);
  if (dir === 'R') doc.rect(x + arm * 2, y + arm, arm, arm).fill(hi);
  return sz + 2;
}

// ─── Public draw function ─────────────────────────────────────────────────────
export function drawTokens(tokens: BtnToken[], x: number, y: number): number {
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
      case 'CR':    ox += drawBtnCRight(ox, y); break;
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
