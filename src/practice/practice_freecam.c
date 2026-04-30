#include "practice.h"

#ifdef PRACTICE_ROM

#define FREECAM_SPEED     200.0f
#define FREECAM_BOOST     5.0f
#define FREECAM_LOOK_SPEED 0.03f
#define FREECAM_PITCH_MAX  85.0f

static bool sActive = false;
static bool sOverlayVisible = true;
static Vec3f sEye;
static f32 sYaw;   /* degrees, 0 = looking toward -Z */
static f32 sPitch; /* degrees, positive = looking up */

bool Practice_FreeCam_IsActive(void) {
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

void Practice_FreeCam_Exit(void) {
    sActive = false;
}

void Practice_FreeCam_GetMapPos(f32* x, f32* z, f32* yaw_deg) {
    *x = sEye.x;
    *z = sEye.z;
    *yaw_deg = sYaw;
}

void Practice_FreeCam_GetView(Vec3f* eye, Vec3f* at) {
    f32 cosPitch = COS_DEG(sPitch);
    eye->x = sEye.x;
    eye->y = sEye.y;
    eye->z = sEye.z;
    at->x = sEye.x + SIN_DEG(sYaw) * cosPitch * 1000.0f;
    at->y = sEye.y + SIN_DEG(sPitch) * 1000.0f;
    at->z = sEye.z - COS_DEG(sYaw) * cosPitch * 1000.0f;
}

void Practice_FreeCam_Update(void) {
    OSContPad* press = &gControllerPress[gMainController];
    OSContPad* hold = &gControllerHold[gMainController];
    f32 speed;
    f32 cosPitch, fwdX, fwdY, fwdZ, strafeX, strafeZ;

    if (press->button & B_BUTTON) {
        Practice_FreeCam_Exit();
        return;
    }

    if (press->button & Z_TRIG) {
        gPracticeConfig.showHitboxes ^= true;
    }

    if (press->button & A_BUTTON) {
        sOverlayVisible ^= true;
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
}

void Practice_FreeCam_Draw(void) {
    bool boosting = (gControllerHold[gMainController].button & R_TRIG) != 0;
    bool hitboxOn = gPracticeConfig.showHitboxes;

    if (!sOverlayVisible) {
        Practice_DrawBox(210, 80, 96, 14, 0, 0, 0, 160);
        Practice_DrawText(214, 83, "A: SHOW HUD");
        return;
    }

    Practice_DrawBox(210, 80, 108, 94, 0, 0, 0, 180);
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
    Practice_DrawText(214, 146, "A: HIDE HUD");
    Practice_DrawText(214, 156, "B: EXIT");
}

#endif
