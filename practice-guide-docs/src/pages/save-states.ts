import { newPage } from '../renderer';
import { sectionHeader, subHeader, bodyText, vspace, callout, bullet, iconCtrlTable, ctrlTable, inlineBody } from '../widgets/typography';

export function pageSaveStates() {
  newPage('Save States');

  sectionHeader('SAVE STATES');
  bodyText(
    'Save states capture your exact game position, ship state, enemy layout, and level progress. ' +
    'Load them instantly to retry difficult sections without replaying from the start.'
  );
  vspace(4);

  subHeader('QUICK SAVE / LOAD  (D-pad, menu closed)');
  iconCtrlTable([
    { btns: ['DL'], val: 'Save state to the active slot' },
    { btns: ['DR'], val: 'Load state from the active slot' },
  ]);

  subHeader('SLOT MANAGEMENT  (menu open)');
  iconCtrlTable([
    { btns: ['L', '/', 'R'], val: 'Cycle to the previous / next save slot' },
  ]);
  ctrlTable([
    { key: 'SAVE option', val: 'Open full save menu -- browse slots, save to a specific slot' },
    { key: 'LOAD option', val: 'Load from the active slot directly' },
  ]);

  vspace(2);
  callout(
    'The active slot number is shown in the HUD overlay. ' +
    'Cycle slots with L/R while the menu is open, then save/load with D-pad LEFT/RIGHT during play.',
    'tip'
  );
  vspace(6);

  sectionHeader('FRAME ADVANCE');
  inlineBody([
    'Pause with ', ['DD'], ' at any moment and step forward ', ['DU'],
    ' one frame at a time. Useful for analyzing inputs, routing, and hitbox interactions.',
  ]);
  vspace(2);

  iconCtrlTable([
    { btns: ['DD'], val: 'Toggle pause / unpause' },
    { btns: ['DU'], val: 'Advance exactly one frame (only while paused)' },
    { btns: ['DL'], val: 'Save state (works while paused)' },
    { btns: ['DR'], val: 'Load state (works while paused)' },
  ]);

  callout(
    'Frame advance and save states work together -- pause with D-DOWN, step to the exact frame, ' +
    'then D-LEFT to snapshot that moment.',
    'tip'
  );

  vspace(6);
  sectionHeader('SD CARD SAVES  (!)');
  callout(
    '(!) SD card saving is NOT functional on any cart hardware (SummerCart64 or EverDrive). ' +
    'The SD CARD option is visible in the menu but saving/loading to SD will silently fail. ' +
    'Use RAM save slots instead -- they work on all hardware but are cleared on power-off.',
    'warning'
  );
  vspace(2);
  bodyText('SD card saves will eventually allow:', 4);
  bullet([
    'Persistent save states that survive power cycles (RAM saves do not)',
    'Sharing save files between consoles',
    'Multiple named save files browsable from the menu',
  ], 8);
}
