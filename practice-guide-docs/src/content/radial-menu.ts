import type { RadialSlice } from '../widgets/radial';

// Matches the root radial exactly (src/practice/practice_menu.c, sRootEntries /
// Root_GetSlice). 7 slices on a 45-degree octant layout; SE is intentionally
// unused -- SAVE/LOAD were removed from the root radial and now run from the
// DL/DR gameplay hotkeys (RAM saves) or the SD CARD sub-radial (SD saves).
export const RADIAL_SLICES: RadialSlice[] = [
  { angle: 270, label: 'RESTART',  rgb: [180,  60,  60] },
  { angle: 315, label: 'DISPLAY',  rgb: [ 60, 160, 160] },
  { angle:   0, label: 'AUDIO',    rgb: [ 90, 140, 220] },
  { angle:  90, label: 'LEVELS',   rgb: [180, 140,  60] },
  { angle: 135, label: 'LOADOUT',  rgb: [140,  60, 180] },
  { angle: 180, label: 'CHEATS',   rgb: [200,  80,  80] },
  { angle: 225, label: 'SD CARD',  rgb: [ 80, 200, 120] },
];

export const MENU_OPTS = [
  { name: 'RESTART', dir: 'stick up',    desc: 'Instantly restarts the current level from the beginning -- no confirmation.' },
  { name: 'DISPLAY', dir: 'up-right',    desc: 'Opens the Display sub-radial -- toggle cutscene skip, free camera, input display, charge-shot meter, and STATS/VISUALIZERS/MACRO submenus.' },
  { name: 'AUDIO',   dir: 'stick right', desc: 'Opens the audio volume menu -- adjust music, SFX, and voice volume sliders.' },
  { name: 'LEVELS',  dir: 'stick down',  desc: 'Opens level select -- warp to any level or checkpoint.' },
  { name: 'LOADOUT', dir: 'down-left',   desc: 'Opens the loadout menu -- change laser, bombs, lives, rings, health bar, wings, and wingman status.' },
  { name: 'CHEATS',  dir: 'stick left',  desc: 'Opens the Cheats sub-radial -- toggle INF HP, INF BOMBS, INF LIVES, INF BOOST, and auto charge-shot.' },
  { name: 'SD CARD', dir: 'up-left',     desc: 'Opens the SD Card sub-radial -- SD SAVE / SD LOAD (early alpha, see SD Card page).' },
];
