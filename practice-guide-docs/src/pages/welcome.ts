import { doc, newPage } from '../renderer';
import { C, ML, PW, H, MB, FONTS } from '../theme';
import { sectionHeader, bodyText, hRule, vspace, callout, bullet } from '../widgets/typography';
import { WELCOME_FEATURES } from '../content/features';

export function pageWelcome() {
  newPage('Welcome');

  doc.fillColor(C.white).font('Helvetica-Bold').fontSize(17)
     .text('Thank You', ML, doc.y);
  doc.y += 5;
  hRule(C.greenBot);
  vspace(2);

  bodyText(
    'Thank you sincerely for testing the beta version of the Star Fox 64 Practice ROM. ' +
    'This ROM attempts to maintain as much vanilla functionality as possible while unlocking helpful ' +
    'practice tools similar to those found in other N64 practice ROMs.'
  );
  vspace(2);
  bodyText(
    'A huge thank you to the Star Fox 64 decomp team -- this project would not exist without ' +
    'their work. And to the HIT64 community:' +
    'thank you for being so welcoming and for all the help and feedback that has shaped this ROM.'
  );
  vspace(2);
  bodyText(
    "This early on, there are bound to be bugs and crashes. Please send over anything you " +
    "find and I'll get it fixed and released as a new version at sageraces.com as soon as possible. " +
    "For future betas, keep an eye on staging.sageraces.com/practice-roms and check the Discord."
  );
  vspace(4);

  callout(
    '(!) SD card save states do NOT work on any cart hardware yet (SummerCart64 or ' +
    'EverDrive). All other features -- RAM save states, frame advance, cheats, HUD ' +
    'overlays, macros, level select -- work normally.',
    'warning'
  );

  vspace(2);
  sectionHeader("WHAT'S IN THIS ROM");

  for (const { name, desc } of WELCOME_FEATURES) {
    const bh = 18;
    if (doc.y + bh > H - MB) { newPage('Welcome'); }
    const y0 = doc.y;
    doc.fillColor(C.greenBot).font(FONTS.mono).fontSize(8.5)
       .text(name, ML + 4, y0 + 4, { width: 100, lineBreak: false });
    doc.fillColor(C.textMain).font('Helvetica').fontSize(8.5)
       .text(desc, ML + 108, y0 + 4, { width: PW - 112, lineBreak: false });
    doc.moveTo(ML, y0 + bh).lineTo(ML + PW, y0 + bh).strokeColor('#282840').lineWidth(0.3).stroke();
    doc.y = y0 + bh;
  }

  vspace(8);
  sectionHeader('REPORTING BUGS');
  bullet([
    'Note the level you were on, what you were doing, and what happened.',
    'Video or screenshot clips are very helpful.',
    'Send reports directly to the project maintainer.',
    'Crashes on specific levels, unexpected menu behavior, and broken saves are top priority.',
  ]);
}
