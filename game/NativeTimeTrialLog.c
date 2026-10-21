#include <common.h>

#if defined(CTR_NATIVE)

#include <CharacterRegistry.h>
#include <NativeSession.h>
#include <platform/native_assets.h>
#include <platform/native_custom_tracks.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Online time-trial leaderboard, game side (cfr.time-trial/2).
 *
 * The game only appends one JSON line per finished Time Trial to
 * <exe dir>/forge_time_trials.jsonl. It never talks to the network and never
 * decides eligibility: the launcher reads this file, adds its CTR-U disc
 * verdict and account, and the server accepts or rejects the run.
 * Writing is best effort; a read-only folder must never affect the race.
 *
 * Racers: registry IDs >= 16 depend on which mods each PC has installed, so
 * the line carries the stable slug, display name, retail base character and
 * engine class; character_id is kept only for local diagnostics.
 */

#define NATIVE_TIME_TRIAL_LOG_NAME "forge_time_trials.jsonl"

static void NativeTimeTrialLog_WriteJsonString(FILE *f, const char *s)
{
	fputc('"', f);

	for (; s != NULL && *s != 0; s++)
	{
		unsigned char c = (unsigned char)*s;

		/* Bytes >= 0x80 pass through: racer names are UTF-8 and JSON allows it. */
		if (c == '"' || c == '\\')
			fprintf(f, "\\%c", c);
		else if (c < 0x20 || c == 0x7f)
			fprintf(f, "\\u%04x", c);
		else
			fputc(c, f);
	}

	fputc('"', f);
}

void NativeTimeTrialLog_Record(struct GameTracker *gGT, struct Driver *player)
{
	char path[1024];
	char finished[32];
	const char *customTrackId = NULL;
	const struct CharacterDef *racer;
	int characterID;
	Uint64 counter;
	time_t now;
	struct tm *utc;
	FILE *f;
	int laps;
	int qaBuild = 0;
	int i;

	if (gGT == NULL || player == NULL)
		return;

	if ((gGT->gameMode1 & TIME_TRIAL) == 0 || (gGT->gameMode1 & RELIC_RACE) != 0)
		return;

	/* Custom tracks reuse a retail levelID (Crash Cove shell or their original
	 * level), so levelID alone would file a community track under a retail
	 * one. Keep its stable ID instead; same signal GhostReplay uses. */
	if (NativeCustomTracks_IsSelected() || NativeCustomTracks_IsActiveRace())
	{
		customTrackId = NativeCustomTracks_IdAt(NativeCustomTracks_SelectedIndex());
		if (customTrackId == NULL || customTrackId[0] == 0)
			customTrackId = "unknown";
	}

#if defined(CTR_PRACTICE_FASTSTART) || defined(CTR_CNK_PHYSICS_PREVIEW_QA) || defined(CTR_CSEQ_TEST_BUILD)
	qaBuild = 1;
#endif

	if (snprintf(path, sizeof(path), "%s/%s", NativeAssets_GetBaseDir(),
	             NATIVE_TIME_TRIAL_LOG_NAME) >= (int)sizeof(path))
		return;

	now = time(NULL);
	utc = gmtime(&now);
	if (utc == NULL ||
	    strftime(finished, sizeof(finished), "%Y-%m-%dT%H:%M:%SZ", utc) == 0)
		snprintf(finished, sizeof(finished), "1970-01-01T00:00:00Z");

	f = fopen(path, "ab");
	if (f == NULL)
	{
		SDL_Log("[CFR TimeTrial] could not open %s", NATIVE_TIME_TRIAL_LOG_NAME);
		return;
	}

	/* run_id only deduplicates uploads; it is not a security token. */
	counter = SDL_GetPerformanceCounter();
	fprintf(f, "{\"schema\":\"cfr.time-trial/2\",\"run_id\":\"%08x%08x%08x%08x\"",
	        (unsigned int)now, (unsigned int)counter, (unsigned int)(counter >> 32),
	        (unsigned int)player->timeElapsedInRace ^ ((unsigned int)gGT->levelID << 24));
	fprintf(f, ",\"finished_utc\":\"%s\",\"game_version\":", finished);
	NativeTimeTrialLog_WriteJsonString(f, CTR_NATIVE_VERSION);
	fprintf(f, ",\"build_id\":");
	NativeTimeTrialLog_WriteJsonString(f, CTR_NATIVE_BUILD_ID);
	fprintf(f, ",\"level_id\":%d,\"custom_track\":", gGT->levelID);
	if (customTrackId != NULL)
		NativeTimeTrialLog_WriteJsonString(f, customTrackId);
	else
		fprintf(f, "null");

	laps = gGT->numLaps;
	if (laps < 0)
		laps = 0;
	if (laps > 7)
		laps = 7;

	fprintf(f, ",\"time_units\":%d,\"laps\":%d,\"lap_units\":[",
	        player->timeElapsedInRace, laps);
	for (i = 0; i < laps; i++)
		fprintf(f, "%s%d", i ? "," : "", gGT->lapTime[i]);

	characterID = data.characterIDs[(u8)player->driverID];
	racer = CharacterRegistry_GetByID(characterID);
	fprintf(f, "],\"character_id\":%d,\"character_key\":", characterID);
	NativeTimeTrialLog_WriteJsonString(f, racer != NULL && racer->slug[0] ? racer->slug : "unknown");
	fprintf(f, ",\"character_name\":");
	NativeTimeTrialLog_WriteJsonString(
		f, racer != NULL && racer->displayName[0] ? racer->displayName : "UNKNOWN");
	fprintf(f, ",\"character_base\":%d,\"character_class\":%d,\"character_mod\":%d",
	        racer != NULL ? racer->fallbackRetailID : characterID,
	        racer != NULL ? racer->engineClass : BALANCED,
	        racer != NULL && (racer->flags & CHARACTER_REGISTRY_FLAG_RETAIL) == 0 ? 1 : 0);
	fprintf(f, ",\"cheats\":%u,\"no_mods\":%d,\"qa_build\":%d}\n",
	        (unsigned int)(gGT->gameMode2 & CHEAT_ALL),
	        NativeSession_NoModsActive() ? 1 : 0, qaBuild);
	fclose(f);

	SDL_Log("[CFR TimeTrial] level %d finished in %d units (custom=%s)",
	        gGT->levelID, player->timeElapsedInRace,
	        customTrackId != NULL ? customTrackId : "no");
}

#endif
