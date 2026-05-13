import { doc, newPage, linearGrad } from '../renderer';
import { C, W, ML, PW, FONTS } from '../theme';
import { hexToRgb, contrastText } from '../colors';
import { type BtnToken, BTN_SZ, drawTokens } from '../widgets/buttons';
import { SHORTCUT_COLS, PATTERN_CARDS, type PatternCard } from '../content/quick-ref';

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
  const COL_GAP  = 10;
  const COL_W    = (PW - COL_GAP) / 2;
  const COL_R    = ML + COL_W + COL_GAP;
  const ROW_H    = 24;
  const ICON_W   = 90;
  const FONT_SZ  = 9.5;
  const HDR_H    = 24;

  const leftRows = SHORTCUT_COLS[0].rows.length;
  const blockH   = HDR_H + leftRows * ROW_H;

  function drawCol(
    x: number, colW: number,
    title: string, colorTop: string, colorBot: string,
    rows: typeof SHORTCUT_COLS[0]['rows']
  ) {
    const g = linearGrad(x, doc.y, x, doc.y + HDR_H, [[0, colorTop], [1, colorBot]]);
    doc.rect(x, doc.y, colW, HDR_H).fill(g);
    doc.rect(x, doc.y + HDR_H - 1, colW, 1).fill(colorBot);
    doc.fillColor(C.white).font('Helvetica-Bold').fontSize(9)
       .text(title, x + 8, doc.y + 8, { width: colW - 16, lineBreak: false, characterSpacing: 0.5 });
    rows.forEach(({ btns, label, note }, i) => {
      const ry = doc.y + HDR_H + i * ROW_H;
      if (i % 2 === 0) doc.rect(x, ry, colW, ROW_H).fill(C.rowAlt);
      drawTokens(btns, x + 6, ry + (ROW_H - BTN_SZ) / 2 - 1);
      doc.fillColor(note ? '#FFB0B8' : C.textMain).font('Helvetica').fontSize(FONT_SZ)
         .text(label, x + ICON_W, ry + (ROW_H - FONT_SZ * 1.15) / 2,
               { width: colW - ICON_W - 6, lineBreak: false });
    });
    // Fill remaining rows with blank alternating bg so columns match height
    for (let i = rows.length; i < leftRows; i++) {
      const ry = doc.y + HDR_H + i * ROW_H;
      if (i % 2 === 0) doc.rect(x, ry, colW, ROW_H).fill(C.rowAlt);
    }
  }

  const savedY = doc.y;
  drawCol(ML, COL_W,
    SHORTCUT_COLS[0].title,
    SHORTCUT_COLS[0].colorTop, SHORTCUT_COLS[0].colorBot,
    SHORTCUT_COLS[0].rows
  );
  doc.y = savedY;
  drawCol(COL_R, COL_W,
    SHORTCUT_COLS[1].title,
    SHORTCUT_COLS[1].colorTop, SHORTCUT_COLS[1].colorBot,
    SHORTCUT_COLS[1].rows
  );
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
