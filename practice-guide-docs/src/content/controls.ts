import type { BtnToken } from '../widgets/buttons';

export const GAMEPLAY_CONTROLS = [
  { btns: ['CR', '+', 'START'] as BtnToken[], val: 'Open the radial practice menu' },
  { btns: ['DD']           as BtnToken[], val: 'Pause / unpause frame advance' },
  { btns: ['DU']           as BtnToken[], val: 'Advance one frame (only while paused)' },
  { btns: ['DL']           as BtnToken[], val: 'Save state to the active slot' },
  { btns: ['DR']           as BtnToken[], val: 'Load state from the active slot' },
];

export const MENU_CONTROLS = [
  { btns: ['A']              as BtnToken[], val: 'Confirm the highlighted option' },
  { btns: ['START']          as BtnToken[], val: 'Return to the title screen  (hold)' },
  { btns: ['L', '/', 'R']   as BtnToken[], val: 'Cycle the active save slot (shown in HUD)' },
  { btns: ['Z']              as BtnToken[], val: 'Save to SD card',    note: '(!) NOT WORKING ON ANY CART' },
  { btns: ['Z', '+', 'B']   as BtnToken[], val: 'Load from SD card',  note: '(!) NOT WORKING ON ANY CART' },
];

export const SUBMENU_NAV = [
  { btns: ['DU'] as BtnToken[], val: 'Move cursor up' },
  { btns: ['DD'] as BtnToken[], val: 'Move cursor down' },
  { btns: ['DL'] as BtnToken[], val: 'Decrease / previous value' },
  { btns: ['DR'] as BtnToken[], val: 'Increase / next value' },
  { btns: ['A']  as BtnToken[], val: 'Toggle option on/off or enter a sub-submenu' },
  { btns: ['B']  as BtnToken[], val: 'Go back / close submenu' },
];
