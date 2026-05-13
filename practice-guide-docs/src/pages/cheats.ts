import { doc, newPage } from '../renderer';
import { C, ML, PW, H, MB, FONTS } from '../theme';
import { rgbToHex, contrastText } from '../colors';
import { sectionHeader, bodyText, vspace, ctrlTable } from '../widgets/typography';
import { CHEATS, LOADOUT_OPTIONS } from '../content/cheats';

export function pageCheats() {
  newPage('Cheats & Loadout');

  sectionHeader('CHEATS');
  bodyText(
    'Select CHEATS from the radial menu. A secondary radial dial appears -- ' +
    'tilt the stick toward a cheat and press A to toggle it on or off. ' +
    'Active cheats stay on until toggled off or the game restarts.'
  );
  vspace(6);

  for (const cheat of CHEATS) {
    doc.font('Helvetica').fontSize(9);
    const descH = doc.heightOfString(cheat.desc, { width: PW - 82 });
    const bh = Math.max(44, descH + 22);

    if (doc.y + bh > H - MB) { newPage('Cheats & Loadout'); }

    const y0  = doc.y;
    const hex = rgbToHex(...cheat.color);
    const txt = contrastText(...cheat.color);

    doc.rect(ML, y0, PW, bh).fill('#0F1026');
    doc.rect(ML, y0, 5, bh).fill(hex);

    const pillX = ML + 12;
    const pillY = y0 + 8;
    doc.roundedRect(pillX, pillY, 54, 16, 2.5).fill(hex);
    doc.fillColor(txt).font(FONTS.mono).fontSize(7)
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

  ctrlTable(LOADOUT_OPTIONS);
}
