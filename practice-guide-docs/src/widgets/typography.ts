import { doc } from '../renderer';
import { linearGrad } from '../renderer';
import { C, ML, PW, FONTS } from '../theme';
import { hexToRgb, contrastText } from '../colors';
import { type BtnToken, BTN_SZ, tokenWidth, drawTokens } from './buttons';

// ─── TextSeg type (string or button token array) ──────────────────────────────
export type TextSeg = string | BtnToken[];

// ─── Icon column width for iconCtrlTable ──────────────────────────────────────
const ICON_COL = 110;

// ─── Section header ───────────────────────────────────────────────────────────
export function sectionHeader(title: string) {
  const y = doc.y + 3;
  const h = 27;
  const g = linearGrad(ML, y, ML, y + h, [[0, C.greenTop], [1, C.greenBot]]);
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

// ─── Sub header ───────────────────────────────────────────────────────────────
export function subHeader(title: string) {
  const y = doc.y + 2;
  doc.fillColor(C.greenBot).font('Helvetica-Bold').fontSize(10)
     .text(title, ML, y, { lineBreak: false });
  doc.y += 14;
  doc.moveTo(ML, doc.y).lineTo(ML + PW, doc.y).strokeColor(C.greenBot).lineWidth(0.35).stroke();
  doc.y += 5;
}

// ─── Body text ────────────────────────────────────────────────────────────────
export function bodyText(text: string, indent = 0) {
  doc.fillColor(C.textMain).font('Helvetica').fontSize(9.5)
     .text(text, ML + indent, doc.y, { width: PW - indent });
  doc.y += 3;
}

// ─── Inline body text with embedded button icons ──────────────────────────────
export function inlineBody(segments: TextSeg[], indent = 0) {
  const startX = ML + indent;
  const maxX   = ML + PW;
  let x = startX;
  let y = doc.y;
  const lineH    = 13.5;
  const iconOffY = -1;

  function breakLine() { x = startX; y += lineH; }

  for (const seg of segments) {
    if (Array.isArray(seg)) {
      const w = (seg as BtnToken[]).reduce((s, t) => s + tokenWidth(t), 0);
      if (x + w > maxX && x > startX) breakLine();
      drawTokens(seg as BtnToken[], x, y + iconOffY);
      x += w;
    } else {
      doc.fillColor(C.textMain).font('Helvetica').fontSize(9.5);
      const words = (seg as string).split(' ');
      for (let i = 0; i < words.length; i++) {
        const word = words[i];
        if (!word && i > 0) continue;
        const display = (i < words.length - 1) ? word + ' ' : word;
        const w = doc.widthOfString(display);
        if (x + w > maxX && x > startX) breakLine();
        doc.text(display, x, y, { lineBreak: false });
        x += w;
      }
    }
  }

  doc.y = y + lineH + 3;
}

// ─── Vertical space ───────────────────────────────────────────────────────────
export function vspace(pts = 6) { doc.y += pts; }

// ─── Horizontal rule ─────────────────────────────────────────────────────────
export function hRule(color = C.grey) {
  doc.moveTo(ML, doc.y).lineTo(ML + PW, doc.y).strokeColor(color).lineWidth(0.35).stroke();
  doc.y += 4;
}

// ─── Bullet list ─────────────────────────────────────────────────────────────
export function bullet(items: string[], indent = 0) {
  doc.font('Helvetica').fontSize(9.5);
  for (const item of items) {
    const bx = ML + indent + 5;
    const ty = doc.y;
    doc.circle(bx, ty + 5.5, 1.8).fill(C.border);
    doc.fillColor(C.textMain).text(item, bx + 8, ty, { width: PW - indent - 14 });
    doc.y += 1;
  }
}

// ─── Key/value control table ─────────────────────────────────────────────────
export function ctrlTable(rows: { key: string; val: string }[]) {
  const rowH = 17;
  const keyW = 135;
  for (let i = 0; i < rows.length; i++) {
    const y = doc.y;
    if (i % 2 === 0) doc.rect(ML, y, PW, rowH).fill(C.rowAlt);
    doc.fillColor(C.greenBot).font(FONTS.mono).fontSize(8.5)
       .text(rows[i].key, ML + 6, y + 4, { width: keyW, lineBreak: false });
    doc.fillColor(C.textMain).font('Helvetica').fontSize(8.5)
       .text(rows[i].val, ML + keyW + 8, y + 4, { width: PW - keyW - 14, lineBreak: false });
    doc.y = y + rowH;
  }
  doc.y += 5;
}

// ─── Icon/button control table ───────────────────────────────────────────────
export function iconCtrlTable(rows: { btns: BtnToken[]; val: string; note?: string }[]) {
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

// ─── Callout box ─────────────────────────────────────────────────────────────
export function callout(text: string, type: 'warning' | 'info' | 'tip' = 'info') {
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
