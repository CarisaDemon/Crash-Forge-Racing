#ifndef CRASH_FORGE_NATIVE_HUD_H
#define CRASH_FORGE_NATIVE_HUD_H

struct Driver;

enum NativeHUDDriftMeterMode
{
	NATIVE_HUD_DRIFT_OFF = 0,
	NATIVE_HUD_DRIFT_CTR_CENTERED = 1,
	NATIVE_HUD_DRIFT_CNK_CENTERED = 2,
	NATIVE_HUD_DRIFT_CNK_3D = 3,
};

enum NativeHUDCNKHeight
{
    NATIVE_HUD_CNK_HEIGHT_HIGH = 0,
    NATIVE_HUD_CNK_HEIGHT_LOW = 1,
};

/* For old ctr_hud.cfg without cnk_meter_height, preserve CNK 2D's
 * original vertical centre and CNK 3D's current lowered preview. */
int NativeHUD_GetCNKMeterHeight(void);
void NativeHUD_SetCNKMeterHeight(int height);

#define NATIVE_HUD_RETAIL_SPEEDOMETER_OFFSET_Y (-4)

struct NativeHUDPlayerData
{
	int reserves;
	int slideRoom;
	int slideMaxRoom;
	int slideReady;
	int isDrifting;
	int fireSpeedCap;
	int turboOutsideTimer;
};

int NativeHUD_GetSaffiEnabled(void);
void NativeHUD_SetSaffiEnabled(int enabled);

int NativeHUD_GetReservesBarEnabled(void);
void NativeHUD_SetReservesBarEnabled(int enabled);

int NativeHUD_GetDriftMeterMode(void);
void NativeHUD_SetDriftMeterMode(int mode);

void NativeHUD_HandleGameplayToggle(void);

void NativeHUD_GetPlayerData(const struct Driver *driver, struct NativeHUDPlayerData *out);
void NativeHUD_RenderPlayerExtras(struct Driver *driver);

#endif
