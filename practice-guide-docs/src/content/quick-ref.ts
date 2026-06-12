import type { BtnToken } from '../widgets/buttons';

export type ShortcutRow     = { btns: BtnToken[]; label: string; note?: string };
export type ShortcutSection = { sectionLabel?: string; rows: ShortcutRow[] };
export type ShortcutCol     = { title: string; colorTop: string; colorBot: string; sections: ShortcutSection[] };
export type PatternStep  = { btns?: BtnToken[]; text: string; dim?: boolean };
export type PatternCard  = { title: string; color: string; steps: PatternStep[] };

export const SHORTCUT_COLS: [ShortcutCol, ShortcutCol] = [
  {
    title: 'DURING GAMEPLAY  (menu closed)',
    colorTop: '#0F5E1F', colorBot: '#7FC224',
    sections: [
      {
        sectionLabel: 'ON LEVEL SELECT:',
        rows: [
          { btns: ['DU', 'DD', 'DL', 'DR'], label: 'Navigate' },
          { btns: ['A'],                     label: 'Start level' },
          { btns: ['START'],                 label: 'Set loadout before starting' },
        ],
      },
      {
        sectionLabel: 'DURING GAMEPLAY:',
        rows: [
          { btns: ['DL'], label: 'Save state' },
          { btns: ['DR'], label: 'Load state' },
        ],
      },
    ],
  },
  {
    title: 'WHILE MENU IS OPEN',
    colorTop: '#0D1848', colorBot: '#3D42A0',
    sections: [
      {
        rows: [
          { btns: ['A'],           label: 'Confirm option' },
          { btns: ['L', '/', 'R'], label: 'Cycle save slot' },
          { btns: ['START'],       label: 'Return to title  (hold)' },
        ],
      },
    ],
  },
];

export const PATTERN_CARDS: PatternCard[] = [
  {
    title: 'MANAGE SLOTS', color: '#7FC224',
    steps: [
      { btns: ['CR', '+', 'START'],   text: 'Open menu' },
      { btns: ['L', '/', 'R'],    text: 'Cycle active slot (in menu)' },
      { btns: ['DL'],             text: 'Save to active slot' },
      { btns: ['DR'],             text: 'Load from active slot' },
      { dim: true,                text: 'Open SAVE option to browse all slots' },
    ],
  },
  {
    title: 'FRAME ADVANCE', color: '#50A0FF',
    steps: [
      { btns: ['DD'],   text: 'Toggle pause on / off' },
      { btns: ['DU'],   text: 'Step one frame  (while paused)' },
      { btns: ['DL'],   text: 'Save state  (works while paused)' },
      { btns: ['DR'],   text: 'Load state  (works while paused)' },
    ],
  },
  {
    title: 'CHANGING LEVELS', color: '#E08020',
    steps: [
      { btns: ['CR', '+', 'START'],  text: 'Open menu' },
      { dim: true,               text: 'Stick DOWN  -> LEVELS, press A' },
      { dim: true,               text: 'Navigate planet map with stick' },
      { btns: ['A'],             text: 'Confirm warp' },
    ],
  },
  {
    title: 'CHANGE VISUALIZERS', color: '#A040C0',
    steps: [
      { btns: ['CR', '+', 'START'],  text: 'Open menu' },
      { dim: true,               text: 'Stick UP-RIGHT  -> DISPLAY, press A' },
      { dim: true,               text: 'Navigate to VISUALIZERS  -> HITBOXES' },
      { btns: ['A'],             text: 'Toggle on / off' },
      { btns: ['DL', 'DR'],      text: 'Change color scheme' },
    ],
  },
  {
    title: 'INPUT MONITOR', color: '#20A8A0',
    steps: [
      { btns: ['CR', '+', 'START'],  text: 'Open menu' },
      { dim: true,               text: 'Stick UP-RIGHT  -> DISPLAY, press A' },
      { dim: true,               text: 'Navigate to INPUT' },
      { btns: ['A'],             text: 'Toggle on / off' },
    ],
  },
  {
    title: 'SAVE TO SD CARD', color: '#FB1A32',
    steps: [
      { btns: ['CR', '+', 'START'],  text: 'Open menu' },
      { btns: ['Z'],             text: 'Save state to SD' },
      { btns: ['Z', '+', 'B'],   text: 'Load state from SD' },
      { dim: true,               text: '(!) NOT WORKING ON ANY CART' },
    ],
  },
];
