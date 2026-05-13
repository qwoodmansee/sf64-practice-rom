import { doc, newPage } from '../renderer';
import { C, ML, PW, FONTS } from '../theme';
import { sectionHeader, vspace, callout, bullet, inlineBody } from '../widgets/typography';

export function pageSDCard() {
  newPage('SD Card (In Development)');

  const y0 = doc.y;
  const wH = 76;
  doc.rect(ML, y0, PW, wH).fill('#280008');
  doc.rect(ML, y0, PW, wH).strokeColor(C.red).lineWidth(1.2).stroke();
  doc.rect(ML, y0, 6, wH).fill(C.red);
  doc.fillColor(C.red).font(FONTS.mono).fontSize(13)
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
  inlineBody([
    'Use ', ['DL'], ' to save and ', ['DR'],
    ' to load -- instant, every frame, on all hardware. Slots survive across levels ' +
    'but are cleared on power-off.',
  ]);
  vspace(2);
  bullet([
    'Multiple slots -- cycle with L/R while the menu is open',
    'Works alongside frame advance -- save/load while paused',
    'Fast and reliable on all hardware configurations',
  ]);

  vspace(6);
  callout(
    'For updates on SD card support, check staging.sageraces.com/practice-roms. ' +
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
