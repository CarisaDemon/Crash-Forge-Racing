#include <common.h>
#if defined(CTR_NATIVE)
#include <RacerLoadout.h>
#include <VehiclePartRegistry.h>
#include <math.h>
#include <stdlib.h>
#endif

enum
{
	TURBO_FIRE_SIZE_MIN = 4,
	TURBO_FIRE_SIZE_MAX = 8,
	TURBO_FIRE_MATRIX_SCALE_SHIFT = 3,
	TURBO_FIRE_LEFT_X_NUMERATOR = 9,
	TURBO_FIRE_LEFT_X_SHIFT = 0xb,
	TURBO_FIRE_RIGHT_X_NUMERATOR = -0x12,
	TURBO_FIRE_RIGHT_X_SHIFT = 0xc,
	TURBO_FIRE_Y_NUMERATOR = 3,
	TURBO_FIRE_Y_SHIFT = 8,
	TURBO_FIRE_Z_NUMERATOR = -0x34,
	TURBO_FIRE_Z_SHIFT = 0xc,
	TURBO_COOLDOWN_SIGN_SCALE = 0x10000,
	TURBO_ALPHA_RUMBLE_THRESHOLD = 2500,
	TURBO_RUMBLE_FRAMES = 4,
	TURBO_RUMBLE_FORCE = 4,
	TURBO_SECONDARY_MODEL_FRAME_OFFSET = 3,
	TURBO_ANIM_FRAME_COUNT = 8,
	TURBO_ANIM_FRAME_MASK = 7,
	TURBO_AUDIO_SLOT = 3,
	TURBO_AUDIO_VOLUME_BASE = 0x100,
	TURBO_AUDIO_ALPHA_SHIFT = 4,
	TURBO_AUDIO_VOLUME_MAX = 0x82,
	TURBO_AUDIO_DISTORT_STEP = 0x10,
	TURBO_AUDIO_DISTORT_MAX = 0x80,
	TURBO_AUDIO_DISTORT_INCREMENT_LIMIT = 0xc0,
	TURBO_AUDIO_SFX_ID = 0xe,
	TURBO_RESERVES_DISAPPEAR_THRESHOLD = 0x10,
	TURBO_ALPHA_FULL_MINUS_ONE = 0xfff,
	TURBO_FADE_FAST_STEP = 0x100,
	TURBO_FADE_SLOW_STEP = 0x40,
	TURBO_STOP_SFX_ID = -1,
};

CTR_STATIC_ASSERT(TURBO_FIRE_SIZE_MIN == 4);
CTR_STATIC_ASSERT(TURBO_FIRE_SIZE_MAX == 8);
CTR_STATIC_ASSERT(TURBO_FIRE_MATRIX_SCALE_SHIFT == 3);
CTR_STATIC_ASSERT(TURBO_FIRE_LEFT_X_NUMERATOR == 9);
CTR_STATIC_ASSERT(TURBO_FIRE_LEFT_X_SHIFT == 0xb);
CTR_STATIC_ASSERT(TURBO_FIRE_RIGHT_X_NUMERATOR == -0x12);
CTR_STATIC_ASSERT(TURBO_FIRE_RIGHT_X_SHIFT == 0xc);
CTR_STATIC_ASSERT(TURBO_FIRE_Y_NUMERATOR == 3);
CTR_STATIC_ASSERT(TURBO_FIRE_Y_SHIFT == 8);
CTR_STATIC_ASSERT(TURBO_FIRE_Z_NUMERATOR == -0x34);
CTR_STATIC_ASSERT(TURBO_FIRE_Z_SHIFT == 0xc);
CTR_STATIC_ASSERT(TURBO_COOLDOWN_SIGN_SCALE == 0x10000);
CTR_STATIC_ASSERT(TURBO_ALPHA_RUMBLE_THRESHOLD == 2500);
CTR_STATIC_ASSERT(TURBO_RUMBLE_FRAMES == 4);
CTR_STATIC_ASSERT(TURBO_RUMBLE_FORCE == 4);
CTR_STATIC_ASSERT(TURBO_SECONDARY_MODEL_FRAME_OFFSET == 3);
CTR_STATIC_ASSERT(TURBO_ANIM_FRAME_COUNT == 8);
CTR_STATIC_ASSERT(TURBO_ANIM_FRAME_MASK == 7);
CTR_STATIC_ASSERT(TURBO_AUDIO_SLOT == 3);
CTR_STATIC_ASSERT(TURBO_AUDIO_VOLUME_BASE == 0x100);
CTR_STATIC_ASSERT(TURBO_AUDIO_ALPHA_SHIFT == 4);
CTR_STATIC_ASSERT(TURBO_AUDIO_VOLUME_MAX == 0x82);
CTR_STATIC_ASSERT(TURBO_AUDIO_DISTORT_STEP == 0x10);
CTR_STATIC_ASSERT(TURBO_AUDIO_DISTORT_MAX == 0x80);
CTR_STATIC_ASSERT(TURBO_AUDIO_DISTORT_INCREMENT_LIMIT == 0xc0);
CTR_STATIC_ASSERT(TURBO_AUDIO_SFX_ID == 0xe);
CTR_STATIC_ASSERT(TURBO_RESERVES_DISAPPEAR_THRESHOLD == 0x10);
CTR_STATIC_ASSERT(TURBO_ALPHA_FULL_MINUS_ONE == 0xfff);
CTR_STATIC_ASSERT(TURBO_FADE_FAST_STEP == 0x100);
CTR_STATIC_ASSERT(TURBO_FADE_SLOW_STEP == 0x40);
CTR_STATIC_ASSERT(TURBO_STOP_SFX_ID == -1);

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80069284-0x80069370.
void VehTurbo_ProcessBucket(struct Thread *turboThread)
{
	while (turboThread != NULL)
	{
		struct Instance *primaryInst = turboThread->inst;
		struct Turbo *turbo = (struct Turbo *)turboThread->object;
		struct Instance *secondaryInst = turbo->inst;
		struct Instance *driverInst = turbo->driver->instSelf;

		struct InstDrawPerPlayer *primary = INST_GETIDPP(primaryInst);
		struct InstDrawPerPlayer *secondary = INST_GETIDPP(secondaryInst);
		struct InstDrawPerPlayer *driver = INST_GETIDPP(driverInst);

		for (int i = 0; i < sdata->gGT->numPlyrCurrGame; i++)
		{
			if ((driver->instFlags & PUSHBUFFER_EXISTS) == 0)
			{
				u32 driverDrawFlag = driver->instFlags | ~DRAW_SUCCESSFUL;

				secondary->instFlags &= driverDrawFlag;
				primary->instFlags &= driverDrawFlag;

				secondary->otRangeNormal = driver->otRangeNormal;
				primary->otRangeNormal = driver->otRangeNormal;
				secondary->otRangeSecondary = driver->otRangeSecondary;
				primary->otRangeSecondary = driver->otRangeSecondary;

				secondary->depthOffset[0] = driver->depthOffset[0];
				primary->depthOffset[0] = driver->depthOffset[0];
				secondary->depthOffset[1] = driver->depthOffset[1];
				primary->depthOffset[1] = driver->depthOffset[1];
			}

			/* Native extra flame Instances receive exactly the same
			 * per-player culling/OT/depth propagation as the retail pair. */
#if defined(CTR_NATIVE)
			if (turbo->nativeExtraExhaust != NULL)
			{
			    int viewIndex = i;
			    for (int e = 0; e < turbo->nativeExtraExhaustCount; ++e)
			    {
			        struct Instance *extra = turbo->nativeExtraExhaust[e];
			        if (extra != NULL &&
						(driver->instFlags & PUSHBUFFER_EXISTS) == 0)
			        {
						struct InstDrawPerPlayer *idpp = INST_GETIDPP(extra) + viewIndex;
						u32 driverDrawFlag = driver->instFlags | ~DRAW_SUCCESSFUL;
						idpp->instFlags &= driverDrawFlag;
						idpp->otRangeNormal = driver->otRangeNormal;
						idpp->otRangeSecondary = driver->otRangeSecondary;
						idpp->depthOffset[0] = driver->depthOffset[0];
						idpp->depthOffset[1] = driver->depthOffset[1];
			        }
			    }
			}
#endif
			primary++;
			secondary++;
			driver++;
		}

		turboThread = turboThread->siblingThread;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80069370-0x800693c8.
void VehTurbo_ThDestroy(struct Thread *t)
{
	struct Turbo *turboObj = t->object;
	struct Driver *d = turboObj->driver;
	d->actionsFlagSet &= ~ACTION_TURBO_ITEM;

	INSTANCE_Death(t->inst);
#if defined(CTR_NATIVE)
	if (turboObj->nativeExtraExhaust != NULL)
	{
		for (int i = 0; i < turboObj->nativeExtraExhaustCount; ++i)
		{
			if (turboObj->nativeExtraExhaust[i] != NULL)
				INSTANCE_Death(turboObj->nativeExtraExhaust[i]);
		}
		free(turboObj->nativeExtraExhaust);
		turboObj->nativeExtraExhaust = NULL;
		turboObj->nativeExtraExhaustCount = 0;
	}
#endif
	INSTANCE_Death(turboObj->inst);
}

static void VehTurbo_TransformOffset(struct Instance *driverInst, s16 x, s16 y, s16 z, s32 *out)
{
	SVECTOR offset = {x, y, z, 0};

	// NOTE(aalhendi): Native expression of retail VXY0/VZ0 loads before gte_rt.
	gte_SetRotMatrix(&driverInst->matrix.m[0][0]);
	gte_SetTransMatrix(&driverInst->matrix.m[0][0]);
	CTR_GteLoadSV0(&offset);
	gte_rt();
	CTR_GteStoreIR(out);
}

#if defined(CTR_NATIVE)
static s16 VehTurbo_ClampS16(float value)
{
	if (value < -32768.0f)
		return -32768;
	if (value > 32767.0f)
		return 32767;
	return (s16)lroundf(value);
}

static void VehTurbo_BuildLocalRotation(
	const float rotation[3],
	float out[3][3])
{
	const float degToRad = 0.01745329251994329577f;
	float rx = rotation[0] * degToRad;
	float ry = rotation[1] * degToRad;
	float rz = rotation[2] * degToRad;
	float sx = sinf(rx), cx = cosf(rx);
	float sy = sinf(ry), cy = cosf(ry);
	float sz = sinf(rz), cz = cosf(rz);

	/*
	 * Euler order matches VehEmitter: local X, then Y, then Z.
	 * Matrix form is Rz * Ry * Rx.
	 */
	out[0][0] = cz * cy;
	out[0][1] = cz * sy * sx - sz * cx;
	out[0][2] = cz * sy * cx + sz * sx;

	out[1][0] = sz * cy;
	out[1][1] = sz * sy * sx + cz * cx;
	out[1][2] = sz * sy * cx - cz * sx;

	out[2][0] = -sy;
	out[2][1] = cy * sx;
	out[2][2] = cy * cx;
}

static int VehTurbo_GetCustomExhaust(
	const struct Driver *driver,
	int exhaustIndex,
	float position[3],
	float rotation[3],
	int *outCount)
{
	int kartID;
	int count;

	if (outCount != NULL)
		*outCount = -1;

	if (driver == NULL)
		return 0;

	kartID = RacerLoadout_GetEffectiveKart((int)driver->driverID);
	if (kartID < 0)
		return 0;

	count = VehiclePartRegistry_GetExhaustCount(kartID);
	if (outCount != NULL)
		*outCount = count;

	if (count < 0 ||
		exhaustIndex < 0 ||
		exhaustIndex >= count)
		return 0;

	return VehiclePartRegistry_GetExhaustMount(
		kartID, exhaustIndex, position, rotation);
}

static int VehTurbo_ApplyCustomExhaustTransform(
	struct Driver *driver,
	struct Instance *effectInst,
	int exhaustIndex,
	int fireSize)
{
	struct Instance *driverInst;
	float position[3];
	float rotation[3];
	float localRotation[3][3];
	float alignedRotation[3][3];
	float nozzleScale;

	if (driver == NULL || effectInst == NULL)
		return 0;

	driverInst = driver->instSelf;
	if (driverInst == NULL ||
		!VehTurbo_GetCustomExhaust(
			driver,
			exhaustIndex,
			position,
			rotation,
			NULL))
	{
		return 0;
	}

	VehTurbo_BuildLocalRotation(rotation, localRotation);
	nozzleScale = VehiclePartRegistry_GetExhaustScale(
		RacerLoadout_GetEffectiveKart((int)driver->driverID), exhaustIndex);

	/* GPU measurements of original CTR fire show two biases:
	 * +17deg sideways and -45deg vertically. The correct right-hand
	 * transform is USER_XYZ * Ry(+17deg) * Rx(-45deg).
	 * Matrix multiplication is order-dependent: apply Ry to the user
	 * basis FIRST, then Rx. This is the exact GPU preview order. */
	const float axisAlign = 0.7071067811865475244f;
	const float sideYawCos = 0.9563047559630354f; /* cos 17deg */
	const float sideYawSin = 0.29237170472273677f; /* sin 17deg */
	for (int row = 0; row < 3; ++row)
	{
		float yawColumn0 =
			sideYawCos * localRotation[row][0] -
			sideYawSin * localRotation[row][2];
		float yawColumn2 =
			sideYawSin * localRotation[row][0] +
			sideYawCos * localRotation[row][2];
		alignedRotation[row][0] = yawColumn0;
		alignedRotation[row][1] =
			(localRotation[row][1] - yawColumn2) * axisAlign;
		alignedRotation[row][2] =
			(localRotation[row][1] + yawColumn2) * axisAlign;
	}

	/*
	 * Kart exhaust attachments use the same local units as smoke. Retail turbo
	 * flame instances use a translation roughly one quarter as large before the
	 * flame mesh's own scaled geometry reaches the exhaust mouth (vanilla is
	 * about 72/208/-208 for smoke versus 18/48/-52 for flame translation).
	 * Convert the custom mount to that turbo-instance space instead of moving
	 * the flame instance all the way to the particle coordinate.
	 */

	for (int row = 0; row < 3; row++)
	{
		for (int col = 0; col < 3; col++)
		{
			float value = 0.0f;

			for (int axis = 0; axis < 3; axis++)
			{
				value +=
					(float)driverInst->matrix.m[row][axis] *
					alignedRotation[axis][col];
			}

			/* All custom nozzles use the SAME unmirrored base transform.
			 * Only their own XYZ rotation and scale may differ. */
			value *= (float)fireSize * nozzleScale /
				(float)(1 << TURBO_FIRE_MATRIX_SCALE_SHIFT);

			effectInst->matrix.m[row][col] =
				VehTurbo_ClampS16(value);
		}
	}

	VehTurbo_TransformOffset(
		driverInst,
		VehTurbo_ClampS16(
			position[0] * (float)driverInst->scale.x / 16384.0f),
		VehTurbo_ClampS16(
			position[1] * (float)driverInst->scale.y / 16384.0f),
		VehTurbo_ClampS16(
			position[2] * (float)driverInst->scale.z / 16384.0f),
		&effectInst->matrix.t[0]);

	return 1;
}

/* Synchronize additional 3D exhausts with the retail turbo animation.
 * This is native-only; no extra threads or sound loops are created.
 * Missing attachment XYZ hides only the affected flame.
 */
static void VehTurbo_SyncExtraExhausts(
	struct Turbo *turbo,
	struct Instance *primary,
	int fireSize)
{
	if (turbo == NULL || primary == NULL ||
		turbo->nativeExtraExhaust == NULL || sdata->gGT == NULL)
		return;

	for (int i = 0; i < turbo->nativeExtraExhaustCount; ++i)
	{
		struct Instance *extra = turbo->nativeExtraExhaust[i];
		int nozzle = i + 2;
		if (extra == NULL)
			continue;

		extra->flags = primary->flags;
		extra->alphaScale = primary->alphaScale;
		extra->vertSplit = primary->vertSplit;
		extra->reflectionRGBA = primary->reflectionRGBA;
		extra->matrix = primary->matrix;
		/* The primary flame is assigned the frame rendered THIS tick.
		 * turbo->fireAnimIndex may already have advanced for the NEXT tick,
		 * so re-reading the index made outlet 3+ lag/lead outlets 1 and 2.
		 * Reuse the exact model pointer to guarantee identical mesh/phase. */
		extra->model = primary->model;

		if (!VehTurbo_ApplyCustomExhaustTransform(
				turbo->driver, extra, nozzle, fireSize))
			extra->flags |= HIDE_MODEL;
	}
}

static void VehTurbo_EnforceCustomExhaustCount(
	struct Driver *driver,
	struct Instance *primary,
	struct Instance *secondary)
{
	int kartID;
	int count;

	if (driver == NULL)
		return;

	kartID = RacerLoadout_GetEffectiveKart((int)driver->driverID);
	if (kartID < 0)
		return;

	count = VehiclePartRegistry_GetExhaustCount(kartID);
	if (count < 0)
		return;

	/* Custom outlets exist only when their entire XYZ offset was assigned.
	 * Otherwise the old vanilla flame transform must stay invisible. */
	if (primary != NULL &&
		(count <= 0 ||
		 !VehiclePartRegistry_GetExhaustMount(kartID, 0, NULL, NULL)))
		primary->flags |= HIDE_MODEL;

	if (secondary != NULL &&
		(count <= 1 ||
		 !VehiclePartRegistry_GetExhaustMount(kartID, 1, NULL, NULL)))
		secondary->flags |= HIDE_MODEL;
}
#endif

void VehTurbo_VisualTransformBucket(struct Thread *turboThread)
{
#if defined(CTR_NATIVE)
	while (turboThread != NULL)
	{
		struct Turbo *turbo = (struct Turbo *)turboThread->object;
		struct Instance *instance = turboThread->inst;
		struct Instance *instanceDriver = (turbo && turbo->driver) ? turbo->driver->instSelf : NULL;
		struct Instance *secondary = turbo ? turbo->inst : NULL;
		if (turbo && instance && secondary && instanceDriver)
		{
			int fireSize = (int)turbo->fireSize;
			if (fireSize > TURBO_FIRE_SIZE_MAX) fireSize = TURBO_FIRE_SIZE_MAX;
			if (fireSize < TURBO_FIRE_SIZE_MIN) fireSize = TURBO_FIRE_SIZE_MIN;

			for (int r = 0; r < 3; r++)
			{
				for (int c = 0; c < 3; c++)
				{
					int base = instanceDriver->matrix.m[r][c];
					instance->matrix.m[r][c] = (s16)(base * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
					secondary->matrix.m[r][c] = (s16)(((c == 0) ? -base : base) * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
				}
			}

			VehTurbo_TransformOffset(instanceDriver, instanceDriver->scale.x * TURBO_FIRE_LEFT_X_NUMERATOR >> TURBO_FIRE_LEFT_X_SHIFT,
			                         instanceDriver->scale.y * TURBO_FIRE_Y_NUMERATOR >> TURBO_FIRE_Y_SHIFT,
			                         instanceDriver->scale.z * TURBO_FIRE_Z_NUMERATOR >> TURBO_FIRE_Z_SHIFT, &instance->matrix.t[0]);
			VehTurbo_TransformOffset(instanceDriver, instanceDriver->scale.x * TURBO_FIRE_RIGHT_X_NUMERATOR >> TURBO_FIRE_RIGHT_X_SHIFT,
			                         instanceDriver->scale.y * TURBO_FIRE_Y_NUMERATOR >> TURBO_FIRE_Y_SHIFT,
			                         instanceDriver->scale.z * TURBO_FIRE_Z_NUMERATOR >> TURBO_FIRE_Z_SHIFT, &secondary->matrix.t[0]);

			/*
			 * Custom kart exhaust attachments override both flame position and
			 * orientation. Missing/incomplete mounts retain the vanilla transform.
			 */
			VehTurbo_ApplyCustomExhaustTransform(
				turbo->driver, instance, 0, fireSize);
			VehTurbo_ApplyCustomExhaustTransform(
				turbo->driver, secondary, 1, fireSize);
			VehTurbo_EnforceCustomExhaustCount(
				turbo->driver, instance, secondary);
			VehTurbo_SyncExtraExhausts(turbo, instance, fireSize);
		}
		turboThread = turboThread->siblingThread;
	}
#else
	(void)turboThread;
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800693c8-0x80069bb0.
void VehTurbo_ThTick(struct Thread *turboThread)
{
	struct GameTracker *gGT = sdata->gGT;

	struct Turbo *turbo = (struct Turbo *)turboThread->object;
	struct Driver *driver = turbo->driver;
	struct Instance *instance = turboThread->inst;
	struct Instance *instanceDriver = driver->instSelf;
	int legacyTicks = 1;
#if defined(CTR_NATIVE)
	if (Platform_GetHighRefreshMode())
	{
		legacyTicks = Platform_GetLegacy30HzTicks();
	}
#endif

	if ((
	        // if not burnt
	        (driver->burnTimer == 0) &&

	        // if alpha of turbo is zero
	        (instance->alphaScale == 0)) &&


	    (instanceDriver->thread->modelIndex != DYNAMIC_GHOST))
	{
		// cut driverInst transparency in half
		instanceDriver->alphaScale = instanceDriver->alphaScale >> 1;
	}

	// if instance is not split by water
	if ((instanceDriver->flags & SPLIT_LINE) == 0)
	{
		// instance flags
		instance->flags &= ~SPLIT_LINE;
		turbo->inst->flags &= ~SPLIT_LINE;
	}

	// if instance is split by water
	else
	{
		// turbos are now split by water, set vertical split height
		instance->flags |= SPLIT_LINE;
		instance->vertSplit = instanceDriver->vertSplit;
		turbo->inst->flags |= SPLIT_LINE;
		turbo->inst->vertSplit = instanceDriver->vertSplit;
	}

	// if driver instance is not reflective
	if ((instanceDriver->flags & REFLECTIVE) == 0)
	{
		// remove reflection from turbo instances
		instance->flags &= ~REFLECTIVE;
		turbo->inst->flags &= ~REFLECTIVE;
	}

	// if driver instance is reflective
	else
	{
		// make turbo instances reflective
		// copy reflection height axis to instance
		instance->flags |= REFLECTIVE;
		instance->vertSplit = instanceDriver->vertSplit;
		turbo->inst->flags |= REFLECTIVE;
		turbo->inst->vertSplit = instanceDriver->vertSplit;
	}

	int fireSize = (int)turbo->fireSize;
	if (TURBO_FIRE_SIZE_MAX < (int)turbo->fireSize)
	{
		fireSize = TURBO_FIRE_SIZE_MAX;
	}
	if ((int)turbo->fireSize < TURBO_FIRE_SIZE_MIN)
	{
		fireSize = TURBO_FIRE_SIZE_MIN;
	}

	// matrix of first turbo instance
	instance->matrix.m[0][0] = (s16)(instanceDriver->matrix.m[0][0] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[0][1] = (s16)(instanceDriver->matrix.m[0][1] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[0][2] = (s16)(instanceDriver->matrix.m[0][2] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[1][0] = (s16)(instanceDriver->matrix.m[1][0] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[1][1] = (s16)(instanceDriver->matrix.m[1][1] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[1][2] = (s16)(instanceDriver->matrix.m[1][2] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[2][0] = (s16)(instanceDriver->matrix.m[2][0] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[2][1] = (s16)(instanceDriver->matrix.m[2][1] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	instance->matrix.m[2][2] = (s16)(instanceDriver->matrix.m[2][2] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);

	VehTurbo_TransformOffset(instanceDriver, instanceDriver->scale.x * TURBO_FIRE_LEFT_X_NUMERATOR >> TURBO_FIRE_LEFT_X_SHIFT,
	                         instanceDriver->scale.y * TURBO_FIRE_Y_NUMERATOR >> TURBO_FIRE_Y_SHIFT,
	                         instanceDriver->scale.z * TURBO_FIRE_Z_NUMERATOR >> TURBO_FIRE_Z_SHIFT, &instance->matrix.t[0]);

	// matrix of second turbo instance, negate X axis
	turbo->inst->matrix.m[0][0] = (s16)(-(int)instanceDriver->matrix.m[0][0] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[0][1] = (s16)(instanceDriver->matrix.m[0][1] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[0][2] = (s16)(instanceDriver->matrix.m[0][2] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[1][0] = (s16)(-(int)instanceDriver->matrix.m[1][0] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[1][1] = (s16)(instanceDriver->matrix.m[1][1] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[1][2] = (s16)(instanceDriver->matrix.m[1][2] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[2][0] = (s16)(-(int)instanceDriver->matrix.m[2][0] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[2][1] = (s16)(instanceDriver->matrix.m[2][1] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);
	turbo->inst->matrix.m[2][2] = (s16)(instanceDriver->matrix.m[2][2] * fireSize >> TURBO_FIRE_MATRIX_SCALE_SHIFT);

	VehTurbo_TransformOffset(instanceDriver, instanceDriver->scale.x * TURBO_FIRE_RIGHT_X_NUMERATOR >> TURBO_FIRE_RIGHT_X_SHIFT,
	                         instanceDriver->scale.y * TURBO_FIRE_Y_NUMERATOR >> TURBO_FIRE_Y_SHIFT,
	                         instanceDriver->scale.z * TURBO_FIRE_Z_NUMERATOR >> TURBO_FIRE_Z_SHIFT, &turbo->inst->matrix.t[0]);

#if defined(CTR_NATIVE)
	VehTurbo_ApplyCustomExhaustTransform(
		driver, instance, 0, fireSize);
	VehTurbo_ApplyCustomExhaustTransform(
		driver, turbo->inst, 1, fireSize);
#endif

	// decrease turbo visibility cooldown by elapsed milliseconds per frame, ~32
	s16 elapsedTime = turbo->fireVisibilityCooldown - gGT->elapsedTimeMS;
	turbo->fireVisibilityCooldown = elapsedTime;

	// don't allow negatives
	if (elapsedTime * TURBO_COOLDOWN_SIGN_SCALE < 0)
	{
		turbo->fireVisibilityCooldown = 0;
	}

	if (turbo->fireVisibilityCooldown == 0)
	{
		// make fire visible now that there's no cooldown
		instance->flags &= ~HIDE_MODEL;
		turbo->inst->flags &= ~HIDE_MODEL;
	}

#if defined(CTR_NATIVE)
	/* Keep explicit 0/1-exhaust kart layouts from reviving hidden flames. */
	VehTurbo_EnforceCustomExhaustCount(
		driver, instance, turbo->inst);
#endif

	if (instance->alphaScale < TURBO_ALPHA_RUMBLE_THRESHOLD)
	{
		// gamepad vibration
		GAMEPAD_ShockFreq(driver, TURBO_RUMBLE_FRAMES, TURBO_RUMBLE_FORCE);
	}

	// set new model pointer, one of eight
	instance->model = gGT->modelPtr[(int)turbo->fireAnimIndex + STATIC_TURBO_EFFECT];

	// set new model pointer, one of eight

	// STATIC_TURBO_EFFECT
	// STATIC_TURBO_EFFECT1
	// STATIC_TURBO_EFFECT2
	// STATIC_TURBO_EFFECT3
	// STATIC_TURBO_EFFECT4
	// STATIC_TURBO_EFFECT5
	// STATIC_TURBO_EFFECT6
	// STATIC_TURBO_EFFECT7
	turbo->inst->model = gGT->modelPtr[(((int)turbo->fireAnimIndex + TURBO_SECONDARY_MODEL_FRAME_OFFSET) & TURBO_ANIM_FRAME_MASK) + STATIC_TURBO_EFFECT];
#if defined(CTR_NATIVE)
	/* Retail still offsets the secondary flame by three animation frames.
	 * Custom kart outlets start with precisely the same animation frame. */
	if (VehiclePartRegistry_GetExhaustCount(
			RacerLoadout_GetEffectiveKart((int)driver->driverID)) >= 0)
		turbo->inst->model = instance->model;
#endif

	if (legacyTicks > 0)
	{
		turbo->fireAnimIndex = (u8)((turbo->fireAnimIndex + legacyTicks) & TURBO_ANIM_FRAME_MASK);

		if (turbo->fireDisappearCountdown > 0)
		{
			int value = turbo->fireDisappearCountdown - legacyTicks;
			turbo->fireDisappearCountdown = (s8)((value > 0) ? value : 0);
		}
	}

	// player of any kind
	if (instanceDriver->thread->modelIndex == DYNAMIC_PLAYER)
	{
		int fireSfxVolume = TURBO_AUDIO_VOLUME_BASE - (u32)(instance->alphaScale >> TURBO_AUDIO_ALPHA_SHIFT);

		if (fireSfxVolume < 0)
		{
			fireSfxVolume = 0;
		}
		else
		{
			if (TURBO_AUDIO_VOLUME_MAX < fireSfxVolume)
			{
				fireSfxVolume = TURBO_AUDIO_VOLUME_MAX;
			}
		}

		u32 fireAudioDistort = (u32)turbo->fireAudioDistort + TURBO_AUDIO_DISTORT_STEP;

		if ((int)fireAudioDistort < 0)
		{
			fireAudioDistort = 0;
		}
		else
		{
			if (fireAudioDistort > TURBO_AUDIO_DISTORT_MAX)
			{
				fireAudioDistort = TURBO_AUDIO_DISTORT_MAX;
			}
		}

		// if echo is required
		u32 echo = ((driver->actionsFlagSet & ACTION_ENGINE_ECHO) != 0);

		// driver audio
		OtherFX_RecycleNew(&driver->driverAudioPtrs[TURBO_AUDIO_SLOT], TURBO_AUDIO_SFX_ID,
		                   HowlSfx_Pack(HOWL_SFX_LR_CENTER, fireAudioDistort, fireSfxVolume, echo));

		// manipulate turbo audio distort at the retail animation rate
		if ((legacyTicks > 0) && (turbo->fireAudioDistort < TURBO_AUDIO_DISTORT_INCREMENT_LIMIT))
		{
			int value = turbo->fireAudioDistort + legacyTicks;
			if (value > TURBO_AUDIO_DISTORT_INCREMENT_LIMIT)
			{
				value = TURBO_AUDIO_DISTORT_INCREMENT_LIMIT;
			}
			turbo->fireAudioDistort = (s16)value;
		}
	}

	u8 kartState = driver->kartState;

	if (
	    // if this is a ghost
	    (instanceDriver->thread->modelIndex == DYNAMIC_GHOST) ||

	    ((kartState != KS_MASK_GRABBED) && (kartState != KS_CRASHING)

// lol they found a glitch with this
#if BUILD > SepReview
	     && (kartState != KS_WARP_PAD)
#endif
	         ))
	{
		// if reserves are nearing zero
		if ((driver->reserves < TURBO_RESERVES_DISAPPEAR_THRESHOLD) || (turbo->fireDisappearCountdown == 0))
		{
			// if fully transparent, skip lines
			if (TURBO_ALPHA_FULL_MINUS_ONE < instance->alphaScale)
			{
				goto LAB_80069b50;
			}

			if (legacyTicks > 0)
			{
				int fadeStep = (turbo->fireDisappearCountdown == 0) ? TURBO_FADE_FAST_STEP : TURBO_FADE_SLOW_STEP;
				fadeStep *= legacyTicks;
				instance->alphaScale += fadeStep;
				turbo->inst->alphaScale += fadeStep;
			}
		}
		else
		{
			// if scale is big, skip lines
			if (TURBO_ALPHA_FULL_MINUS_ONE < instance->alphaScale)
			{
				goto LAB_80069b50;
			}
		}
	}

	// if not a ghost, and
	// kart state is mask grab, crashed, or warped
	else
	{
		// restore backup of alpha
		instanceDriver->alphaScale = driver->alphaScaleBackup;
	LAB_80069b50:

		// player of any kind
		if (instanceDriver->thread->modelIndex == DYNAMIC_PLAYER)
		{
			// volume, distortion, left/right
			u32 stopSfxParams = HOWL_SFX_CENTER_NO_DISTORTION;

			// if echo is required
			if ((driver->actionsFlagSet & ACTION_ENGINE_ECHO) != 0)
			{
				// add echo, volume, distortion, left/right
				stopSfxParams = HOWL_SFX_CENTER_NO_DISTORTION | HOWL_SFX_ECHO_FLAG;
			}

			// driver audio
			OtherFX_RecycleNew(&driver->driverAudioPtrs[TURBO_AUDIO_SLOT], TURBO_STOP_SFX_ID, stopSfxParams);
		}

		// 0x800 = this thread needs to be deleted
		turboThread->flags |= THREAD_FLAG_DEAD;
	}

#if defined(CTR_NATIVE)
	/* Update after fade, cooldown, model-frame and reflection changes. */
	VehTurbo_SyncExtraExhausts(turbo, instance, fireSize);
#endif
	// do not use infinite loop optimization,
	// modern GCC "without" the $RA skip is more
	// optimized than PSYQ "with" the $RA skip
}
