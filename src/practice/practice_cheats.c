#include "practice.h"

#ifdef PRACTICE_ROM

#define PRACTICE_CHEAT_MAX_BOMBS 9
#define PRACTICE_CHEAT_MAX_LIVES 99

void Practice_Cheats_Apply(void) {
    Player* p;

    if (!gPracticeConfig.infHealth && !gPracticeConfig.infBombs && !gPracticeConfig.infLives &&
        !gPracticeConfig.infBoost) {
        return;
    }

    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }
    if (gPlayer == NULL) {
        return;
    }

    p = &gPlayer[0];

    if (gPracticeConfig.infHealth) {
        p->shields = Play_GetMaxShields();
    }
    if (gPracticeConfig.infBombs) {
        gBombCount[gPlayerNum] = PRACTICE_CHEAT_MAX_BOMBS;
    }
    if (gPracticeConfig.infLives) {
        gLifeCount[gPlayerNum] = PRACTICE_CHEAT_MAX_LIVES;
    }
    if (gPracticeConfig.infBoost) {
        p->boostMeter = 0.0f;
        p->boostCooldown = false;
    }
}

#endif
