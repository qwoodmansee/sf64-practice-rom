import { doc } from '../renderer';
import { C, ML, PW, FONTS } from '../theme';
import { rgbToHex, contrastText } from '../colors';

// ─── RadialSlice type ─────────────────────────────────────────────────────────
export type RadialSlice = {
  angle: number;
  label: string;
  rgb: [number, number, number];
};

// ─── Radial menu diagram ──────────────────────────────────────────────────────
export function radialDiagram(cx: number, cy: number, slices: RadialSlice[], R = 88) {
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
    doc.fillColor(contrastText(...s.rgb)).font(FONTS.mono).fontSize(6.2)
       .text(s.label, px - PW2 / 2 + 2, py - 4.5, { width: PW2 - 4, align: 'center', lineBreak: false });
  }

  doc.circle(cx, cy, 28).fillAndStroke(C.panelMid, C.border);
  doc.fillColor(C.grey).font(FONTS.mono).fontSize(5.5);
  doc.text('STICK', cx - 14, cy - 8, { width: 28, align: 'center', lineBreak: false });
  doc.text('THEN A', cx - 14, cy - 1, { width: 28, align: 'center', lineBreak: false });

  doc.fillColor(C.grey).font('Helvetica').fontSize(5.5);
  doc.text('^ UP',   cx - 8,       cy - R - 20, { lineBreak: false });
  doc.text('v DOWN', cx - 10,      cy + R + 10, { lineBreak: false });
  doc.text('>',      cx + R + 14,  cy - 3,      { lineBreak: false });
  doc.text('<',      cx - R - 18,  cy - 3,      { lineBreak: false });
}
