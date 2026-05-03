#include "practice.h"

#ifdef PRACTICE_ROM

#define MMAP_BX     196
#define MMAP_BY     138
#define MMAP_BW     116
#define MMAP_BH      96
#define MMAP_CX     254
#define MMAP_CY     186
#define MMAP_RADIUS  42

static f32 Minimap_WorldHalfSize(void) {
    switch (gCurrentLevel) {
        case LEVEL_SECTOR_Z: return 20000.0f;
        case LEVEL_CORNERIA: return 8000.0f;
        case LEVEL_BOLSE:    return 10000.0f;
        default:             return 12500.0f;
    }
}

static s32 Minimap_ToScreenX(f32 wx, f32 halfSize) {
    s32 px = (s32)((wx / halfSize) * (f32)MMAP_RADIUS);
    if (px < -MMAP_RADIUS) px = -MMAP_RADIUS;
    if (px > MMAP_RADIUS)  px = MMAP_RADIUS;
    return MMAP_CX + px;
}

static s32 Minimap_ToScreenZ(f32 wz, f32 halfSize) {
    s32 py = (s32)((wz / halfSize) * (f32)MMAP_RADIUS);
    if (py < -MMAP_RADIUS) py = -MMAP_RADIUS;
    if (py > MMAP_RADIUS)  py = MMAP_RADIUS;
    return MMAP_CY + py;
}

static bool Minimap_FloatValid(f32* fp) {
    return ((*(u32*)fp >> 23) & 0xFF) != 0xFF;
}

/* Scanline-fill triangle with integer vertices. */
static void Minimap_FillTriangle(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2,
                                  u8 r, u8 g, u8 b) {
    s32 tmp, y, xa, xb;

    /* Sort by screen Y ascending */
    if (y0 > y1) { tmp=y0;y0=y1;y1=tmp; tmp=x0;x0=x1;x1=tmp; }
    if (y1 > y2) { tmp=y1;y1=y2;y2=tmp; tmp=x1;x1=x2;x2=tmp; }
    if (y0 > y1) { tmp=y0;y0=y1;y1=tmp; tmp=x0;x0=x1;x1=tmp; }

    if (y2 == y0) return;

    for (y = y0; y <= y2; y++) {
        xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        if (y <= y1) {
            xb = (y1 != y0) ? (x0 + (x1 - x0) * (y - y0) / (y1 - y0)) : x1;
        } else {
            xb = (y2 != y1) ? (x1 + (x2 - x1) * (y - y1) / (y2 - y1)) : x2;
        }
        if (xa > xb) { tmp = xa; xa = xb; xb = tmp; }
        if (xb >= xa) Practice_DrawBox(xa, y, xb - xa + 1, 1, r, g, b, 220);
    }
}

/*
 * Draw a filled triangle at (cx,cy) pointing in heading_deg direction.
 * Screen +X = world +X, screen +Y = world +Z (our minimap convention).
 * Apex 5px forward, base 2px back and +-3px wide.
 */
static void Minimap_DrawTriangle(s32 cx, s32 cy, f32 heading_deg, u8 r, u8 g, u8 b) {
    f32 sinH = SIN_DEG(heading_deg);
    f32 cosH = COS_DEG(heading_deg);
    /* SF64 rot.y=0 faces -Z; negate to get screen-space forward */
    f32 fwdX = -sinH;
    f32 fwdY = -cosH;
    /* Right = 90 deg CW of forward */
    f32 rgtX = -cosH;
    f32 rgtY =  sinH;

    s32 ax  = cx + (s32)(fwdX * 5.0f);
    s32 ay  = cy + (s32)(fwdY * 5.0f);
    s32 blx = cx - (s32)(fwdX * 2.0f) + (s32)(rgtX * 3.0f);
    s32 bly = cy - (s32)(fwdY * 2.0f) + (s32)(rgtY * 3.0f);
    s32 brx = cx - (s32)(fwdX * 2.0f) - (s32)(rgtX * 3.0f);
    s32 bry = cy - (s32)(fwdY * 2.0f) - (s32)(rgtY * 3.0f);

    Minimap_FillTriangle(ax, ay, blx, bly, brx, bry, r, g, b);
}


void Practice_Minimap_Draw(void) {
    s32 i;
    Actor* actor;
    Boss* boss;
    f32 halfSize;
    s32 sx, sy;

    if (!gPracticeConfig.showPauseMinimap) return;
    if (gLevelMode != LEVELMODE_ALL_RANGE) return;
    if (!Practice_FreeCam_IsActive()) {
        if (gPlayState != PLAY_PAUSE) return;
        if (gPracticeMenuState != PMENU_CLOSED) return;
    }

    halfSize = Minimap_WorldHalfSize();

    Practice_DrawBox(MMAP_BX, MMAP_BY, MMAP_BW, MMAP_BH, 0, 0, 0, 170);
    Practice_DrawBox(MMAP_BX + 1, MMAP_BY + 1, MMAP_BW - 2, 1, 60, 60, 80, 200);
    Practice_DrawBox(MMAP_BX + 1, MMAP_BY + MMAP_BH - 2, MMAP_BW - 2, 1, 60, 60, 80, 200);
    Practice_DrawBox(MMAP_BX + 1, MMAP_BY + 1, 1, MMAP_BH - 2, 60, 60, 80, 200);
    Practice_DrawBox(MMAP_BX + MMAP_BW - 2, MMAP_BY + 1, 1, MMAP_BH - 2, 60, 60, 80, 200);

    Practice_DrawTextColor(MMAP_BX + 4, MMAP_BY + 4, "MAP", 100, 180, 255);

    /* Faint crosshair at arena center */
    Practice_DrawBox(MMAP_CX - 12, MMAP_CY, 25, 1, 40, 40, 60, 140);
    Practice_DrawBox(MMAP_CX, MMAP_CY - 12, 1, 25, 40, 40, 60, 140);

    /* Actors: OBJ_ACTOR_ALLRANGE only */
    for (i = 0, actor = &gActors[0]; i < ARRAY_COUNT(gActors); i++, actor++) {
        if (actor->obj.status != OBJ_ACTIVE) continue;
        if (actor->obj.id != OBJ_ACTOR_ALLRANGE) continue;
        if (!Minimap_FloatValid(&actor->obj.pos.x)) continue;
        if (!Minimap_FloatValid(&actor->obj.pos.z)) continue;
        if (!Minimap_FloatValid(&actor->obj.rot.y)) continue;
        if (fabsf(actor->obj.pos.x) > halfSize * 1.1f) continue;
        if (fabsf(actor->obj.pos.z) > halfSize * 1.1f) continue;

        sx = Minimap_ToScreenX(actor->obj.pos.x, halfSize);
        sy = Minimap_ToScreenZ(actor->obj.pos.z, halfSize);

        switch (actor->aiType) {
            case AI360_FALCO:  Minimap_DrawTriangle(sx, sy, actor->obj.rot.y,  60, 120, 255); break;
            case AI360_SLIPPY: Minimap_DrawTriangle(sx, sy, actor->obj.rot.y,  60, 220,  60); break;
            case AI360_PEPPY:  Minimap_DrawTriangle(sx, sy, actor->obj.rot.y, 220,  60,  60); break;
            case AI360_KATT:   Minimap_DrawTriangle(sx, sy, actor->obj.rot.y, 255, 100, 200); break;
            case AI360_BILL:   Minimap_DrawTriangle(sx, sy, actor->obj.rot.y,   0,  80,   0); break;
            case AI360_WOLF:
            case AI360_LEON:
            case AI360_PIGMA:
            case AI360_ANDREW: Minimap_DrawTriangle(sx, sy, actor->obj.rot.y, 255, 120,   0); break;
            case AI360_ENEMY:
                /* animFrame == 1 marks a Cornerian Fighter ally in the generic
                 * enemy pool (same check Actor_Despawn uses). Skip those. */
                if (actor->animFrame != 1) {
                    Minimap_DrawTriangle(sx, sy, actor->obj.rot.y, 255, 120, 0);
                }
                break;
            default: break;
        }
    }

    /* Bosses: separate array from gActors, same Object layout */
    for (i = 0, boss = &gBosses[0]; i < ARRAY_COUNT(gBosses); i++, boss++) {
        if (boss->obj.status != OBJ_ACTIVE) continue;
        if (!Minimap_FloatValid(&boss->obj.pos.x)) continue;
        if (!Minimap_FloatValid(&boss->obj.pos.z)) continue;
        if (!Minimap_FloatValid(&boss->obj.rot.y)) continue;
        if (fabsf(boss->obj.pos.x) > halfSize * 1.1f) continue;
        if (fabsf(boss->obj.pos.z) > halfSize * 1.1f) continue;

        sx = Minimap_ToScreenX(boss->obj.pos.x, halfSize);
        sy = Minimap_ToScreenZ(boss->obj.pos.z, halfSize);

        if (boss->obj.id == OBJ_BOSS_SZ_GREAT_FOX) {
            /* Great Fox is an ally being defended - cyan triangle.
             * Boss rot.y=0 faces +Z (opposite actor convention), add 180. */
            Minimap_DrawTriangle(sx, sy, boss->obj.rot.y + 180.0f, 0, 200, 255);
        } else {
            /* Enemy boss - red triangle */
            Minimap_DrawTriangle(sx, sy, boss->obj.rot.y + 180.0f, 255, 60, 60);
        }
    }

    /* Player - safe: PLAY_PAUSE and free cam (PLAY_UPDATE from menu) both require PLAY_INIT */
    {
        f32 ppx = gPlayer[0].pos.x;
        f32 ppz = gPlayer[0].trueZpos;
        if (Minimap_FloatValid(&ppx) && Minimap_FloatValid(&ppz)) {
            sx = Minimap_ToScreenX(ppx, halfSize);
            sy = Minimap_ToScreenZ(ppz, halfSize);
            Minimap_DrawTriangle(sx, sy, gPlayer[0].yRot_114 + gPlayer[0].rot.y,
                                 255, 220, 0);
        }
    }

    /* Free cam position: cyan triangle pointing in camera yaw direction */
    if (Practice_FreeCam_IsActive()) {
        f32 cx, cz, cyaw;
        Practice_FreeCam_GetMapPos(&cx, &cz, &cyaw);
        if (Minimap_FloatValid(&cx) && Minimap_FloatValid(&cz)) {
            sx = Minimap_ToScreenX(cx, halfSize);
            sy = Minimap_ToScreenZ(cz, halfSize);
            Minimap_DrawTriangle(sx, sy, -cyaw, 0, 220, 255);
        }
    }
}

#endif
