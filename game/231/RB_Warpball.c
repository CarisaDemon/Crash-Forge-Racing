#include <common.h>

static const s16 s_warpballFadeScale[6 * 3] = {
    4505, 5120, 4096, 5226, 8192, 4096, 5600, 5501, 4096, 5272, 3183, 4096, 4242, 1411, 3337, 2878, 437, 1668,
};

static const s32 s_warpballFadeY[6] = {
    -64, -256, -87, 57, 167, 228,
};

#if defined(CTR_NATIVE)
/* CNK's source IGB contains grow (333ms), spin (2s loop) and fizzle
 * (667ms). The original geometry/texture are decoded separately.
 * Until its compressed QS bone tracks are decoded, reproduce these
 * visual phases with smooth, delta-time based object transforms.
 * Never enter this function for the retail CTR Warp Orb. */
static void RB_Warpball_TornadoVisualTransform(struct Instance *inst,
    int elapsedVisualMS, int scaleQ12)
{
    int angle = ((elapsedVisualMS % 2000) * 0x1000) / 2000;
    s16 s = MATH_Sin(angle);
    s16 c = MATH_Cos(angle);
    if (scaleQ12 < 0) scaleQ12 = 0;
    if (scaleQ12 > 0x1000) scaleQ12 = 0x1000;
    inst->matrix.m[0][0] = c;
    inst->matrix.m[0][1] = 0;
    inst->matrix.m[0][2] = s;
    inst->matrix.m[1][0] = 0;
    inst->matrix.m[1][1] = 0x1000;
    inst->matrix.m[1][2] = 0;
    inst->matrix.m[2][0] = -s;
    inst->matrix.m[2][1] = 0;
    inst->matrix.m[2][2] = c;
    inst->scale.x = (s16)scaleQ12;
    inst->scale.y = (s16)scaleQ12;
    inst->scale.z = (s16)scaleQ12;
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ae524-0x800ae604.
// NOTE(aalhendi): Native uses retail fade scale/Y table bytes from 0x800b2c88 and 0x800b2cac.
void RB_Warpball_FadeAway(struct Thread *t)
{
	s16 frameId;
	struct TrackerWeapon *tw;
	struct Instance *inst;
	struct Driver *d;
	struct GameTracker *gGT;

	gGT = sdata->gGT;

	tw = t->object;
	inst = t->inst;
	frameId = tw->fadeFrame;

#if defined(CTR_NATIVE)
	if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
	{
		/* Separate 667ms fizzle for Tornado; never use Warp Orb fade. */
		int age = tw->framesSeekTargetTnt + gGT->elapsedTimeMS;
		/* Original CNK tornado.csv: 1000ms fizzle time. */
		if (age >= 1000)
		{
			d = tw->driverTarget;
			if (d != NULL)
				d->actionsFlagSet &= ~ACTION_TRACKER_TARGETED;
			t->flags |= THREAD_FLAG_DEAD;
			return;
		}
		tw->framesSeekTargetTnt = age;
		RB_Warpball_TornadoVisualTransform(inst, age,
		    ((1000 - age) * 0x1000) / 1000);
		return;
	}
#endif

	if (frameId > 5)
	{
		d = tw->driverTarget;

		if (d != NULL)
		{
			// remove 2D square-target being drawn on the player's screen
			d->actionsFlagSet &= ~ACTION_TRACKER_TARGETED;
		}

		// Tornado does not own retail warpball inventory flag.
#if defined(CTR_NATIVE)
		if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) == 0)
#endif
			gGT->gameMode1 &= ~(WARPBALL_HELD);

		// This thread is now dead
		t->flags |= THREAD_FLAG_DEAD;
		return;
	}

	// set scale (x, y, z)
	inst->scale.x = s_warpballFadeScale[(frameId * 3) + 0];
	inst->scale.y = s_warpballFadeScale[(frameId * 3) + 1];
	inst->scale.z = s_warpballFadeScale[(frameId * 3) + 2];

	inst->matrix.t[1] = tw->distFromGround + s_warpballFadeY[frameId];

	tw->fadeFrame += 1;

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800ae604-0x800ae668.
void RB_Warpball_Death(struct Thread *t)
{
	struct TrackerWeapon *tw;

	tw = t->object;
	if (tw->ptrParticle != NULL)
		tw->ptrParticle->framesLeftInLife = 0;
	tw->fadeFrame = 0;

	// play sound of warpball death
	struct Instance *inst = t->inst;
	tw->distFromGround = inst->matrix.t[1];
#if defined(CTR_NATIVE)
	if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
	{
		/* Native Tornado's independent fizzle clock starts on impact. */
		tw->framesSeekTargetTnt = 0;
		NativeAudio_CNKTornadoStop();
		NativeAudio_CNKTornadoPlay(0);
	}
	else
#endif
		PlaySound3D(0x4f, inst);

	// stop audio of moving
	OtherFX_RecycleMute(&tw->soundIDCount);

	ThTick_SetAndExec(t, &RB_Warpball_FadeAway);
	return;
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800ae668-0x800ae778.
struct CheckpointNode *RB_Warpball_NewPathNode(struct CheckpointNode *cn, struct Driver *d)
{
	struct GameTracker *gGT = sdata->gGT;
	u8 pathIndex;
	u8 targetIndex;
	int foundLeftPath;

	if (d == NULL)
	{
		return &gGT->level1->ptr_restart_points[cn->nextIndex_forward];
	}

	foundLeftPath = 0;
	targetIndex = d->checkpoint.branchChoiceIndex;
	pathIndex = cn->nextIndex_left;

	if (targetIndex == pathIndex)
	{
		return &gGT->level1->ptr_restart_points[pathIndex];
	}

	if (pathIndex != 0xff)
	{
		struct CheckpointNode *currNode = cn;

		for (int i = 0; i < 3; i++)
		{
			if (currNode->nextIndex_left == 0xff)
			{
				pathIndex = currNode->nextIndex_forward;
			}
			else
			{
				pathIndex = currNode->nextIndex_left;
			}

			currNode = &gGT->level1->ptr_restart_points[pathIndex];

			if (targetIndex == currNode->nextIndex_forward)
			{
				foundLeftPath = 1;
				break;
			}
		}
	}

	if (foundLeftPath)
	{
		return &gGT->level1->ptr_restart_points[cn->nextIndex_left];
	}

	return &gGT->level1->ptr_restart_points[cn->nextIndex_forward];
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800ae778-0x800ae7dc.
void RB_Warpball_Start(struct TrackerWeapon *tw)
{
	tw->ptrNodeCurr = RB_Warpball_NewPathNode(tw->ptrNodeCurr, tw->driverTarget);
	tw->ptrNodeNext = RB_Warpball_NewPathNode(tw->ptrNodeCurr, tw->driverTarget);
	return;
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800ae7dc-0x800aeaac.
struct Driver *RB_Warpball_GetDriverTarget(struct TrackerWeapon *tw, struct Instance *inst)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *bestDriver = NULL;

#if defined(CTR_NATIVE)
	/* The CNK Tornado targets FIRST PLACE in the current race, not the
	 * local controller's Player 1 slot. This also permits a bot to lead.
	 * Ignore racers who already finished; never choose by driverID. */
	if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
	{
		for (int rank = 0; rank < 8; ++rank)
		{
			struct Driver *leader = gGT->driversInRaceOrder[rank];
			if (leader != NULL &&
			    (leader->actionsFlagSet & ACTION_RACE_FINISHED) == 0)
				return leader;
		}
		return NULL;
	}
#endif

	if ((tw->flags & TRACKER_FLAG_POWERED_UP) == 0)
	{
		for (int i = 0; i < 8; i++)
		{
			struct Driver *driver = gGT->driversInRaceOrder[i];

			if ((driver != NULL) && (driver != tw->driverParent) && ((driver->actionsFlagSet & ACTION_RACE_FINISHED) == 0))
			{
				return driver;
			}
		}

		return bestDriver;
	}

	struct CheckpointNode *nodes = gGT->level1->ptr_restart_points;
	struct CheckpointNode *node1 = &nodes[tw->ptrNodeCurr->nextIndex_forward];
	struct CheckpointNode *node2 = &nodes[node1->nextIndex_forward];
	int trackDistance = nodes[0].distToFinish << 3;
	SVec3 pathVector;
	SVec3 orbVector;
	s32 projectedDistance;
	int bestDistance = 0x7fffffff;

	pathVector.x = (s16)(node1->pos.x - node2->pos.x);
	pathVector.y = (s16)(node1->pos.y - node2->pos.y);
	pathVector.z = (s16)(node1->pos.z - node2->pos.z);
	MATH_VectorNormalize(&pathVector);

	orbVector.x = (s16)(inst->matrix.t[0] - node1->pos.x);
	orbVector.y = (s16)(inst->matrix.t[1] - node1->pos.y);
	orbVector.z = (s16)(inst->matrix.t[2] - node1->pos.z);

	// NOTE(aalhendi): Retail uses GTE MVMVA MAC1; this is the same row0 dot product.
	projectedDistance = ((s32)pathVector.x * orbVector.x) + ((s32)pathVector.y * orbVector.y) + ((s32)pathVector.z * orbVector.z);

	projectedDistance = ((node1->distToFinish << 3) + (projectedDistance >> 12) + 0x200) % trackDistance;

	for (int i = 0; i < 8; i++)
	{
		struct Driver *driver = gGT->drivers[i];

		if ((driver != NULL) && ((tw->driversHit & (1u << (i & 0x1f))) == 0) && ((driver->actionsFlagSet & ACTION_RACE_FINISHED) == 0) &&
		    (driver->kartState != KS_MASK_GRABBED))
		{
			int distance = projectedDistance - driver->distanceToFinish_curr;

			if (distance < 0)
			{
				distance += trackDistance;
			}

			if (distance < bestDistance)
			{
				bestDistance = distance;
				bestDriver = driver;
			}
		}
	}

	return bestDriver;
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800aeaac-0x800aece0.
void RB_Warpball_SetTargetDriver(struct TrackerWeapon *tw)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *target = tw->driverTarget;

	if (target == NULL)
	{
		return;
	}

	struct CheckpointNode *nodes = gGT->level1->ptr_restart_points;
	struct CheckpointNode *targetNode = &nodes[target->checkpoint.currentIndex];
	struct CheckpointNode *prevNode = targetNode;
	int targetDistance = target->distanceToFinish_curr;

	while ((((int)targetNode->distToFinish << 3) >= targetDistance) && (targetNode != nodes))
	{
		prevNode = targetNode;
		targetNode = RB_Warpball_NewPathNode(targetNode, tw->driverTarget);
	}
	targetNode = prevNode;

	struct CheckpointNode *rightPathNode = NULL;

	if ((tw->flags & TRACKER_FLAG_WARPBALL_TARGET_PATH) == 0)
	{
		struct CheckpointNode *pathStarts[2];

		pathStarts[0] = tw->ptrNodeCurr;
		pathStarts[1] = rightPathNode;

		for (int i = 0; (i < 2) && ((tw->flags & TRACKER_FLAG_WARPBALL_TARGET_PATH) == 0); i++)
		{
			struct CheckpointNode *pathNode = pathStarts[i];

			if (pathNode == NULL)
			{
				continue;
			}

			for (int j = 0; j < 3; j++)
			{
				if (pathNode == targetNode)
				{
					tw->flags = (tw->flags & ~TRACKER_FLAG_WARPBALL_FALLBACK_PATH) | TRACKER_FLAG_WARPBALL_TARGET_PATH;
					break;
				}

				if (pathNode->nextIndex_right != 0xff)
				{
					rightPathNode = &nodes[pathNode->nextIndex_right];
					pathStarts[1] = rightPathNode;
				}

				pathNode = &nodes[pathNode->nextIndex_backward];
			}
		}
	}

	struct CheckpointNode *pathNode = tw->ptrNodeCurr;

	for (int i = 0; i < 3; i++)
	{
		if (pathNode == targetNode)
		{
			tw->flags = (tw->flags & ~TRACKER_FLAG_WARPBALL_FALLBACK_PATH) | TRACKER_FLAG_WARPBALL_TARGET_PATH;
			return;
		}

		pathNode = RB_Warpball_NewPathNode(pathNode, tw->driverTarget);
	}
}

// NOTE(aalhendi): ASM-verified against NTSC-U 926 overlay 231 0x800aece0-0x800aede0.
void RB_Warpball_SeekDriver(struct TrackerWeapon *tw, u32 checkpointIndex, struct Driver *d)
{
	checkpointIndex &= 0xff;

	if (d == 0)
	{
		return;
	}
	if (checkpointIndex == 0xff)
	{
		return;
	}

	struct CheckpointNode *first = &sdata->gGT->level1->ptr_restart_points[0];

	// pointer to path node
	struct CheckpointNode *cn = &first[checkpointIndex];

	while ((d->distanceToFinish_curr <= (u32)(cn->distToFinish << 3)) &&

	       // node is not first node
	       (cn != first))
	{
		cn = RB_Warpball_NewPathNode(cn, tw->driverTarget);
	}

	// path index = pathPtr2 - pathPtr1
	tw->nodeCurrIndex = (u8)(cn - first);

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800aede0-0x800aef9c.
void RB_Warpball_TurnAround(struct Thread *t)
{
	struct TrackerWeapon *tw;
	struct Instance *inst;
	TrackerWeaponFlags flags;
	struct GameTracker *gGT = sdata->gGT;
	s16 rot;

	tw = t->object;
	inst = t->inst;
	flags = tw->flags;

	if (
	    // if turnaround was requested
	    ((flags & TRACKER_FLAG_WARPBALL_TURN_AROUND) != 0) ||

	    // if no driver is being chased
	    (tw->driverTarget == NULL))
	{
		if ((flags & TRACKER_FLAG_WARPBALL_TARGET_PATH) != 0)
		{
			tw->flags = (flags & ~TRACKER_FLAG_WARPBALL_TARGET_PATH) | TRACKER_FLAG_WARPBALL_BACKTRACKING | TRACKER_FLAG_WARPBALL_FALLBACK_PATH;
		}

		tw->vel.x = -tw->vel.x;
		tw->vel.y = -tw->vel.y;
		tw->vel.z = -tw->vel.z;

		inst->matrix.t[0] += ((int)tw->vel.x * gGT->elapsedTimeMS) >> 5;
		inst->matrix.t[1] += ((int)tw->vel.y * gGT->elapsedTimeMS) >> 5;
		inst->matrix.t[2] += ((int)tw->vel.z * gGT->elapsedTimeMS) >> 5;

		// increment counter
		tw->turnAroundFrames++;

		if (
		    // if count too high
		    (0x78 < tw->turnAroundFrames) ||

		    // pointer to driver being chased,
		    // is null, so warpball is chasing nobody
		    (tw->driverTarget == 0))
		{
			tw->driverParent->instBombThrow = 0;

			// play sound warpball death
			PlaySound3D(0x4f, inst);

			RB_Warpball_Death(t);
		}

		// if attempted to turn around 3 times
		if ((tw->turnAroundFrames & 3) == 0)
		{
			tw->ptrNodeNext = tw->ptrNodeCurr;

			struct CheckpointNode *first = &sdata->gGT->level1->ptr_restart_points[0];

			// set new end to 10 path indices ahead of current
			tw->ptrNodeCurr = &first[tw->ptrNodeCurr->nextIndex_backward];
		}

		struct CheckpointNode *cn = tw->ptrNodeCurr;

		// rotation
		rot = ratan2(cn->pos.x - inst->matrix.t[0], cn->pos.z - inst->matrix.t[2]);

		// rotation
		tw->dir.y = rot;
	}
	return;
}

static const s16 s_warpballParticleHeight = 0xff;

#if defined(CTR_NATIVE)

/* Separate per-racer capture state. Do NOT store in Player1, the Warp Orb,
 * or recycled kart state: the leader may be a human OR a bot. */
enum
{
    CNK_TORNADO_CAPTURE_MS = 3000,         /* original CNK tornado.csv */
    CNK_TORNADO_CAPTURE_LIFT_FORCE = 30,   /* original CNK tornado.csv */
    CNK_TORNADO_CAPTURE_RISE_MS = 1350,
    CNK_TORNADO_CAPTURE_HEIGHT_FP8 = 470 * 256,
    CNK_TORNADO_CAPTURE_SPEED_UP = 0x2900,
    CNK_TORNADO_CAPTURE_SPEED_DOWN = -0x1500
};

struct CNKTornadoCapture
{
    struct Driver *victim;
    int ageMS;
    int startPosY;
    int startPosX;
    int startPosZ;
    int tornadoGroundY;
    /* Keep vertical velocity between physics frames. The original code
     * recreated it from a ground-reset velocity each tick (observed 60-75),
     * resulting in 0 real elevation after 3s. */
    int liftVelocityY;
    int lastLoggedStage;
};

static struct CNKTornadoCapture s_cnkTornadoCapture[8];

static struct CNKTornadoCapture *RB_CNKTornado_CaptureFor(struct Driver *d)
{
    struct CNKTornadoCapture *state;
    if (d == NULL || d->driverID >= 8 || sdata == NULL ||
        sdata->gGT == NULL || sdata->gGT->drivers[d->driverID] != d)
        return NULL;
    state = &s_cnkTornadoCapture[d->driverID];
    if (state->victim != d || state->ageMS >= CNK_TORNADO_CAPTURE_MS)
        return NULL;
    if (d->kartState == KS_MASK_GRABBED ||
        (d->actionsFlagSet & ACTION_RACE_FINISHED) != 0)
    {
        state->victim = NULL;
        return NULL;
    }
    return state;
}

/* Only active on victims actually grabbed by the new CNK Tornado.
 * Never affects idle racers or the original CTR Warp Orb. */
int RB_CNKTornado_IsCapturing(struct Driver *d)
{
    return RB_CNKTornado_CaptureFor(d) != NULL;
}

/* Lock the victim to its exact X/Z at impact, with the original collision
 * solver still responsible for movement in Y. This removes forward momentum
 * as soon as the Tornado grabs the kart, including at 200+ FPS. */
void RB_CNKTornado_HoldHorizontal(struct Driver *d)
{
    struct CNKTornadoCapture *c = RB_CNKTornado_CaptureFor(d);
    if (c == NULL) return;

    d->posCurr.x = c->startPosX;
    d->posCurr.z = c->startPosZ;
    d->velocity.x = 0;
    d->velocity.z = 0;
    d->accel.x = 0;
    d->accel.z = 0;
    d->xSpeed = 0;
    d->zSpeed = 0;
    d->baseSpeed = 0;
    d->speedApprox = 0;
    d->speed = 0;
    d->forwardAccelImpulse = 0;
    d->forwardAccelVector.x = 0;
    d->forwardAccelVector.z = 0;

    if ((d->actionsFlagSet & ACTION_BOT) != 0)
    {
        d->botData.aiPhysics.speedLinear = 0;
        /* Bot position = AI positionBackup + AI velocity offset. Keep the
         * integrator synchronized so there is no snap on release. */
        d->botData.positionBackup.x =
            c->startPosX - d->botData.aiPhysics.velocity.x;
        d->botData.positionBackup.z =
            c->startPosZ - d->botData.aiPhysics.velocity.z;
    }
}

static void RB_CNKTornado_BeginCapture(struct Driver *d)
{
    struct CNKTornadoCapture *state;
    if (d == NULL || d->driverID >= 8) return;
    state = &s_cnkTornadoCapture[d->driverID];
    state->victim = d;
    state->ageMS = 0;
    state->startPosY = d->posCurr.y;
    state->startPosX = d->posCurr.x;
    state->startPosZ = d->posCurr.z;
    /* Anchor the tornado funnel to the track, never to the kart as it
     * rises. quadBlockHeight is the ground under the victim (24.8). */
    state->tornadoGroundY = d->posCurr.y >> 8;
    if (d->underDriver != NULL && d->quadBlockHeight <= d->posCurr.y &&
        d->posCurr.y - d->quadBlockHeight < (600 << 8))
        state->tornadoGroundY = d->quadBlockHeight >> 8;
    state->lastLoggedStage = 0;
    state->liftVelocityY = d->velocity.y > 0 ? d->velocity.y : 0;
    d->posPrev.x = state->startPosX;
    d->posPrev.z = state->startPosZ;
    RB_CNKTornado_HoldHorizontal(d);
    Platform_Log("[CNK TORNADO QA] HOLD_GROUND driver=%d x=%d z=%d groundY=%d\n",
        d->driverID, state->startPosX >> 8, state->startPosZ >> 8,
        state->tornadoGroundY);
    Platform_LogWarn("[CNK TORNADO QA] CAPTURE_BEGIN driver=%d bot=%d startY=%d liftMS=%d sourceForce=%d\n",
        d->driverID, (d->actionsFlagSet & ACTION_BOT) != 0,
        state->startPosY, CNK_TORNADO_CAPTURE_MS, CNK_TORNADO_CAPTURE_LIFT_FORCE);
}

/* This acts on velocity in the existing physical movement/collision solver.
 * Cubic ease-in/out defines the target altitude, while a limited vertical
 * force moves the kart there. Collision, gravity, and camera remain native.
 * Capture lasts 3s (CNK), then the kart falls under regular gravity.
 * Using elapsedMS rather than 30Hz frames works at 200Hz/Unlimited. */
int RB_CNKTornado_TickLiftVelocity(struct Driver *d, int velocityY, int elapsedMS)
{
    struct CNKTornadoCapture *capture = RB_CNKTornado_CaptureFor(d);
    int age, rise, desiredHeight, difference, desiredSpeed, accelerationLimit;
    int64_t diff64;
    if (capture == NULL) return velocityY;
    /* Freeze X/Z before collision integration without changing the vertical
     * lift force or ordinary gravity after release. */
    RB_CNKTornado_HoldHorizontal(d);
    if (elapsedMS <= 0) return velocityY;
    if (elapsedMS > 64) elapsedMS = 64;
    age = capture->ageMS + elapsedMS;
    if (age >= CNK_TORNADO_CAPTURE_MS)
    {
        capture->ageMS = CNK_TORNADO_CAPTURE_MS;
        capture->victim = NULL;
        Platform_LogWarn("[CNK TORNADO QA] CAPTURE_RELEASE driver=%d altitude=%d ms=%d\n",
            d->driverID, (d->posCurr.y - capture->startPosY) >> 8,
            age);
        return velocityY;
    }
    capture->ageMS = age;
    rise = age < CNK_TORNADO_CAPTURE_RISE_MS ?
        age : CNK_TORNADO_CAPTURE_RISE_MS;
    /* 3t^2 - 2t^3 smoothstep; 64bit prevents overflow in fixed point. */
    desiredHeight = (int)((int64_t)CNK_TORNADO_CAPTURE_HEIGHT_FP8 *
        rise * rise * (3 * CNK_TORNADO_CAPTURE_RISE_MS - 2 * rise) /
        ((int64_t)CNK_TORNADO_CAPTURE_RISE_MS *
         CNK_TORNADO_CAPTURE_RISE_MS * CNK_TORNADO_CAPTURE_RISE_MS));
    diff64 = (int64_t)capture->startPosY + desiredHeight - d->posCurr.y;
    if (diff64 > 500000) diff64 = 500000;
    if (diff64 < -500000) diff64 = -500000;
    difference = (int)diff64;
    desiredSpeed = difference / 15;
    if (desiredSpeed > CNK_TORNADO_CAPTURE_SPEED_UP)
        desiredSpeed = CNK_TORNADO_CAPTURE_SPEED_UP;
    if (desiredSpeed < CNK_TORNADO_CAPTURE_SPEED_DOWN)
        desiredSpeed = CNK_TORNADO_CAPTURE_SPEED_DOWN;
    accelerationLimit = CNK_TORNADO_CAPTURE_LIFT_FORCE * elapsedMS;

    /* Persist the physical upward force over the grab. In the old build
     * PhysLinear/ConvertSpeedToVec reconstructed velocity.y every frame and
     * ground collision brought it back to ~0; acceleration never accumulated.
     * All position changes still happen through CTR moved/fixed collision. */
    velocityY = capture->liftVelocityY;
    if (desiredSpeed - velocityY > accelerationLimit)
        velocityY += accelerationLimit;
    else if (desiredSpeed - velocityY < -accelerationLimit)
        velocityY -= accelerationLimit;
    else
        velocityY = desiredSpeed;
    capture->liftVelocityY = velocityY;
    if (age > 400 && capture->lastLoggedStage == 0)
    {
        capture->lastLoggedStage = 1;
        Platform_Log("[CNK TORNADO QA] LIFT_RISING driver=%d altitude=%d velocityY=%d targetAltitude=%d\n",
            d->driverID, (d->posCurr.y - capture->startPosY) >> 8,
            velocityY, desiredHeight >> 8);
    }
    else if (age > 1700 && capture->lastLoggedStage == 1)
    {
        capture->lastLoggedStage = 2;
        Platform_Log("[CNK TORNADO QA] LIFT_FLOAT driver=%d altitude=%d velocityY=%d targetAltitude=%d\n",
            d->driverID, (d->posCurr.y - capture->startPosY) >> 8,
            velocityY, desiredHeight >> 8);
    }
    return velocityY;
}

enum
{
	/* Fixed-point Q8.8 speed scales: retail Warp Orb = 256. */
	CNK_TORNADO_SPEED_NORMAL_Q8 = 294, /* 1.1484x */
	CNK_TORNADO_SPEED_JUICED_Q8 = 768,  /* 3.0000x */
	CNK_TORNADO_AVOID_RADIUS = 520,
	CNK_TORNADO_AVOID_LATERAL = 310,
	CNK_TORNADO_AVOID_STEP_Q8 = 1,
	CNK_TORNADO_HIT_RADIUS_SQ = 0x9000
};

/* Keep the 200Hz/unlimited fractional milliseconds; the release build still
 * follows the original Warp Orb timer unchanged. timeAlive is unused there. */
static int RB_Warpball_TornadoScaledMS(struct TrackerWeapon *tw, int ms)
{
	int scale = (tw->flags & TRACKER_FLAG_POWERED_UP) != 0 ?
	    CNK_TORNADO_SPEED_JUICED_Q8 : CNK_TORNADO_SPEED_NORMAL_Q8;
	int accumulation = ms * scale + tw->timeAlive;
	tw->timeAlive = accumulation & 255;
	return accumulation >> 8;
}

/* The CNK Tornado bends *around* any non-leading racer, even human
 * players, and never damages or retargets them. rotY holds the smooth
 * lateral dodge offset (tornado-only); the path follows the race leader. */
static void RB_Warpball_TornadoAvoidBystanders(
	struct TrackerWeapon *tw, struct Instance *inst, int elapsedMS)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *target = tw->driverTarget;
	int lateralWanted = 0;
	int headingSin = MATH_Sin(tw->dir.y);
	int headingCos = MATH_Cos(tw->dir.y);
	int bestDistance = CNK_TORNADO_AVOID_RADIUS * CNK_TORNADO_AVOID_RADIUS;
	if (target != NULL)
	{
		int tx = (target->posCurr.x >> 8) - inst->matrix.t[0];
		int tz = (target->posCurr.z >> 8) - inst->matrix.t[2];
		/* Do not dodge around the actual victim on final approach. */
		if ((int64_t)tx * tx + (int64_t)tz * tz < (int64_t)350 * 350)
			bestDistance = 0;
	}
	for (int i = 0; i < 8; i++)
	{
		struct Driver *bot = gGT->drivers[i];
		int dx, dz, distSq, forward, lateral;
		/* The current leader is the only intended victim, regardless of ID.
		 * Ignore our own kart briefly while the projectile is launching. */
		if (bot == NULL || bot == target ||
		    (bot == tw->driverParent && tw->parentSafetyFrames > 0) ||
		    (bot->actionsFlagSet & ACTION_RACE_FINISHED))
			continue;
		dx = (bot->posCurr.x >> 8) - inst->matrix.t[0];
		dz = (bot->posCurr.z >> 8) - inst->matrix.t[2];
		if ((int64_t)dx * dx + (int64_t)dz * dz >= bestDistance)
			continue;
		forward = (int)(((int64_t)dx * headingSin + (int64_t)dz * headingCos) >> 12);
		if (forward < -75)
			continue;
		lateral = (int)(((int64_t)dx * headingCos - (int64_t)dz * headingSin) >> 12);
		distSq = dx * dx + dz * dz;
		bestDistance = distSq;
		/* Go around the side opposite the obstacle. */
		lateralWanted = lateral < 0 ? CNK_TORNADO_AVOID_LATERAL :
		    -CNK_TORNADO_AVOID_LATERAL;
	}
	{
		int oldOffset = tw->rotY;
		int lateralOffset = oldOffset;
		int maxStep = elapsedMS * CNK_TORNADO_AVOID_STEP_Q8 * 11;
		int delta = lateralWanted - lateralOffset;
		int appliedOffset;
		if (maxStep < 3) maxStep = 3;
		if (delta > maxStep) delta = maxStep;
		if (delta < -maxStep) delta = -maxStep;
		lateralOffset += delta;
		tw->rotY = (s16)lateralOffset;
		/* In tracking mode, position is integrated: add only the change.
		 * In checkpoint interpolation mode, position is rebuilt every tick:
		 * apply the full dodge offset. Otherwise it drifts off the track. */
		appliedOffset = (tw->flags & TRACKER_FLAG_WARPBALL_FALLBACK_PATH) != 0 ?
		    lateralOffset : (lateralOffset - oldOffset);
		inst->matrix.t[0] += (int)(((int64_t)headingCos * appliedOffset) >> 12);
		inst->matrix.t[2] -= (int)(((int64_t)headingSin * appliedOffset) >> 12);
	}
}

/* Only the CURRENT race leader may be hit. The swept collision avoids
 * missing a bot leader when running at 200Hz or at the 3x Wumpa speed. */
static struct Instance *RB_Warpball_TornadoCollideLeader(
	struct TrackerWeapon *tw, struct Instance *inst,
	int oldX, int oldY, int oldZ)
{
	struct Driver *leader = tw->driverTarget;
	int64_t vx, vz, toLeaderX, toLeaderZ, distanceSq, projection;
	int64_t nearX, nearY, nearZ, dx, dy, dz;
	if (leader == NULL || leader->instSelf == NULL ||
	    leader != RB_Warpball_GetDriverTarget(tw, inst) ||
	    leader->kartState == KS_MASK_GRABBED ||
	    (leader->actionsFlagSet & ACTION_RACE_FINISHED) != 0 ||
	    tw->parentSafetyFrames > 0)
		return NULL;
	vx = (int64_t)inst->matrix.t[0] - oldX;
	vz = (int64_t)inst->matrix.t[2] - oldZ;
	toLeaderX = (int64_t)leader->instSelf->matrix.t[0] - oldX;
	toLeaderZ = (int64_t)leader->instSelf->matrix.t[2] - oldZ;
	distanceSq = vx * vx + vz * vz;
	projection = 0;
	if (distanceSq > 0)
	{
		projection = (toLeaderX * vx + toLeaderZ * vz) * 4096 / distanceSq;
		if (projection < 0) projection = 0;
		if (projection > 4096) projection = 4096;
	}
	nearX = oldX + vx * projection / 4096;
	nearZ = oldZ + vz * projection / 4096;
	nearY = oldY + ((int64_t)inst->matrix.t[1] - oldY) * projection / 4096;
	dx = (int64_t)leader->instSelf->matrix.t[0] - nearX;
	dy = (int64_t)leader->instSelf->matrix.t[1] - nearY;
	dz = (int64_t)leader->instSelf->matrix.t[2] - nearZ;
	if (dx * dx + dz * dz < CNK_TORNADO_HIT_RADIUS_SQ &&
	    dy * dy < CNK_TORNADO_HIT_RADIUS_SQ * 4)
		return leader->instSelf;
	return NULL;
}

/* Unlike CTR's Orb impact, CNK holds the victim for the complete 3-second
 * lift. The funnel remains grounded at the impact point while the kart
 * rises in its own Y-only physics. It fades out after releasing the kart. */
static void RB_Warpball_TornadoCarryTick(struct Thread *t)
{
    struct TrackerWeapon *tw = t->object;
    struct Instance *inst = t->inst;
    struct Driver *driver = tw->driverTarget;
    struct CNKTornadoCapture *capture = RB_CNKTornado_CaptureFor(driver);
    int elapsedMS = sdata->gGT->elapsedTimeMS;
    if (capture == NULL || driver == NULL || driver->instSelf == NULL)
    {
        RB_Warpball_Death(t);
        return;
    }
    if (elapsedMS < 0) elapsedMS = 0;
    if (elapsedMS > 64) elapsedMS = 64;
    tw->framesSeekTargetTnt += elapsedMS;
    /* Grounded funnel: fixed X/Z and constant floor Y for the entire
     * capture, even if the kart is hundreds of units above it. */
    inst->matrix.t[0] = capture->startPosX >> 8;
    inst->matrix.t[2] = capture->startPosZ >> 8;
    inst->matrix.t[1] = capture->tornadoGroundY;
    RB_Warpball_TornadoVisualTransform(inst,
        tw->framesSeekTargetTnt, 0x1000);
}

#endif

static void RB_Warpball_AdvanceStraight(struct TrackerWeapon *tw, struct Instance *inst, int elapsedTime)
{
	inst->matrix.t[0] += ((int)tw->vel.x * elapsedTime) >> 5;
	inst->matrix.t[1] += ((int)tw->vel.y * elapsedTime) >> 5;
	inst->matrix.t[2] += ((int)tw->vel.z * elapsedTime) >> 5;
}

static int RB_Warpball_NodeDeltaLength(struct CheckpointNode *curr, struct CheckpointNode *next, int *dx, int *dy, int *dz)
{
	*dx = next->pos.x - curr->pos.x;
	*dy = next->pos.y - curr->pos.y;
	*dz = next->pos.z - curr->pos.z;

	return SquareRoot0_stub(((*dx) * (*dx)) + ((*dy) * (*dy)) + ((*dz) * (*dz)));
}

static void RB_Warpball_SetQuadblockIndex(struct TrackerWeapon *tw, struct ScratchpadStruct *sps)
{
	if (sps->hit.ptrQuadblock->checkpointIndex != 0xff)
	{
		tw->nodeNextIndex = sps->hit.ptrQuadblock->checkpointIndex;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800aef9c-0x800afb70.
// NOTE(aalhendi): Native uses the extracted warpball particle-height halfword from RDATA 0x800b2c84.
void RB_Warpball_ThTick(struct Thread *t)
{
	struct GameTracker *gGT;
	struct TrackerWeapon *tw;
	struct Instance *inst;
	struct Driver *target;
	struct ScratchpadStruct *sps;
	struct Instance *hitInst;
	SVec3 posTop;
	SVec3 posBottom;
	int elapsedTime;
	int distX;
	int distY;
	int distZ;
	int distXZ;
#if defined(CTR_NATIVE)
	int tornadoStartX = 0;
	int tornadoStartY = 0;
	int tornadoStartZ = 0;
#endif

	gGT = sdata->gGT;
	inst = t->inst;
	tw = t->object;

#if defined(CTR_NATIVE)
	if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
	{
		tornadoStartX = inst->matrix.t[0];
		tornadoStartY = inst->matrix.t[1];
		tornadoStartZ = inst->matrix.t[2];
		/* Refresh the CURRENT leader every tick. Recalculate route when
		 * someone overtakes the previous target; never stick to Player 1. */
		struct Driver *previous = tw->driverTarget;
		struct Driver *leader = RB_Warpball_GetDriverTarget(tw, inst);
		tw->driverTarget = leader;
		if (leader != previous)
		{
			tw->flags &= ~(TRACKER_FLAG_WARPBALL_TARGET_PATH |
			    TRACKER_FLAG_WARPBALL_MASK_REPATH);
			tw->flags |= TRACKER_FLAG_WARPBALL_FALLBACK_PATH;
			if (leader != NULL && tw->ptrNodeCurr != NULL)
				RB_Warpball_SetTargetDriver(tw);
			Platform_LogWarn("[CNK TORNADO QA] RETARGET old=%d new=%d firstRank=%d\n",
			    previous != NULL ? previous->driverID : -1,
			    leader != NULL ? leader->driverID : -1,
			    leader != NULL ? leader->driverRank : -1);
		}
	}
#endif

	CTR_WriteU16LE(&tw->savedPosXY, (u16)inst->matrix.t[0]);
	CTR_WriteU16LE((u8 *)&tw->savedPosXY + 2, (u16)inst->matrix.t[1]);
	tw->savedPosZ = (s16)inst->matrix.t[2];

#if defined(CTR_NATIVE)
	if (Platform_GetLegacy30HzTicks() > 0)
#endif
	{
		if ((int)inst->animFrame + 1 < INSTANCE_GetNumAnimFrames(inst, 0))
		{
			inst->animFrame++;
		}
		else
		{
			inst->animFrame = 0;
		}
	}

	if (tw->driverTarget != NULL)
	{
		if ((tw->driverTarget->kartState == KS_MASK_GRABBED) && ((tw->flags & TRACKER_FLAG_WARPBALL_TARGET_PATH) != 0))
		{
			struct CheckpointNode *nodes = gGT->level1->ptr_restart_points;

			tw->flags = (tw->flags & ~TRACKER_FLAG_WARPBALL_TARGET_PATH) | TRACKER_FLAG_WARPBALL_FALLBACK_PATH | TRACKER_FLAG_WARPBALL_MASK_REPATH;
			tw->ptrNodeCurr = &nodes[tw->nodeNextIndex];
			tw->ptrNodeNext = RB_Warpball_NewPathNode(tw->ptrNodeCurr, tw->driverTarget);
			tw->driverTarget = RB_Warpball_GetDriverTarget(tw, inst);
			RB_Warpball_SetTargetDriver(tw);
		}

		if ((tw->flags & TRACKER_FLAG_WARPBALL_TARGET_REFRESH_BLOCKED) == 0)
		{
			tw->driverTarget = RB_Warpball_GetDriverTarget(tw, inst);

			if (tw->driverTarget != NULL)
			{
				RB_Warpball_SetTargetDriver(tw);
			}
		}
	}

	target = tw->driverTarget;
	tw->flags &= ~TRACKER_FLAG_WARPBALL_BACKTRACKING;
	elapsedTime = gGT->elapsedTimeMS;
#if defined(CTR_NATIVE)
	/* Native Tornado: +15% normal, 3x with 10 Wumpa; preserve sub-ms
	 * fractions across frames at 120/200/Unlimited Hz. Warp Orb untouched. */
	if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
		elapsedTime = RB_Warpball_TornadoScaledMS(tw, elapsedTime);
#endif

	if (target != NULL)
	{
		distX = (target->posCurr.x >> 8) - inst->matrix.t[0];
		distZ = (target->posCurr.z >> 8) - inst->matrix.t[2];
		distY = (target->posCurr.y >> 8) - inst->matrix.t[1];
		distXZ = (distX * distX) + (distZ * distZ);
		tw->distanceToTarget = distXZ;
		target->thTrackingMe = RB_GetThread_ClosestTracker(target);

		if ((tw->flags & TRACKER_FLAG_WARPBALL_PATH_MODE) != 0)
		{
			s16 rotSpeed = 0x100;

			if ((tw->flags & TRACKER_FLAG_WARPBALL_TARGET_PATH) == 0)
			{
				struct CheckpointNode *pathNode = tw->ptrNodeCurr;

				distX = pathNode->pos.x - inst->matrix.t[0];
				distZ = pathNode->pos.z - inst->matrix.t[2];
				distY = pathNode->pos.y - inst->matrix.t[1];
				distXZ = (distX * distX) + (distZ * distZ);

				if (distXZ < 0x4000)
				{
					tw->flags = (tw->flags & ~TRACKER_FLAG_WARPBALL_FALLBACK_PATH) | TRACKER_FLAG_WARPBALL_TURN_AROUND;
				}
				else if (distXZ < 0x24000)
				{
					rotSpeed = 0x400;
				}
			}

			if (distXZ < 0x90000)
			{
				rotSpeed = 0x400 - (distXZ >> 9);

				if (rotSpeed < 0x100)
				{
					rotSpeed = 0x100;
				}
			}

			if (tw->parentSafetyFrames > 0)
			{
				rotSpeed = 0x40;
			}

			tw->dir.x = 0;
			tw->dir.y = RB_Hazard_InterpolateValue(tw->dir.y, ratan2(distX, distZ), rotSpeed);
			tw->dir.z = 0;
			tw->vel.x = (MATH_Sin(tw->dir.y) * 7) >> 8;
			tw->vel.z = (MATH_Cos(tw->dir.y) * 7) >> 8;

			if (distY > 0)
			{
				tw->vel.y += (elapsedTime << 2) >> 5;

				if (distY < tw->vel.y)
				{
					tw->vel.y = distY;
				}

				if (tw->vel.y > 0x60)
				{
					tw->vel.y = 0x60;
				}
			}
			else if (distY < 0)
			{
				tw->vel.y -= (elapsedTime << 2) >> 5;

				if (tw->vel.y < distY)
				{
					tw->vel.y = distY;
				}

				if (tw->vel.y < -0x60)
				{
					tw->vel.y = -0x60;
				}
			}

			RB_Warpball_AdvanceStraight(tw, inst, elapsedTime);
		}
		else
		{
			struct CheckpointNode *curr = tw->ptrNodeCurr;
			struct CheckpointNode *next = tw->ptrNodeNext;
			int segmentLength = RB_Warpball_NodeDeltaLength(curr, next, &distX, &distY, &distZ);
			int progress = tw->pathProgress + ((elapsedTime * 0x70) >> 5);
			int fraction;

			if (segmentLength <= progress)
			{
				progress -= segmentLength;

				do
				{
					int keepAdvancing;

					curr = next;
					next = RB_Warpball_NewPathNode(curr, tw->driverTarget);
					segmentLength = RB_Warpball_NodeDeltaLength(curr, next, &distX, &distY, &distZ);
					keepAdvancing = segmentLength <= progress;
					progress -= segmentLength;

					if (!keepAdvancing)
					{
						break;
					}
				} while (1);

				progress += segmentLength;
			}

			tw->pathProgress = progress;
			tw->ptrNodeCurr = curr;
			tw->ptrNodeNext = next;

			if (segmentLength == 0)
			{
				fraction = 0;
			}
			else
			{
				fraction = (progress << 12) / segmentLength;
			}

			inst->matrix.t[0] = curr->pos.x + ((distX * fraction) >> 12);
			inst->matrix.t[1] = curr->pos.y + ((distY * fraction) >> 12);
			inst->matrix.t[2] = curr->pos.z + ((distZ * fraction) >> 12);

			tw->dir.y = ratan2(distX, distZ);
			tw->vel.x = (MATH_Sin(tw->dir.y) * 7) >> 8;
			tw->vel.z = (MATH_Cos(tw->dir.y) * 7) >> 8;
			tw->vel.y = 0;
		}
	}
	else
	{
		RB_Warpball_AdvanceStraight(tw, inst, elapsedTime);
	}

#if defined(CTR_NATIVE)
	if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
	{
		int age;
		int grow;
		RB_Warpball_TornadoAvoidBystanders(tw, inst, gGT->elapsedTimeMS);
		age = tw->framesSeekTargetTnt + gGT->elapsedTimeMS;
		if (age > 1000000) age = 1000000;
		tw->framesSeekTargetTnt = age;
		grow = age < 333 ? (age * 0x1000) / 333 : 0x1000;
		if (grow < 0x100) grow = 0x100;
		RB_Warpball_TornadoVisualTransform(inst, age, grow);
	}
	else
#endif
		PlaySound3D_Flags(&tw->soundIDCount, 0x4e, inst);

	posTop.x = (s16)inst->matrix.t[0];
	posTop.y = (s16)(inst->matrix.t[1] - 0x80);
	posTop.z = (s16)inst->matrix.t[2];
	posBottom.x = (s16)inst->matrix.t[0];
	posBottom.y = (s16)(inst->matrix.t[1] + 0x80);
	posBottom.z = (s16)inst->matrix.t[2];

	sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);
	sps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_TRIGGER;
	sps->Union.QuadBlockColl.quadFlagsIgnored = 0;
	sps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_TEST_INSTANCES | COLL_SEARCH_FORCE_INSTANCE_HIT;

	if (gGT->numPlyrCurrGame < 3)
	{
		sps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_TEST_INSTANCES | COLL_SEARCH_HIGH_LOD | COLL_SEARCH_FORCE_INSTANCE_HIT;
	}

	sps->ptr_mesh_info = gGT->level1->ptr_mesh_info;
	COLL_SearchBSP_CallbackQUADBLK(&posTop, &posBottom, sps, 0);
	RB_MakeInstanceReflective(sps, inst);

	if ((sps->collision.stepFlags & COLL_STEP_TRIGGER_WEAPON_REACT) != 0)
	{
		RB_Warpball_TurnAround(t);
	}

	if (sps->boolDidTouchHitbox != 0)
	{
		sps->Input1.modelID = DYNAMIC_WARPBALL;

		if (RB_Hazard_CollLevInst(sps, t) == 1)
		{
			RB_Warpball_TurnAround(t);
		}
	}

	if (sps->boolDidTouchQuadblock != 0)
	{
		tw->flags |= TRACKER_FLAG_WARPBALL_TURN_AROUND;
		RB_Warpball_SetQuadblockIndex(tw, sps);
		tw->vel.y = 0;

		if (((tw->flags & TRACKER_FLAG_WARPBALL_PATH_MODE) != 0) && (inst->matrix.t[1] < sps->hit.hitPos.y))
		{
			inst->matrix.t[1] = sps->hit.hitPos.y;
			inst->depthBiasNormal = sps->hit.ptrQuadblock->draw_order_low - 1;
		}
	}
	else
	{
		posTop.x = (s16)inst->matrix.t[0];
		posTop.y = (s16)(inst->matrix.t[1] - 0x900);
		posTop.z = (s16)inst->matrix.t[2];
		COLL_SearchBSP_CallbackQUADBLK(&posTop, &posBottom, sps, 0);

		if (sps->boolDidTouchQuadblock != 0)
		{
			tw->flags |= TRACKER_FLAG_WARPBALL_TURN_AROUND;
			RB_Warpball_SetQuadblockIndex(tw, sps);
		}

		if ((sps->boolDidTouchQuadblock == 0) && (((tw->flags & TRACKER_FLAG_WARPBALL_PATH_MODE) != 0) || (tw->driverTarget == NULL)))
		{
			RB_Warpball_TurnAround(t);
		}
	}

	if ((s_warpballParticleHeight != 0) && (tw->ptrParticle != NULL))
	{
		struct Particle *p = tw->ptrParticle;

		p->axis[0].startVal = inst->matrix.t[0] << 8;
		p->axis[1].startVal = (inst->matrix.t[1] + s_warpballParticleHeight) << 8;
		p->axis[2].startVal = inst->matrix.t[2] << 8;
		p->axis[3].startVal = s_warpballParticleHeight << 8;
		p->axis[4].startVal = s_warpballParticleHeight * 0xc0;
		p->axis[5].startVal = s_warpballParticleHeight << 7;
		p->otIndexOffset = inst->depthBiasNormal + 1;
		p->framesLeftInLife = -1;
	}

#if defined(CTR_NATIVE)
	if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
		hitInst = RB_Warpball_TornadoCollideLeader(
		    tw, inst, tornadoStartX, tornadoStartY, tornadoStartZ);
	else
#endif
		hitInst = RB_Hazard_CollideWithDrivers(inst, tw->parentSafetyFrames, 0x9000, tw->instParent);

	if (hitInst != NULL)
	{
		struct Driver *hitDriver = hitInst->thread->object;

#if defined(CTR_NATIVE)
		if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
		{
			/* Current leader ONLY (human or bot). Shields/active
			 * damage must be respected by the existing hit function. */
			int damaged = RB_Hazard_HurtDriver(hitDriver, 1, tw->driverParent, 0);
			if (damaged)
			{
				/* Separate CNK grab/throw stage. The victim rises by
				 * applied force, not by instantly teleporting posCurr. */
				RB_CNKTornado_BeginCapture(hitDriver);
				if ((hitDriver->actionsFlagSet & ACTION_BOT) != 0)
				{
					/* 30Hz AI spin cooldown of roughly 3.0 sec. */
					if (hitDriver->botData.aiPhysics.squishCooldown < 0x560)
						hitDriver->botData.aiPhysics.squishCooldown = 0x560;
				}
				else
					VehPhysProc_CNKTornadoMarkSpin(hitDriver);
			}
			NativeAudio_CNKTornadoPlay(1);
			Platform_LogWarn("[CNK TORNADO QA] HIT leaderDriver=%d rank=%d bot=%d damage=%d juiced=%d\n",
			    hitDriver->driverID, hitDriver->driverRank,
			    (hitDriver->actionsFlagSet & ACTION_BOT) != 0, damaged,
			    (tw->flags & TRACKER_FLAG_POWERED_UP) != 0);
			if (tw->driverParent != NULL)
				tw->driverParent->instBombThrow = NULL;
			if (damaged)
			{
				/* Keep effect alive around lifted kart until it is dropped. */
				tw->framesSeekTargetTnt = 0;
				ThTick_SetAndExec(t, &RB_Warpball_TornadoCarryTick);
			}
			else
				RB_Warpball_Death(t);
			return;
		}
#endif
		if (hitDriver != tw->driverParent)
		{
			TrackerWeaponFlags hadTargetPathFlag = tw->flags & TRACKER_FLAG_WARPBALL_TARGET_PATH;
			TrackerWeaponFlags flagsBeforeHit;

			RB_Hazard_HurtDriver(hitDriver, 2, tw->driverParent, 0);
#if defined(CTR_NATIVE)
			if ((tw->flags & TRACKER_FLAG_CNK_TORNADO) != 0)
				NativeAudio_CNKTornadoPlay(1);
#endif
			hitDriver->damageColorTimer = 0x1e;

			flagsBeforeHit = tw->flags | TRACKER_FLAG_WARPBALL_HIT_DRIVER;
			tw->flags = flagsBeforeHit;

			if ((((flagsBeforeHit & TRACKER_FLAG_POWERED_UP) == 0) && (tw->driverTarget == hitDriver)) || (hitDriver->driverRank == 0))
			{
				tw->driverParent->instBombThrow = NULL;
				RB_Warpball_Death(t);
				return;
			}

			tw->driversHit |= 1u << (hitDriver->driverID & 0x1f);

			for (int rank = hitDriver->driverRank; rank < 8; rank++)
			{
				struct Driver *rankDriver = gGT->driversInRaceOrder[rank];

				if (rankDriver != NULL)
				{
					tw->driversHit |= 1u << (rankDriver->driverID & 0x1f);
				}
			}

			if (tw->driverTarget == hitDriver)
			{
				tw->flags &= ~TRACKER_FLAG_WARPBALL_TARGET_PATH;
				tw->driverTarget = RB_Warpball_GetDriverTarget(tw, inst);

				if (tw->driverTarget == NULL)
				{
					tw->driverParent->instBombThrow = NULL;
					RB_Warpball_Death(t);
				}
				else
				{
					RB_Warpball_SetTargetDriver(tw);
				}

				if (hadTargetPathFlag != 0)
				{
					RB_Warpball_SeekDriver(tw, hitDriver->checkpoint.currentIndex, hitDriver);
				}

				if (((tw->flags & TRACKER_FLAG_WARPBALL_TARGET_PATH) == 0) && (hadTargetPathFlag != 0))
				{
					if (tw->nodeCurrIndex != 0xff)
					{
						struct CheckpointNode *nodes = gGT->level1->ptr_restart_points;

						tw->ptrNodeCurr = &nodes[tw->nodeCurrIndex];
						tw->ptrNodeNext = RB_Warpball_NewPathNode(tw->ptrNodeCurr, tw->driverTarget);
					}

					tw->flags |= TRACKER_FLAG_WARPBALL_FALLBACK_PATH;
				}
			}
		}
	}
	else
	{
		hitInst = RB_Hazard_CollideWithBucket(inst, t, gGT->threadBuckets[MINE].thread, tw->parentSafetyFrames, 0x2400, tw->instParent);

		if (hitInst != NULL)
		{
			struct Thread *hitTh = hitInst->thread;

			((ThreadSimpleCollideFunc)hitTh->funcThCollide)(hitTh);
		}
	}

	if (tw->parentSafetyFrames != 0)
	{
		tw->parentSafetyFrames--;
	}
}
