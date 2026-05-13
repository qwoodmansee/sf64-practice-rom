import { doc, bumpPageNum, drawStarfield } from '../renderer';
import { C, W, H, ML, PW, HBAR, FONTS } from '../theme';
import { drawTokens } from '../widgets/buttons';
import { COVER_FEATURES } from '../content/features';

export function pageCover() {
  bumpPageNum();
  doc.addPage();
  doc.rect(0, 0, W, H).fill(C.bg);
  drawStarfield();

  // Custom header (not via newPage)
  doc.rect(0, 0, W, HBAR).fill(C.panelDark);
  doc.rect(0, HBAR, W, 1).fill(C.border);
  doc.fillColor(C.greenBot).font(FONTS.mono).fontSize(9)
     .text('SF64 PRACTICE ROM  -  BETA', ML, 9, { width: PW, align: 'center', lineBreak: false });

  // Custom footer
  doc.rect(0, H - HBAR, W, HBAR).fill(C.panelDark);
  doc.rect(0, H - HBAR, W, 1).fill(C.border);
  doc.fillColor(C.grey).font('Helvetica').fontSize(8)
     .text('sageraces.com  -  staging.sageraces.com/practice-roms (beta)', ML, H - 17, { width: PW, align: 'center', lineBreak: false });

  const ruleY1 = 90;
  doc.rect(ML, ruleY1, PW, 1.5).fill(C.border);
  doc.rect(ML + 50, ruleY1 - 3, PW - 100, 1).fill(C.panelMid);

  const ruleY2 = H - 90;
  doc.rect(ML, ruleY2, PW, 1.5).fill(C.border);
  doc.rect(ML + 50, ruleY2 + 2, PW - 100, 1).fill(C.panelMid);

  const titleY = 130;
  doc.fillColor(C.white).font(FONTS.mono).fontSize(50)
     .text('STAR FOX 64', ML, titleY, { width: PW, align: 'center', lineBreak: false });
  doc.fillColor(C.grey).font(FONTS.mono).fontSize(19)
     .text('PRACTICE ROM', ML, titleY + 63, { width: PW, align: 'center', lineBreak: false });

  const dotY = titleY + 98;
  for (let i = 0; i < 5; i++) {
    const dx = W / 2 - 24 + i * 12;
    doc.circle(dx, dotY, i === 2 ? 3.5 : 2).fill(i === 2 ? C.greenBot : C.border);
  }
  doc.fillColor(C.border).font('Helvetica').fontSize(11)
     .text('BETA USER GUIDE', ML, dotY + 14, { width: PW, align: 'center', lineBreak: false });

  const wY = 338;
  const wH = 96;
  doc.rect(ML, wY, PW, wH).fill('#280008');
  doc.rect(ML, wY, PW, wH).strokeColor(C.red).lineWidth(0.9).stroke();
  doc.rect(ML, wY, 5, wH).fill(C.red);
  doc.fillColor(C.red).font(FONTS.mono).fontSize(10.5)
     .text('(!) SD CARD SAVING: NOT WORKING ON EVERDRIVE', ML + 14, wY + 11, { width: PW - 20, lineBreak: false });
  doc.moveTo(ML + 14, wY + 28).lineTo(ML + PW - 8, wY + 28).strokeColor('#6B0010').lineWidth(0.5).stroke();
  doc.fillColor('#FFB0B8').font('Helvetica').fontSize(9.5)
     .text(
       'SD card save states are in development and do NOT function on EverDrive hardware. ' +
       'RAM save states (D-pad shortcuts) work on all hardware but are cleared on power-off. ' +
       'See the SD Card section for full details.',
       ML + 14, wY + 36, { width: PW - 22 }
     );

  const featY = 454;
  doc.fillColor(C.border).font(FONTS.mono).fontSize(9)
     .text('WHAT IS INCLUDED', ML, featY, { width: PW, align: 'center', lineBreak: false });
  doc.moveTo(ML + 60, featY + 14).lineTo(W - ML - 60, featY + 14).strokeColor(C.border).lineWidth(0.5).stroke();

  const fY = featY + 24;
  const colW = PW / 2 - 6;
  for (let i = 0; i < COVER_FEATURES.length; i++) {
    const { name, desc } = COVER_FEATURES[i];
    const col = i % 2;
    const row = Math.floor(i / 2);
    const fx = ML + col * (colW + 12);
    const fy = fY + row * 26;
    doc.fillColor(C.greenBot).font(FONTS.mono).fontSize(8)
       .text(name, fx, fy, { width: colW, lineBreak: false });
    doc.fillColor(C.grey).font('Helvetica').fontSize(7.5)
       .text(desc, fx, fy + 11, { width: colW, lineBreak: false });
  }

  // Quick start with inline button icons
  const qY = fY + 4 * 26 + 20;
  doc.moveTo(ML, qY).lineTo(ML + PW, qY).strokeColor(C.panelMid).lineWidth(0.5).stroke();
  const qTy = qY + 6;
  const qIy = qY + 4;
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
