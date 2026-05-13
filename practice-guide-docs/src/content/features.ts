export type Feature = { name: string; desc: string };

export const COVER_FEATURES: Feature[] = [
  { name: 'SAVE STATES',   desc: 'Save + load your exact game position instantly' },
  { name: 'FRAME ADVANCE', desc: 'Pause and step forward one frame at a time' },
  { name: 'CHEATS',        desc: 'Infinite health, bombs, lives, and boost meter' },
  { name: 'HUD OVERLAYS',  desc: 'Hit counter, speed, lag frames, charge timing' },
  { name: 'VISUALIZERS',   desc: 'Colored hitboxes, spawn zones, live enemy HP bars' },
  { name: 'LEVEL SELECT',  desc: 'Warp to any stage or checkpoint directly' },
  { name: 'LOADOUT',       desc: 'Laser, bombs, lives, wings, wingmen — editable mid-run' },
  { name: 'INPUT MACROS',  desc: 'Record and replay button sequences' },
];

export const WELCOME_FEATURES: Feature[] = [
  { name: 'SAVE STATES',    desc: 'Save and instantly reload your exact position and game state at any point.' },
  { name: 'FRAME ADVANCE',  desc: 'Pause gameplay and step forward one frame at a time for precise analysis.' },
  { name: 'CHEATS',         desc: 'INF HEALTH, INF BOMBS, INF LIVES, INF BOOST, and auto charge-shot.' },
  { name: 'HUD OVERLAYS',   desc: 'Hit counter, speed, lag frames, charge timing, and missed input tracking.' },
  { name: 'VISUALIZERS',    desc: 'Colored hitboxes on enemies, scenery, items, and the player ship.' },
  { name: 'ENEMY HP BARS',  desc: 'Live health bars on enemies and bosses, sortable and filterable.' },
  { name: 'LEVEL SELECT',   desc: 'Warp to any stage or checkpoint directly from the practice menu.' },
  { name: 'LOADOUT EDITOR', desc: 'Change laser, bombs, lives, rings, health bar, and wingman status mid-run.' },
  { name: 'INPUT MACROS',   desc: 'Record and replay button sequences for routing analysis.' },
  { name: 'SD CARD SAVES',  desc: '(In development) Save states to SD card for persistent run history.' },
];
