import { doc, newPage } from '../renderer';
import { C, ML, PW, FONTS } from '../theme';
import { sectionHeader, vspace, callout, bullet, inlineBody } from '../widgets/typography';

export function pageSDCard() {
  newPage('SD Card (Early Alpha)');

  const y0 = doc.y;
  const wH = 84;
  doc.rect(ML, y0, PW, wH).fill('#280008');
  doc.rect(ML, y0, PW, wH).strokeColor(C.red).lineWidth(1.2).stroke();
  doc.rect(ML, y0, 6, wH).fill(C.red);
  doc.fillColor(C.red).font(FONTS.mono).fontSize(13)
     .text('(!) SD CARD SAVING: EARLY ALPHA', ML + 14, y0 + 10, { width: PW - 20, lineBreak: false });
  doc.fillColor('#FFB0B8').font('Helvetica').fontSize(9.5)
     .text(
       'SD card save states are in early alpha. They have been tested and are working on ' +
       'SummerCart64, but crashes have been observed and are still possible -- use with caution ' +
       'and keep RAM save states as your backup for anything you care about. EverDrive X7/X8 ' +
       'support is written but unverified on real hardware. If you hit a crash, please send us ' +
       'what you were doing (see REPORTING CRASHES below) -- that info directly helps us fix it.',
       ML + 14, y0 + 30, { width: PW - 22 }
     );
  doc.y = y0 + wH + 10;

  sectionHeader('CURRENT STATUS');
  bullet([
    'SD card interface: implemented and visible in the menu',
    'SummerCart64 support: early alpha -- tested and working, but crashes have been observed',
    'EverDrive X7 / X8 support: written but UNVERIFIED on real hardware',
    'Status: usable with caution -- not yet fully stable, crashes are possible',
    'RAM save states are fully functional and crash-free -- use those for anything critical',
  ]);

  vspace(6);
  sectionHeader('WHAT SD SAVES DO');
  bullet([
    'Save states to the SD card inside your cart (SummerCart64 / EverDrive) -- no Controller Pak needed',
    'States persist across power cycles',
    'Multiple save files per game, browsable from the menu',
    'Shareable save files -- copy to PC and share with other runners',
  ]);

  vspace(6);
  sectionHeader('FOR ANYTHING CRITICAL: RAM SAVES');
  inlineBody([
    'Use ', ['DL'], ' to save and ', ['DR'],
    ' to load -- instant, every frame, on all hardware, and does not carry the crash risk ' +
    'SD saves currently do. Slots survive across levels but are cleared on power-off.',
  ]);
  vspace(2);
  bullet([
    'Multiple slots -- cycle with L/R while the menu is open',
    'Works alongside frame advance -- save/load while paused',
    'Fast and reliable on all hardware configurations',
  ]);

  vspace(6);
  sectionHeader('REPORTING CRASHES');
  bullet([
    'Note the level/scene you were on and exactly what you were doing (saving, loading, browsing files).',
    'Note your cart hardware (SummerCart64 or EverDrive) and SD card size/format if known.',
    'Video or screenshot clips of the crash are very helpful.',
    'Send reports directly to the project maintainer -- see REPORTING BUGS on the Welcome page.',
    'SD save/load crashes are top priority -- this info is what moves SD saving out of alpha.',
  ]);

  vspace(6);
  callout(
    'SD card saving is under active development. For updates, check staging.sageraces.com/practice-roms. ' +
    'Stability fixes will be released as new ROM versions at sageraces.com.',
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
