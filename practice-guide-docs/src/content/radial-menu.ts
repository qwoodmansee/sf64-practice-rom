import type { RadialSlice } from '../widgets/radial';

export const RADIAL_SLICES: RadialSlice[] = [
  { angle: 270, label: 'RESTART',  rgb: [180,  60,  60] },
  { angle: 315, label: 'DISPLAY',  rgb: [ 60, 160, 160] },
  { angle:   0, label: 'SAVE',     rgb: [ 60, 140, 180] },
  { angle:  45, label: 'LOAD',     rgb: [ 60, 180, 100] },
  { angle:  90, label: 'LEVELS',   rgb: [180, 140,  60] },
  { angle: 135, label: 'LOADOUT',  rgb: [140,  60, 180] },
  { angle: 180, label: 'CHEATS',   rgb: [200,  80,  80] },
  { angle: 225, label: 'SD CARD',  rgb: [ 80, 200, 120] },
];

export const MENU_OPTS = [
  { name: 'RESTART', dir: 'stick up',    desc: 'Restart the current level from the beginning.' },
  { name: 'DISPLAY', dir: 'up-right',    desc: 'Toggle HUD overlays: hit counter, speed, lag frames, input display, minimap, and more.' },
  { name: 'SAVE',    dir: 'stick right', desc: 'Open save state manager -- browse slots, save to a specific slot.' },
  { name: 'LOAD',    dir: 'down-right',  desc: 'Instantly load the most recent state in the active slot.' },
  { name: 'LEVELS',  dir: 'stick down',  desc: 'Warp to any level or checkpoint.' },
  { name: 'LOADOUT', dir: 'down-left',   desc: 'Change laser, bombs, lives, rings, health bar, wings, and wingman status.' },
  { name: 'CHEATS',  dir: 'stick left',  desc: 'Toggle cheats: INF HP, INF BOMBS, INF LIVES, INF BOOST, auto charge-shot.' },
  { name: 'SD CARD', dir: 'up-left',     desc: '(!) NOT WORKING ON EVERDRIVE -- SD card save state I/O (in development).' },
];
