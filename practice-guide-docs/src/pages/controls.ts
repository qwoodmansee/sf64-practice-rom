import { newPage } from '../renderer';
import { sectionHeader, bodyText, vspace, callout, iconCtrlTable } from '../widgets/typography';
import { GAMEPLAY_CONTROLS, MENU_CONTROLS, SUBMENU_NAV } from '../content/controls';

export function pageControls() {
  newPage('Controls');

  sectionHeader('OPENING THE MENU');
  bodyText('Press C-RIGHT + START at any time during active gameplay to open the radial practice menu.');
  vspace(2);

  sectionHeader('DURING GAMEPLAY  (menu CLOSED)');
  callout(
    'These D-pad shortcuts ONLY work while the radial menu is closed. ' +
    'Once the menu is open, D-pad directional inputs do nothing.',
    'info'
  );

  iconCtrlTable(GAMEPLAY_CONTROLS);

  vspace(4);
  sectionHeader('WHILE MENU IS OPEN');
  callout(
    'D-pad directional shortcuts are disabled while the menu is open. ' +
    'Stick controls the menu. Only these controls apply:',
    'info'
  );

  iconCtrlTable(MENU_CONTROLS);

  vspace(4);
  sectionHeader('SUBMENU NAVIGATION');
  bodyText('After selecting a menu option (e.g. DISPLAY or LOADOUT), a list submenu opens:');
  vspace(2);

  iconCtrlTable(SUBMENU_NAV);
}
