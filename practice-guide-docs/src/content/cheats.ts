export type CheatDef = { name: string; full: string; color: [number, number, number]; desc: string };

export const CHEATS: CheatDef[] = [
  { name: 'INF HP',    full: 'INF HEALTH',      color: [220, 190,  40], desc: 'Player cannot take damage. Health bar locked at maximum.' },
  { name: 'INF BOMB',  full: 'INF BOMBS',        color: [200,  55,  55], desc: 'Smart Bomb count never depletes. Fire as many bombs as you want without running out.' },
  { name: 'INF LIFE',  full: 'INF LIVES',        color: [120, 120, 140], desc: 'Lives counter is locked. Game over screens are prevented -- you always continue.' },
  { name: 'INF BST',   full: 'INF BOOST',        color: [ 90, 190, 240], desc: 'Boost meter never depletes. Boost and brake freely without penalty. Laser never overheats.' },
  { name: 'AUTO SHOT', full: 'AUTO CHARGE SHOT', color: [ 60, 180,  80], desc: 'Automatically fires a charge shot when the charge meter is full, without releasing A.' },
];

export const LOADOUT_OPTIONS: { key: string; val: string }[] = [
  { key: 'LASER',   val: 'Cycle laser type: SINGLE -> TWIN -> HYPER' },
  { key: 'BOMBS',   val: 'Set Smart Bomb count (0-9)' },
  { key: 'LIVES',   val: 'Set extra life count' },
  { key: 'RINGS',   val: 'Set ring (shield) count' },
  { key: 'HEALTH',  val: 'Toggle health bar length: SHORT (3 hits) or LONG (full)' },
  { key: 'R WING',  val: 'Toggle right wing: NONE / BROKEN / INTACT' },
  { key: 'L WING',  val: 'Toggle left wing: NONE / BROKEN / INTACT' },
  { key: 'FALCO',   val: 'Toggle Falco: ALIVE or DOWN' },
  { key: 'SLIPPY',  val: 'Toggle Slippy: ALIVE or DOWN' },
  { key: 'PEPPY',   val: 'Toggle Peppy: ALIVE or DOWN' },
  { key: 'EXPERT',  val: 'Toggle expert mode on / off' },
  { key: 'HITS',    val: 'Set current hit count (score counter)' },
  { key: 'PLANETS', val: 'Configure which previous planets count as cleared' },
];
