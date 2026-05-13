import { doc, newPage, linearGrad } from '../renderer';
import { C, W, ML, PW, FONTS } from '../theme';
import { hexToRgb, contrastText } from '../colors';
import { BTN_SZ, drawTokens } from '../widgets/buttons';
import { SHORTCUT_COLS, PATTERN_CARDS, type ShortcutCol, type PatternCard } from '../content/quick-ref';

export function pageQuickRef() {
  newPage('Quick Reference');

  vspaceLocal(4);
  doc.fillColor(C.white).font(FONTS.mono).fontSize(20)
     .text('SHORTCUT REFERENCE', ML, doc.y, { width: PW, align: 'center', lineBreak: false });
  doc.y += 26;
  const ruleX = W / 2 - 55;
  doc.rect(ruleX, doc.y, 110, 1).fill(C.greenBot);
  doc.y += 12;

  // ── Top section: two shortcut columns side by side ─────────────────────────
  const COL_GAP      = 10;
  const COL_W        = (PW - COL_GAP) / 2;
  const COL_R        = ML + COL_W + COL_GAP;
  const ROW_H        = 24;
  const SECTION_HDR_H = 18;
  const ICON_W       = 90;
  const FONT_SZ      = 9.5;
  const HDR_H        = 24;

  function colContentH(col: ShortcutCol): number {
    return col.sections.reduce((h, s) =>
      h + (s.sectionLabel ? SECTION_HDR_H : 0) + s.rows.length * ROW_H, 0);
  }

  // Draw a column using explicit y-tracking — never relies on doc.y drifting.
  function drawCol(col: ShortcutCol, x: number, colW: number, startY: number) {
    const g = linearGrad(x, startY, x, startY + HDR_H, [[0, col.colorTop], [1, col.colorBot]]);
    doc.rect(x, startY, colW, HDR_H).fill(g);
    doc.rect(x, startY + HDR_H - 1, colW, 1).fill(col.colorBot);
    doc.fillColor(C.white).font('Helvetica-Bold').fontSize(9)
       .text(col.title, x + 8, startY + 8,
             { width: colW - 16, lineBreak: false, characterSpacing: 0.5 });

    let rowY = startY + HDR_H;
    let dataRowIdx = 0;

    for (const section of col.sections) {
      if (section.sectionLabel) {
        doc.rect(x, rowY, colW, SECTION_HDR_H).fill(C.panelDark);
        doc.fillColor(C.textDim).font('Helvetica-Bold').fontSize(7.5)
           .text(section.sectionLabel, x + 8, rowY + (SECTION_HDR_H - 7.5) / 2,
                 { width: colW - 16, lineBreak: false, characterSpacing: 0.8 });
        rowY += SECTION_HDR_H;
      }
      for (const row of section.rows) {
        if (dataRowIdx % 2 === 0) doc.rect(x, rowY, colW, ROW_H).fill(C.rowAlt);
        drawTokens(row.btns, x + 6, rowY + (ROW_H - BTN_SZ) / 2 - 1);
        doc.fillColor(row.note ? '#FFB0B8' : C.textMain).font('Helvetica').fontSize(FONT_SZ)
           .text(row.label, x + ICON_W, rowY + (ROW_H - FONT_SZ * 1.15) / 2,
                 { width: colW - ICON_W - 6, lineBreak: false });
        rowY += ROW_H;
        dataRowIdx++;
      }
    }
  }

  const savedY  = doc.y;
  const blockH  = HDR_H + Math.max(colContentH(SHORTCUT_COLS[0]), colContentH(SHORTCUT_COLS[1]));
  drawCol(SHORTCUT_COLS[0], ML,    COL_W, savedY);
  drawCol(SHORTCUT_COLS[1], COL_R, COL_W, savedY);
  doc.y = savedY + blockH + 14;

  // ── Common patterns section header ─────────────────────────────────────────
  const secY = doc.y;
  const secH = 22;
  const g2 = linearGrad(ML, secY, ML, secY + secH, [[0, C.panelMid], [1, C.panelDark]]);
  doc.rect(ML, secY, PW, secH).fill(g2);
  doc.rect(ML, secY + secH - 1, PW, 1).fill(C.border);
  doc.fillColor(C.white).font('Helvetica-Bold').fontSize(10)
     .text('COMMON PATTERNS', ML + 10, secY + 6, { lineBreak: false, characterSpacing: 1 });
  doc.y = secY + secH + 8;

  // ── Pattern cards (2 columns x 3 rows) ────────────────────────────────────
  const CARD_W   = (PW - COL_GAP) / 2;
  const CARD_H   = 118;
  const CARD_GAP = 8;
  const CARD_HDR = 20;
  const STEP_H   = 17;
  const STEP_ICON_W = 58;

  function drawCard(cx: number, cy: number, card: PatternCard) {
    doc.rect(cx, cy, CARD_W, CARD_H).fill(C.rowAlt);
    doc.rect(cx, cy, CARD_W, CARD_HDR).fill(card.color);
    doc.fillColor(contrastText(...hexToRgb(card.color))).font('Helvetica-Bold').fontSize(8.5)
       .text(card.title, cx + 8, cy + 6, { width: CARD_W - 16, lineBreak: false });

    let sy = cy + CARD_HDR + 4;
    for (const step of card.steps) {
      if (step.btns && step.btns.length > 0) {
        drawTokens(step.btns, cx + 6, sy + 3);
        doc.fillColor(C.textMain).font('Helvetica').fontSize(7.8)
           .text(step.text, cx + STEP_ICON_W, sy + 4, { width: CARD_W - STEP_ICON_W - 6, lineBreak: false });
      } else {
        doc.fillColor(step.dim ? C.textDim : C.textMain).font('Helvetica').fontSize(7.8)
           .text(step.text, cx + 8, sy + 4, { width: CARD_W - 14, lineBreak: false });
      }
      sy += STEP_H;
    }
  }

  const cardStartY = doc.y;
  for (let row = 0; row < 3; row++) {
    const cy = cardStartY + row * (CARD_H + CARD_GAP);
    drawCard(ML,                        cy, PATTERN_CARDS[row * 2]);
    drawCard(ML + CARD_W + COL_GAP, cy, PATTERN_CARDS[row * 2 + 1]);
  }
}

function vspaceLocal(pts = 6) { doc.y += pts; }
