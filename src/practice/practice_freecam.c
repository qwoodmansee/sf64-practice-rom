#include "practice.h"

#ifdef PRACTICE_ROM

#define FREECAM_SPEED     200.0f
#define FREECAM_BOOST     5.0f
#define FREECAM_LOOK_SPEED 0.03f
#define FREECAM_PITCH_MAX  85.0f
#define FREECAM_VIEW_DISTANCE   1000.0f
#define FREECAM_MARKER_DISTANCE 400.0f
#define FREECAM_MARKER_SIZE     30.0f
#define FREECAM_BOMB_RADIUS     1800.0f
#define FREECAM_EXPLOSION_RADIUS 450.0f
#define FREECAM_RING_UNIT       1000.0f
#define FREECAM_RING_STEPS      16

static bool sActive = false;
static bool sOverlayVisible = true;
static PracticeFreeCamObject sObject = PFREECAM_OBJECT_NONE;
static bool sMarkerPersisted = false;
static PracticeFreeCamObject sPersistedObject = PFREECAM_OBJECT_NONE;
static Vec3f sEye;
static Vec3f sPersistedMarker;
static f32 sYaw;   /* degrees, 0 = looking toward -Z */
static f32 sPitch; /* degrees, positive = looking up */

static Vtx sMarkerCubeVtx[8] = {
    VTX(-1, -1, -1, 0, 0, 255, 255, 255, 255),
    VTX( 1, -1, -1, 0, 0, 255, 255, 255, 255),
    VTX( 1,  1, -1, 0, 0, 255, 255, 255, 255),
    VTX(-1,  1, -1, 0, 0, 255, 255, 255, 255),
    VTX(-1, -1,  1, 0, 0, 255, 255, 255, 255),
    VTX( 1, -1,  1, 0, 0, 255, 255, 255, 255),
    VTX( 1,  1,  1, 0, 0, 255, 255, 255, 255),
    VTX(-1,  1,  1, 0, 0, 255, 255, 255, 255),
};

static Vtx sUnitRingVtx[FREECAM_RING_STEPS] = {
    VTX( 1000,     0, 0, 0, 0, 255, 255, 255, 255),
    VTX(  924,   383, 0, 0, 0, 255, 255, 255, 255),
    VTX(  707,   707, 0, 0, 0, 255, 255, 255, 255),
    VTX(  383,   924, 0, 0, 0, 255, 255, 255, 255),
    VTX(    0,  1000, 0, 0, 0, 255, 255, 255, 255),
    VTX( -383,   924, 0, 0, 0, 255, 255, 255, 255),
    VTX( -707,   707, 0, 0, 0, 255, 255, 255, 255),
    VTX( -924,   383, 0, 0, 0, 255, 255, 255, 255),
    VTX(-1000,     0, 0, 0, 0, 255, 255, 255, 255),
    VTX( -924,  -383, 0, 0, 0, 255, 255, 255, 255),
    VTX( -707,  -707, 0, 0, 0, 255, 255, 255, 255),
    VTX( -383,  -924, 0, 0, 0, 255, 255, 255, 255),
    VTX(    0, -1000, 0, 0, 0, 255, 255, 255, 255),
    VTX(  383,  -924, 0, 0, 0, 255, 255, 255, 255),
    VTX(  707,  -707, 0, 0, 0, 255, 255, 255, 255),
    VTX(  924,  -383, 0, 0, 0, 255, 255, 255, 255),
};

static Vtx sUnitDiscVtx[FREECAM_RING_STEPS + 1] = {
    VTX(    0,     0, 0, 0, 0, 255, 255, 255, 255),
    VTX( 1000,     0, 0, 0, 0, 255, 255, 255, 255),
    VTX(  924,   383, 0, 0, 0, 255, 255, 255, 255),
    VTX(  707,   707, 0, 0, 0, 255, 255, 255, 255),
    VTX(  383,   924, 0, 0, 0, 255, 255, 255, 255),
    VTX(    0,  1000, 0, 0, 0, 255, 255, 255, 255),
    VTX( -383,   924, 0, 0, 0, 255, 255, 255, 255),
    VTX( -707,   707, 0, 0, 0, 255, 255, 255, 255),
    VTX( -924,   383, 0, 0, 0, 255, 255, 255, 255),
    VTX(-1000,     0, 0, 0, 0, 255, 255, 255, 255),
    VTX( -924,  -383, 0, 0, 0, 255, 255, 255, 255),
    VTX( -707,  -707, 0, 0, 0, 255, 255, 255, 255),
    VTX( -383,  -924, 0, 0, 0, 255, 255, 255, 255),
    VTX(    0, -1000, 0, 0, 0, 255, 255, 255, 255),
    VTX(  383,  -924, 0, 0, 0, 255, 255, 255, 255),
    VTX(  707,  -707, 0, 0, 0, 255, 255, 255, 255),
    VTX(  924,  -383, 0, 0, 0, 255, 255, 255, 255),
};

static const char* Practice_FreeCam_ObjectName(PracticeFreeCamObject object) {
    switch (object) {
        case PFREECAM_OBJECT_BOMB:
            return "BOMB";
        case PFREECAM_OBJECT_EXPLOSION:
            return "EXPLOSION";
        case PFREECAM_OBJECT_NONE:
        default:
            return "NONE";
    }
}

static void Practice_FreeCam_GetMarkerPos(Vec3f* pos) {
    f32 cosPitch = COS_DEG(sPitch);

    pos->x = sEye.x + SIN_DEG(sYaw) * cosPitch * FREECAM_MARKER_DISTANCE;
    pos->y = sEye.y + SIN_DEG(sPitch) * FREECAM_MARKER_DISTANCE;
    pos->z = sEye.z - COS_DEG(sYaw) * cosPitch * FREECAM_MARKER_DISTANCE;
}

static void Practice_FreeCam_ShowObjectStatus(void) {
    char msg[32];

    sprintf(msg, "FREECAM: %s", Practice_FreeCam_ObjectName(sObject));
    Practice_Hud_ShowStatus(msg, 0, 200, 255);
}

static void Practice_FreeCam_CycleObject(s32 dir) {
    s32 object = (s32) sObject + dir;

    if (object < PFREECAM_OBJECT_NONE) {
        object = PFREECAM_OBJECT_MAX - 1;
    } else if (object >= PFREECAM_OBJECT_MAX) {
        object = PFREECAM_OBJECT_NONE;
    }

    sObject = (PracticeFreeCamObject) object;
    Practice_FreeCam_ShowObjectStatus();
}

static f32 Practice_FreeCam_ObjectRadius(PracticeFreeCamObject object) {
    switch (object) {
        case PFREECAM_OBJECT_BOMB:
            return FREECAM_BOMB_RADIUS;
        case PFREECAM_OBJECT_EXPLOSION:
            return FREECAM_EXPLOSION_RADIUS;
        case PFREECAM_OBJECT_NONE:
        default:
            return 0.0f;
    }
}

static void Practice_FreeCam_ObjectColor(PracticeFreeCamObject object, u8* r, u8* g, u8* b) {
    switch (object) {
        case PFREECAM_OBJECT_EXPLOSION:
            *r = 40;
            *g = 255;
            *b = 80;
            break;
        case PFREECAM_OBJECT_BOMB:
        default:
            *r = 255;
            *g = 30;
            *b = 30;
            break;
    }
}

static void Practice_FreeCam_MarkerSetupRCP(void) {
    gDPPipeSync(gMasterDisp++);
    gDPSetCycleType(gMasterDisp++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMasterDisp++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCombineMode(gMasterDisp++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gSPSetGeometryMode(gMasterDisp++, G_ZBUFFER);
    gSPClearGeometryMode(gMasterDisp++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK | G_SHADING_SMOOTH);
}

static void Practice_FreeCam_RangeSetupRCP(void) {
    gDPPipeSync(gMasterDisp++);
    gDPSetCycleType(gMasterDisp++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMasterDisp++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gMasterDisp++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gSPClearGeometryMode(gMasterDisp++, G_ZBUFFER | G_LIGHTING | G_CULL_FRONT | G_CULL_BACK | G_SHADING_SMOOTH);
}

static void Practice_FreeCam_DrawMarker(Vec3f* pos, f32 size, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gMasterDisp++);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, a);

    Matrix_Push(&gGfxMatrix);
    Matrix_Translate(gGfxMatrix, pos->x, pos->y, pos->z, MTXF_APPLY);
    Matrix_Scale(gGfxMatrix, size, size, size, MTXF_APPLY);
    Matrix_SetGfxMtx(&gMasterDisp);

    gSPVertex(gMasterDisp++, sMarkerCubeVtx, 8, 0);
    gSP2Triangles(gMasterDisp++, 0,2,1,0, 0,3,2,0);
    gSP2Triangles(gMasterDisp++, 4,5,6,0, 4,6,7,0);
    gSP2Triangles(gMasterDisp++, 0,1,5,0, 0,5,4,0);
    gSP2Triangles(gMasterDisp++, 3,6,2,0, 3,7,6,0);
    gSP2Triangles(gMasterDisp++, 0,4,7,0, 0,7,3,0);
    gSP2Triangles(gMasterDisp++, 1,2,6,0, 1,6,5,0);

    Matrix_Pop(&gGfxMatrix);
}

static void Practice_FreeCam_DrawRing(Vec3f* pos, f32 radius, s32 axis) {
    s32 i;

    Matrix_Push(&gGfxMatrix);
    Matrix_Translate(gGfxMatrix, pos->x, pos->y, pos->z, MTXF_APPLY);

    if (axis == 1) {
        Matrix_RotateX(gGfxMatrix, M_PI / 2.0f, MTXF_APPLY);
    } else if (axis == 2) {
        Matrix_RotateY(gGfxMatrix, M_PI / 2.0f, MTXF_APPLY);
    }

    Matrix_Scale(gGfxMatrix, radius / FREECAM_RING_UNIT, radius / FREECAM_RING_UNIT, radius / FREECAM_RING_UNIT, MTXF_APPLY);
    Matrix_SetGfxMtx(&gMasterDisp);

    gSPVertex(gMasterDisp++, sUnitRingVtx, FREECAM_RING_STEPS, 0);
    for (i = 0; i < FREECAM_RING_STEPS; i++) {
        gSPLine3D(gMasterDisp++, i, (i + 1) % FREECAM_RING_STEPS, 0);
    }

    Matrix_Pop(&gGfxMatrix);
}

static void Practice_FreeCam_DrawDisc(Vec3f* pos, f32 radius, s32 axis) {
    s32 i;

    Matrix_Push(&gGfxMatrix);
    Matrix_Translate(gGfxMatrix, pos->x, pos->y, pos->z, MTXF_APPLY);

    if (axis == 1) {
        Matrix_RotateX(gGfxMatrix, M_PI / 2.0f, MTXF_APPLY);
    } else if (axis == 2) {
        Matrix_RotateY(gGfxMatrix, M_PI / 2.0f, MTXF_APPLY);
    }

    Matrix_Scale(gGfxMatrix, radius / FREECAM_RING_UNIT, radius / FREECAM_RING_UNIT, radius / FREECAM_RING_UNIT, MTXF_APPLY);
    Matrix_SetGfxMtx(&gMasterDisp);

    gSPVertex(gMasterDisp++, sUnitDiscVtx, FREECAM_RING_STEPS + 1, 0);
    for (i = 1; i <= FREECAM_RING_STEPS; i += 2) {
        gSP2Triangles(gMasterDisp++, 0, i, (i % FREECAM_RING_STEPS) + 1, 0,
                                      0, (i % FREECAM_RING_STEPS) + 1,
                                      ((i + 1) % FREECAM_RING_STEPS) + 1, 0);
    }

    Matrix_Pop(&gGfxMatrix);
}

static void Practice_FreeCam_DrawRadius(Vec3f* pos, f32 radius, u8 r, u8 g, u8 b) {
    if (radius <= 0.0f) {
        return;
    }

    Practice_FreeCam_RangeSetupRCP();
    gDPPipeSync(gMasterDisp++);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, 48);

    Practice_FreeCam_DrawDisc(pos, radius, 0);
    Practice_FreeCam_DrawDisc(pos, radius, 1);
    Practice_FreeCam_DrawDisc(pos, radius, 2);

    gDPPipeSync(gMasterDisp++);
    gDPSetPrimColor(gMasterDisp++, 0, 0, r, g, b, 220);
    Practice_FreeCam_DrawRing(pos, radius, 0);
    Practice_FreeCam_DrawRing(pos, radius, 1);
    Practice_FreeCam_DrawRing(pos, radius, 2);
}

bool Practice_FreeCam_IsActive_PakImpl(void) {
    return sActive;
}

void Practice_FreeCam_Enter(void) {
    f32 dx, dz;

    sEye.x = gPlayCamEye.x;
    sEye.y = gPlayCamEye.y;
    sEye.z = gPlayCamEye.z;

    dx = gPlayCamAt.x - gPlayCamEye.x;
    dz = gPlayCamAt.z - gPlayCamEye.z;

    sYaw = Math_RadToDeg(Math_Atan2F(dx, -dz));
    sPitch = 0.0f;
    sOverlayVisible = true;
    sActive = true;
}

bool Practice_FreeCam_OverlayVisible(void) {
    return sOverlayVisible;
}

PracticeFreeCamObject Practice_FreeCam_GetObject(void) {
    return sObject;
}

void Practice_FreeCam_Exit(void) {
    sActive = false;
}

void Practice_FreeCam_GetMapPos(f32* x, f32* z, f32* yaw_deg) {
    *x = sEye.x;
    *z = sEye.z;
    *yaw_deg = sYaw;
}

void Practice_FreeCam_GetView_PakImpl(Vec3f* eye, Vec3f* at) {
    f32 cosPitch = COS_DEG(sPitch);
    eye->x = sEye.x;
    eye->y = sEye.y;
    eye->z = sEye.z;
    at->x = sEye.x + SIN_DEG(sYaw) * cosPitch * FREECAM_VIEW_DISTANCE;
    at->y = sEye.y + SIN_DEG(sPitch) * FREECAM_VIEW_DISTANCE;
    at->z = sEye.z - COS_DEG(sYaw) * cosPitch * FREECAM_VIEW_DISTANCE;
}

void Practice_FreeCam_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    OSContPad* hold = &gControllerHold[gMainController];
    f32 speed;
    f32 cosPitch, fwdX, fwdY, fwdZ, strafeX, strafeZ;
    u8 markerR, markerG, markerB;

    if (press->button & B_BUTTON) {
        Practice_FreeCam_Exit();
        return;
    }

    if (press->button & Z_TRIG) {
        gPracticeConfig.showHitboxes ^= true;
    }

    if (press->button & L_JPAD) {
        Practice_FreeCam_CycleObject(-1);
    } else if (press->button & R_JPAD) {
        Practice_FreeCam_CycleObject(1);
    }

    sYaw += hold->stick_x * FREECAM_LOOK_SPEED;
    sPitch -= hold->stick_y * FREECAM_LOOK_SPEED;
    if (sPitch > FREECAM_PITCH_MAX) { sPitch = FREECAM_PITCH_MAX; }
    if (sPitch < -FREECAM_PITCH_MAX) { sPitch = -FREECAM_PITCH_MAX; }

    speed = (hold->button & R_TRIG) ? FREECAM_SPEED * FREECAM_BOOST : FREECAM_SPEED;

    cosPitch = COS_DEG(sPitch);
    fwdX = SIN_DEG(sYaw) * cosPitch;
    fwdY = SIN_DEG(sPitch);
    fwdZ = -COS_DEG(sYaw) * cosPitch;
    strafeX = COS_DEG(sYaw);
    strafeZ = SIN_DEG(sYaw);

    if (hold->button & U_CBUTTONS) {
        sEye.x += fwdX * speed;
        sEye.y += fwdY * speed;
        sEye.z += fwdZ * speed;
    }
    if (hold->button & D_CBUTTONS) {
        sEye.x -= fwdX * speed;
        sEye.y -= fwdY * speed;
        sEye.z -= fwdZ * speed;
    }
    if (hold->button & R_CBUTTONS) {
        sEye.x += strafeX * speed;
        sEye.z += strafeZ * speed;
    }
    if (hold->button & L_CBUTTONS) {
        sEye.x -= strafeX * speed;
        sEye.z -= strafeZ * speed;
    }

    if ((press->button & A_BUTTON) && (sObject == PFREECAM_OBJECT_NONE)) {
        sOverlayVisible ^= true;
    } else if (press->button & A_BUTTON) {
        Practice_FreeCam_GetMarkerPos(&sPersistedMarker);
        sPersistedObject = sObject;
        sMarkerPersisted = true;
        Practice_FreeCam_ObjectColor(sPersistedObject, &markerR, &markerG, &markerB);
        Practice_Hud_ShowStatus("FREECAM MARKER SET", markerR, markerG, markerB);
    }
}

void Practice_FreeCam_DrawMarkers_PakImpl(void) {
    Vec3f marker;
    u8 markerR, markerG, markerB;

    if (!sActive && !sMarkerPersisted) {
        return;
    }

    if (sMarkerPersisted) {
        Practice_FreeCam_ObjectColor(sPersistedObject, &markerR, &markerG, &markerB);
        Practice_FreeCam_DrawRadius(&sPersistedMarker, Practice_FreeCam_ObjectRadius(sPersistedObject), markerR,
                                    markerG, markerB);
        Practice_FreeCam_MarkerSetupRCP();
        Practice_FreeCam_DrawMarker(&sPersistedMarker, FREECAM_MARKER_SIZE, markerR, markerG, markerB, 255);
    }

    if (sActive && (sObject != PFREECAM_OBJECT_NONE)) {
        Practice_FreeCam_GetMarkerPos(&marker);
        Practice_FreeCam_ObjectColor(sObject, &markerR, &markerG, &markerB);
        Practice_FreeCam_MarkerSetupRCP();
        Practice_FreeCam_DrawMarker(&marker, FREECAM_MARKER_SIZE, markerR, markerG, markerB, 160);
    }
}

void Practice_FreeCam_Draw(void) {
    bool boosting = (gControllerHold[gMainController].button & R_TRIG) != 0;
    bool hitboxOn = gPracticeConfig.showHitboxes;
    char objectText[32];

    if (!sOverlayVisible) {
        Practice_DrawBox(210, 80, 96, 14, 0, 0, 0, 160);
        Practice_DrawText(214, 83, "A: SHOW HUD");
        return;
    }

    Practice_DrawBox(210, 80, 118, 104, 0, 0, 0, 180);
    Practice_DrawTextOutline(214, 84,  "FREE CAM",    0, 200, 255);
    Practice_DrawText(       214, 96,  "STICK: LOOK");
    Practice_DrawText(       214, 106, "C-UD: FLY");
    Practice_DrawText(       214, 116, "C-LR: STRAFE");
    if (boosting) {
        Practice_DrawTextColor(214, 126, "R: BOOST", 255, 200, 0);
    } else {
        Practice_DrawText(     214, 126, "R: BOOST");
    }
    if (hitboxOn) {
        Practice_DrawTextColor(214, 136, "Z: HITBOX ON",  0, 255, 100);
    } else {
        Practice_DrawText(     214, 136, "Z: HITBOX OFF");
    }
    sprintf(objectText, "D-L/R: %s", Practice_FreeCam_ObjectName(sObject));
    Practice_DrawTextColor(214, 146, objectText, 0, 200, 255);
    if (sObject == PFREECAM_OBJECT_NONE) {
        Practice_DrawText(214, 156, "A: HIDE HUD");
    } else {
        Practice_DrawTextColor(214, 156, "A: SET MARKER", 255, 60, 60);
    }
    Practice_DrawText(214, 166, "B: EXIT");
}

#endif
