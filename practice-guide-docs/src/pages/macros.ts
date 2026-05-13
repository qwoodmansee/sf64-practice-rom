import { newPage } from '../renderer';
import { sectionHeader, subHeader, bodyText, vspace, callout, ctrlTable } from '../widgets/typography';

export function pageMacros() {
  newPage('Input Macros');

  sectionHeader('INPUT MACROS');
  bodyText(
    'Input macros let you record a sequence of button inputs and replay them with perfect timing. ' +
    'Useful for routing analysis. Access via the MACRO option inside the DISPLAY submenu.'
  );
  vspace(6);

  subHeader('RECORDING');
  ctrlTable([
    { key: 'RECORD: ARMED', val: 'Armed -- will auto-start on next input, or at boot if SAVE START is ON' },
    { key: 'RECORD: START',  val: 'Active -- every button press is being captured' },
    { key: 'RECORD: STOP',   val: 'Stop recording and save the macro' },
  ]);

  vspace(4);
  subHeader('PLAYBACK');
  ctrlTable([
    { key: 'PLAY: START', val: 'Begin playing back the recorded macro' },
    { key: 'PLAY: STOP',  val: 'Stop playback' },
    { key: 'REWIND',      val: 'Rewind playback to the beginning' },
    { key: 'TRIM',        val: 'Trim the macro to the current playback position' },
  ]);

  vspace(4);
  subHeader('OPTIONS');
  ctrlTable([
    { key: 'SAVE START', val: 'ON = recording auto-arms at game startup (TAS-style)' },
    { key: 'LOOP',       val: 'ON = macro loops continuously after reaching the end' },
    { key: 'LEN',        val: 'Shows recorded macro length in frames and seconds' },
  ]);

  vspace(4);
  callout(
    'Macros are stored in RAM and cleared on power cycle. ' +
    'They work best with save states -- load a state, arm the macro, replay from the exact same position.',
    'tip'
  );

  vspace(8);
  sectionHeader('LEVEL SELECT');
  bodyText(
    'Select LEVELS from the radial menu to warp directly to any stage or checkpoint. ' +
    'Navigate the planet map the same way as the in-game end-of-level route selection.'
  );
  vspace(4);
  callout(
    'The level select warps to the chosen stage starting checkpoint. ' +
    'Wingman status and planet clear flags can be adjusted in the LOADOUT menu.',
    'info'
  );
}
