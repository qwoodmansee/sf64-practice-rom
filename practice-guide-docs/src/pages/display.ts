import { newPage } from '../renderer';
import { sectionHeader, subHeader, bodyText, vspace, ctrlTable } from '../widgets/typography';

export function pageDisplay() {
  newPage('Display & HUD');

  sectionHeader('DISPLAY OVERLAYS');
  bodyText(
    'Select DISPLAY from the radial menu to toggle HUD information overlays. ' +
    'These render over the normal game HUD without affecting gameplay.'
  );
  vspace(4);

  subHeader('STATS submenu');
  ctrlTable([
    { key: 'HUD',    val: 'Master toggle for the entire practice HUD overlay' },
    { key: 'LAG',    val: 'Frame lag counter -- frames where the game dropped below 60 fps' },
    { key: 'SPEED',  val: 'Current ship speed (base + boost modifier)' },
    { key: 'CHARGE', val: 'Charge shot timing -- shows charge level and lock-on state' },
    { key: 'MISSED', val: 'Counts inputs buffered but not registered by the engine' },
    { key: 'HITS',   val: 'Real-time hit count synced with the scoring engine' },
  ]);

  vspace(4);
  subHeader('Other display options');
  ctrlTable([
    { key: 'SCENES',  val: 'Cutscene handling: PLAY (normal) or SKIP (auto-advance)' },
    { key: 'INPUT',   val: 'Show on-screen input display (button presses visualized)' },
    { key: 'MINIMAP', val: 'Show a minimap overlay when the game is paused' },
  ]);

  vspace(6);
  sectionHeader('VISUALIZERS');
  bodyText(
    'Inside the DISPLAY submenu, VISUALIZERS draws colored overlays on in-game objects ' +
    'to show hitbox boundaries and spawn zones.'
  );
  vspace(4);

  subHeader('HITBOXES submenu');
  ctrlTable([
    { key: 'HITBOXES',  val: 'Master toggle for all hitbox visualization' },
    { key: '  ACTORS',  val: 'Hitboxes on enemies and bosses' },
    { key: '  SCENERY', val: 'Hitboxes on level geometry and obstacles' },
    { key: '  ITEMS',   val: 'Hitboxes on collectible items (rings, bombs, etc.)' },
    { key: '  PLAYER',  val: "The player ship's own hitbox" },
    { key: '  FLASH',   val: 'Flash hitboxes on hit detection events' },
  ]);

  subHeader('SPAWN ZONES submenu');
  ctrlTable([
    { key: 'SPAWN ZONES', val: 'Master toggle for spawn zone visualization' },
    { key: '  ENEMIES',   val: 'Enemy spawn trigger zones (red)' },
    { key: '  ITEMS',     val: 'Item spawn trigger zones (green)' },
    { key: '  SCENERY',   val: 'Scenery spawn trigger zones (blue)' },
  ]);

  vspace(4);
  sectionHeader('ENEMY HP DISPLAY');
  bodyText('The ENEMY HP submenu (inside VISUALIZERS) shows live health bars:');
  vspace(2);
  ctrlTable([
    { key: 'SHOW',   val: 'Enable enemy HP bar overlay' },
    { key: 'SORT',   val: 'Sort bars by: NEAREST enemy or HIGH HP first' },
    { key: 'MIN HP', val: 'Hide enemies below this HP threshold (or OFF)' },
    { key: 'FILTER', val: 'Show ALL enemies or BOSSES only' },
    { key: 'MODELS', val: 'ON = normal 3-D models, OFF = ghost mode (models hidden)' },
  ]);
}
