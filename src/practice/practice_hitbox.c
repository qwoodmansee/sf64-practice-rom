#include "practice.h"

#ifdef PRACTICE_ROM

#define HITBOX_DRAW_RANGE_SQ 25000000.0f

static Vtx sUnitCubeVtx[8] = {
    { { { -1, -1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1, -1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1,  1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -1,  1, -1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -1, -1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1, -1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {  1,  1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -1,  1,  1 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
};

static void Hitbox_SetupRCP(void) {
    gDPPipeSync(gMasterDisp++);
    gDPSetCycleType(gMasterDisp++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMasterDisp++, G_RM_AA_ZB_XLU_LINE, G_RM_AA_ZB_XLU_LINE2);
    gDPSetCombineMode(gMasterDisp++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gSPSetGeometryMode(gMasterDisp++, G_ZBUFFER);
    gSPClearGeometryMode(gMasterDisp++, G_LIGHTING | G_CULL_BOTH | G_SHADING_SMOOTH);
}

static void Hitbox_DrawWireframeBox(f32 posX, f32 posY, f32 posZ,
                                     f32 rotX, f32 rotY, f32 rotZ,
                                     f32 hitRotX, f32 hitRotY, f32 hitRotZ,
                                     bool hasHitRot,
                                     f32 offZ, f32 sizeZ, f32 offY, f32 sizeY, f32 offX, f32 sizeX,
                                     u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gMasterDisp++);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, a);

    Matrix_Push(&gGfxMatrix);

    Matrix_Translate(gGfxMatrix, posX, posY, posZ + gPathProgress, MTXF_APPLY);
    Matrix_RotateY(gGfxMatrix, rotY * M_DTOR, MTXF_APPLY);
    Matrix_RotateX(gGfxMatrix, rotX * M_DTOR, MTXF_APPLY);
    Matrix_RotateZ(gGfxMatrix, rotZ * M_DTOR, MTXF_APPLY);

    if (hasHitRot) {
        Matrix_RotateY(gGfxMatrix, hitRotY * M_DTOR, MTXF_APPLY);
        Matrix_RotateX(gGfxMatrix, hitRotX * M_DTOR, MTXF_APPLY);
        Matrix_RotateZ(gGfxMatrix, hitRotZ * M_DTOR, MTXF_APPLY);
    }

    Matrix_Translate(gGfxMatrix, offX, offY, offZ, MTXF_APPLY);
    Matrix_Scale(gGfxMatrix, sizeX, sizeY, sizeZ, MTXF_APPLY);

    Matrix_SetGfxMtx(&gMasterDisp);

    gSPVertex(gMasterDisp++, sUnitCubeVtx, 8, 0);

    gSPLine3D(gMasterDisp++, 0, 1, 0);
    gSPLine3D(gMasterDisp++, 1, 2, 0);
    gSPLine3D(gMasterDisp++, 2, 3, 0);
    gSPLine3D(gMasterDisp++, 3, 0, 0);

    gSPLine3D(gMasterDisp++, 4, 5, 0);
    gSPLine3D(gMasterDisp++, 5, 6, 0);
    gSPLine3D(gMasterDisp++, 6, 7, 0);
    gSPLine3D(gMasterDisp++, 7, 4, 0);

    gSPLine3D(gMasterDisp++, 0, 4, 0);
    gSPLine3D(gMasterDisp++, 1, 5, 0);
    gSPLine3D(gMasterDisp++, 2, 6, 0);
    gSPLine3D(gMasterDisp++, 3, 7, 0);

    Matrix_Pop(&gGfxMatrix);
}

static bool Hitbox_CheckPlayerCollision(f32 objX, f32 objY, f32 objZ, Hitbox* hitbox) {
    Player* player = &gPlayer[0];
    Vec3f* pts[2];
    s32 i;

    pts[0] = &player->hit3;
    pts[1] = &player->hit4;

    for (i = 0; i < 2; i++) {
        if ((fabsf(hitbox->z.offset + objZ - pts[i]->z) < (hitbox->z.size + 20.0f)) &&
            (fabsf(hitbox->x.offset + objX - pts[i]->x) < (hitbox->x.size + 20.0f)) &&
            (fabsf(hitbox->y.offset + objY - pts[i]->y) < (hitbox->y.size + 10.0f))) {
            return true;
        }
    }
    return false;
}

static void Hitbox_DrawObjectHitboxes(Object* obj, f32* hitboxData, u8 r, u8 g, u8 b, u8 a) {
    s32 count;
    s32 i;
    f32 hitRotX, hitRotY, hitRotZ;
    bool hasHitRot;
    Hitbox* hitbox;
    u8 drawR, drawG, drawB, drawA;

    if (hitboxData == NULL) {
        return;
    }

    count = (s32) *hitboxData;
    if (count == 0) {
        return;
    }

    hitboxData++;

    for (i = 0; i < count; i++, hitboxData += 6) {
        hasHitRot = false;
        hitRotX = hitRotY = hitRotZ = 0.0f;

        if (*hitboxData == HITBOX_ROTATED) {
            hitRotX = hitboxData[1];
            hitRotY = hitboxData[2];
            hitRotZ = hitboxData[3];
            hitboxData += 4;
            hasHitRot = true;
        } else if (*hitboxData >= HITBOX_SHADOW) {
            hitboxData++;
            continue;
        }

        hitbox = (Hitbox*) hitboxData;

        drawR = r;
        drawG = g;
        drawB = b;
        drawA = a;

        if (gPracticeConfig.showHitboxFlash) {
            if (Hitbox_CheckPlayerCollision(obj->pos.x, obj->pos.y, obj->pos.z, hitbox)) {
                drawR = 255;
                drawG = 255;
                drawB = 0;
                drawA = 220;
            }
        }

        Hitbox_DrawWireframeBox(
            obj->pos.x, obj->pos.y, obj->pos.z,
            obj->rot.x, obj->rot.y, obj->rot.z,
            hitRotX, hitRotY, hitRotZ, hasHitRot,
            hitbox->z.offset, hitbox->z.size,
            hitbox->y.offset, hitbox->y.size,
            hitbox->x.offset, hitbox->x.size,
            drawR, drawG, drawB, drawA
        );
    }
}

void Practice_Hitbox_Draw(void) {
    s32 i;
    f32 dx, dy, dz, distSq;
    Player* player;

    if (!gPracticeConfig.showHitboxes) {
        return;
    }
    if ((gGameState != GSTATE_PLAY) || (gPlayState != PLAY_UPDATE)) {
        return;
    }

    player = &gPlayer[0];

    Hitbox_SetupRCP();

    if (gPracticeConfig.showHitboxActors) {
        for (i = 0; i < 60; i++) {
            if (gActors[i].obj.status == OBJ_FREE) {
                continue;
            }
            dx = gActors[i].obj.pos.x - player->pos.x;
            dy = gActors[i].obj.pos.y - player->pos.y;
            dz = gActors[i].obj.pos.z - player->trueZpos;
            distSq = (dx * dx) + (dy * dy) + (dz * dz);
            if (distSq > HITBOX_DRAW_RANGE_SQ) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gActors[i].obj, gActors[i].info.hitbox,
                                      255, 50, 50, 180);
        }

        for (i = 0; i < 4; i++) {
            if (gBosses[i].obj.status == OBJ_FREE) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gBosses[i].obj, gBosses[i].info.hitbox,
                                      255, 50, 50, 180);
        }
    }

    if (gPracticeConfig.showHitboxScenery) {
        for (i = 0; i < 50; i++) {
            if (gScenery[i].obj.status == OBJ_FREE) {
                continue;
            }
            dx = gScenery[i].obj.pos.x - player->pos.x;
            dy = gScenery[i].obj.pos.y - player->pos.y;
            dz = gScenery[i].obj.pos.z - player->trueZpos;
            distSq = (dx * dx) + (dy * dy) + (dz * dz);
            if (distSq > HITBOX_DRAW_RANGE_SQ) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gScenery[i].obj, gScenery[i].info.hitbox,
                                      50, 100, 255, 180);
        }
    }

    if (gPracticeConfig.showHitboxItems) {
        for (i = 0; i < 20; i++) {
            if (gItems[i].obj.status == OBJ_FREE) {
                continue;
            }
            dx = gItems[i].obj.pos.x - player->pos.x;
            dy = gItems[i].obj.pos.y - player->pos.y;
            dz = gItems[i].obj.pos.z - player->trueZpos;
            distSq = (dx * dx) + (dy * dy) + (dz * dz);
            if (distSq > HITBOX_DRAW_RANGE_SQ) {
                continue;
            }
            Hitbox_DrawObjectHitboxes(&gItems[i].obj, gItems[i].info.hitbox,
                                      50, 255, 50, 180);
        }
    }

    if (gPracticeConfig.showHitboxPlayer) {
        Hitbox_DrawWireframeBox(
            player->hit1.x, player->hit1.y, player->hit1.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
        Hitbox_DrawWireframeBox(
            player->hit2.x, player->hit2.y, player->hit2.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
        Hitbox_DrawWireframeBox(
            player->hit3.x, player->hit3.y, player->hit3.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
        Hitbox_DrawWireframeBox(
            player->hit4.x, player->hit4.y, player->hit4.z,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false,
            0.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f,
            255, 255, 255, 200);
    }
}

#endif
