import { doc, newPage } from '../renderer';
import { C, W, ML, PW, H, MB, FONTS } from '../theme';
import { sectionHeader, vspace, inlineBody } from '../widgets/typography';
import { radialDiagram } from '../widgets/radial';
import { RADIAL_SLICES, MENU_OPTS } from '../content/radial-menu';

export function pageRadialMenu() {
  newPage('Radial Menu');

  sectionHeader('THE RADIAL PRACTICE MENU');
  inlineBody([
    'Press ', ['CR', '+', 'START'], ' to open the menu. Tilt the control stick toward any option, ' +
    'then press ', ['A'], ' to confirm. ' +
    'Options are arranged like a compass -- stick up selects RESTART, stick right selects SAVE, etc.',
  ]);
  vspace(4);

  const diagCY = doc.y + 108;
  radialDiagram(W / 2, diagCY, RADIAL_SLICES, 88);
  doc.y = diagCY + 88 + 26;

  vspace(4);
  sectionHeader('MENU OPTIONS');

  for (const opt of MENU_OPTS) {
    const bh = 18;
    if (doc.y + bh > H - MB) { newPage('Radial Menu'); }
    const y0 = doc.y;
    doc.fillColor(C.greenBot).font(FONTS.mono).fontSize(8.5)
       .text(opt.name, ML + 4, y0 + 3, { width: 68, lineBreak: false });
    doc.fillColor(C.textDim).font('Helvetica').fontSize(7.5)
       .text(opt.dir, ML + 76, y0 + 4, { width: 70, lineBreak: false });
    doc.fillColor(C.textMain).font('Helvetica').fontSize(8.5)
       .text(opt.desc, ML + 150, y0 + 3, { width: PW - 154, lineBreak: false });
    doc.moveTo(ML, y0 + bh).lineTo(ML + PW, y0 + bh).strokeColor('#282840').lineWidth(0.3).stroke();
    doc.y = y0 + bh;
  }
}
