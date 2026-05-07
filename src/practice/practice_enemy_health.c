#include "practice.h"

#ifdef PRACTICE_ROM

/* Per-frame camera basis - populated by Practice_EnemyHealth_Draw */
static f32 sCamFwdX, sCamFwdY, sCamFwdZ;
static f32 sCamRightX, sCamRightZ;   /* Y component is always 0 */
static f32 sCamUpX, sCamUpY, sCamUpZ;
static f32 sTanHalfFovX, sTanHalfFovY;

static bool EHealth_FloatValid(f32* fp) {
    return ((*(u32*)fp >> 23) & 0xFF) != 0xFF;
}

/* Format a non-negative integer into buf; caller must provide at least 7 bytes. */
static void EHealth_FormatNum(s32 value, char* buf) {
    s32 i = 0;
    s32 v;
    if (value <= 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    v = value;
    while (v > 0) { i++; v /= 10; }
    buf[i] = '\0';
    v = value;
    while (v > 0) { buf[--i] = '0' + (v % 10); v /= 10; }
}

/* Project a world-space point to screen coordinates using the pre-built camera
 * basis vectors. Returns false if the point is behind the camera or off screen.
 * wz is the actor-relative Z (path-progress offset is applied internally). */
static bool EHealth_WorldToScreen(f32 wx, f32 wy, f32 wz, s32* sx, s32* sy) {
    f32 dx, dy, dz;
    f32 vx, vy, vz;
    f32 ndcX, ndcY;

    dx = wx - gPlayCamEye.x;
    dy = wy - gPlayCamEye.y;
    dz = (wz + gPathProgress) - gPlayCamEye.z;

    vz = dx * sCamFwdX + dy * sCamFwdY + dz * sCamFwdZ;
    if (vz <= 10.0f) { return false; }

    vx = dx * sCamRightX               + dz * sCamRightZ;
    vy = dx * sCamUpX   + dy * sCamUpY + dz * sCamUpZ;

    ndcX = vx / (vz * sTanHalfFovX);
    ndcY = vy / (vz * sTanHalfFovY);

    *sx = (s32)((ndcX + 1.0f) * 160.0f);
    *sy = (s32)((1.0f - ndcY) * 120.0f);

    return (*sx >= 4 && *sx < 296 && *sy >= 8 && *sy < 232);
}

void Practice_EnemyHealth_Draw(void) {
    s32 i;
    s32 sx, sy;
    s32 boxW;
    f32 len;
    f32 halfFovRad, sinHalf, cosHalf;
    Actor* a;
    Boss* b;
    s32 hp;
    char buf[7];

    if (!gPracticeConfig.showEnemyHealth) {
        return;
    }
    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    /* Ghost mode: black out the entire frame so only HP numbers remain. */
    if (gPracticeConfig.enemyHealthHideModels) {
        Practice_DrawBox(0, 0, 320, 240, 0, 0, 0, 255);
    }

    /* Build camera orthonormal basis from gPlayCamEye/gPlayCamAt.
     * forward = normalize(at - eye) */
    sCamFwdX = gPlayCamAt.x - gPlayCamEye.x;
    sCamFwdY = gPlayCamAt.y - gPlayCamEye.y;
    sCamFwdZ = gPlayCamAt.z - gPlayCamEye.z;
    len = sqrtf(sCamFwdX * sCamFwdX + sCamFwdY * sCamFwdY + sCamFwdZ * sCamFwdZ);
    if (len < 0.001f) { return; }
    sCamFwdX /= len; sCamFwdY /= len; sCamFwdZ /= len;

    /* right = normalize(forward x worldUp) - worldUp = (0,1,0)
     * cross gives (-fwdZ, 0, fwdX); Y component is always zero */
    sCamRightX = -sCamFwdZ;
    sCamRightZ =  sCamFwdX;
    len = sqrtf(sCamRightX * sCamRightX + sCamRightZ * sCamRightZ);
    if (len < 0.001f) { return; }
    sCamRightX /= len;
    sCamRightZ /= len;

    /* up = right x forward */
    sCamUpX =  0.0f      * sCamFwdZ - sCamRightZ * sCamFwdY;
    sCamUpY =  sCamRightZ * sCamFwdX - sCamRightX * sCamFwdZ;
    sCamUpZ =  sCamRightX * sCamFwdY - 0.0f      * sCamFwdX;

    /* Projection constants from live gFovY */
    halfFovRad  = gFovY * 0.5f * M_DTOR;
    sinHalf     = __sinf(halfFovRad);
    cosHalf     = __cosf(halfFovRad);
    if (cosHalf < 0.001f) { return; }
    sTanHalfFovY = sinHalf / cosHalf;
    sTanHalfFovX = sTanHalfFovY * (320.0f / 240.0f);

    /* Actors */
    if (!gPracticeConfig.enemyHealthBossOnly) {
        for (i = 0; i < 60; i++) {
            a = &gActors[i];
            if (a->obj.status == OBJ_FREE) { continue; }
            hp = (s32)a->health;
            if (hp <= gPracticeConfig.enemyHealthMinHp) { continue; }
            if (!EHealth_FloatValid(&a->obj.pos.x) ||
                !EHealth_FloatValid(&a->obj.pos.y) ||
                !EHealth_FloatValid(&a->obj.pos.z)) { continue; }
            if (!EHealth_WorldToScreen(a->obj.pos.x, a->obj.pos.y, a->obj.pos.z,
                                       &sx, &sy)) { continue; }

            /* backdrop + number centered on projected position */
            boxW = (hp >= 100) ? 31 : (hp >= 10) ? 22 : 13;
            Practice_DrawBox(sx - boxW / 2 - 2, sy - 11, boxW + 4, 10, 0, 0, 0, 160);
            Practice_DrawNumber(sx - boxW / 2, sy - 10, hp);
        }
    }

    /* Bosses */
    for (i = 0; i < 4; i++) {
        b = &gBosses[i];
        if (b->obj.status == OBJ_FREE) { continue; }
        hp = (s32)b->health;
        if (hp <= gPracticeConfig.enemyHealthMinHp) { continue; }
        if (!EHealth_WorldToScreen(b->obj.pos.x, b->obj.pos.y, b->obj.pos.z,
                                   &sx, &sy)) { continue; }

        boxW = (hp >= 10000) ? 49 : (hp >= 1000) ? 40 : (hp >= 100) ? 31 : (hp >= 10) ? 22 : 13;
        Practice_DrawBox(sx - boxW / 2 - 2, sy - 11, boxW + 4, 10, 0, 0, 0, 180);
        EHealth_FormatNum(hp, buf);
        Practice_DrawTextColor(sx - boxW / 2, sy - 10, buf, 255, 200, 80);
    }
}

#endif
