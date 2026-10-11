#include <common.h>
#include <NativeHowlSampleCatalog.h>
#include <NativeCustomSampleRegistry.h>
#if defined(CTR_CSEQ_LIBERATION)
#include <NativeMusicRegistry.h>
#endif
#include <stdint.h>

#if defined(CTR_CSEQ_TEST_BUILD)
static int s_cseqTestPitchLogCount;
#endif

/*
 * Native PC music can reference any retail HOWL sample without requiring its
 * original level bank to be resident. The tagged address is consumed only by
 * native_audio.c; PS1/retail builds keep using the original SPU address table.
 */
static void *howl_GetMusicSampleStartAddr(int spuIndex)
{
#if defined(CTR_NATIVE) && defined(CTR_CSEQ_LIBERATION)
	const unsigned char *blob = NULL;
	int blobSize = 0;
	unsigned int handle;

	/* Negative spuIndex values are reserved by Crash Forge for package-local
	 * native PCM samples: -1 = sample_000.wav, -2 = sample_001.wav, etc. */
	if (spuIndex < 0)
	{
		struct NativeCustomSampleView view;
		int slot = -1 - spuIndex;

		if (NativeCustomSampleRegistry_GetView(slot, &view))
		{
			handle = NativeCustomSampleRegistry_EncodeHandle(slot);
			if (handle != 0)
				return (void *)(uintptr_t)handle;
		}

		return NULL;
	}

	if (NativeHowlSampleCatalog_GetBlob(spuIndex, &blob, &blobSize) &&
	    blob != NULL &&
	    blobSize > 0)
	{
		handle = NativeHowlSampleCatalog_EncodeRetailHandle(spuIndex);
		if (handle != 0)
		{
			return (void *)(uintptr_t)handle;
		}
	}
#endif

	if (spuIndex < 0)
		return NULL;

	return (void *)(uintptr_t)
		((u32)sdata->howl_spuAddrs[spuIndex].spuAddr << 3);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029e18-0x80029f1c
void cseq_opcode01_noteoff(struct SongSeq *seq)
{
	struct ChannelStats *curr, *backupNext;
	u8 *currNote = seq->currNote;
	int soundID = seq->soundID;
	u32 *flagPtr;

	for (curr = (struct ChannelStats *)sdata->channelTaken.first; curr != NULL; curr = backupNext)
	{
		backupNext = curr->next;

		if (curr->type != HOWL_CHANNEL_TYPE_MUSIC)
		{
			continue;
		}

		// not the sound needed to turn off
		if (curr->soundID != soundID)
		{
			continue;
		}

		// need a struct for Note offset 0x1
		if (curr->drumIndex_pitchIndex != currNote[1])
		{
			continue;
		}

		// enable OFF flag, disable ON flag
		flagPtr = &sdata->ChannelUpdateFlags[curr->channelID];
		*flagPtr |= HOWL_CHANNEL_UPDATE_OFF;
		*flagPtr &= ~HOWL_CHANNEL_UPDATE_KEY_ON;

		curr->flags &= (u8)~1;

		// recycle: remove from taken, put on free
		LIST_RemoveMember(&sdata->channelTaken, (struct Item *)curr);
		LIST_AddBack(&sdata->channelFree, (struct Item *)curr);
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029f1c-0x80029f24
void cseq_opcode02_empty(struct SongSeq *seq)
{
#if defined(CTR_NATIVE) && defined(CTR_CSEQ_LIBERATION)
	/*
	 * Crash Forge external CSEQ extension:
	 * opcode 02 + 0xCF marks the beginning of the repeating section.
	 * Everything before it is a one-shot race intro. Retail CSEQ keeps the
	 * original no-op behavior because the extension is only honored while an
	 * external package is active.
	 */
	if (seq != NULL &&
	    seq->currNote != NULL &&
	    seq->currNote[1] == 0xCF &&
	    NativeMusicRegistry_IsExternalCseqActive())
	{
		seq->firstNote = seq->currNote + 2;
		return;
	}
#else
	(void)seq;
#endif
	// left empty by ND for retail CSEQ
}

// "end of song" opcode
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029f24-0x80029f78
void cseq_opcode03(struct SongSeq *seq)
{
	// if song does not loop
	if ((seq->flags & 2) == 0)
	{
		SongPool_StopAllCseq(&sdata->songPool[seq->songPoolIndex]);
	}

	// if song loops
	else
	{
		// start over
		seq->flags |= 8;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029f78-0x80029f80
void cseq_opcode04_empty(struct SongSeq *seq)
{
	(void)seq;
	// left empty by ND
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80029f80-0x8002a170
void howl_InitChannelAttr_Music(struct SongSeq *seq, struct ChannelAttr *attr, int index, int channelVol)
{
	int pitch;
	s32 sampleVol;
	int songIndex = seq->songPoolIndex;

	sampleVol = CTR_MipsSra(CTR_MipsMulLo(CTR_MipsMulLo(sdata->vol_Music, sdata->songPool[songIndex].vol_Curr), seq->vol_Curr), 10);

	// instrument
	if ((seq->flags & 4) == 0)
	{
		struct SampleInstrument *longSample = &sdata->ptrCseqLongSamples[seq->instrumentID];

		pitch = howl_InstrumentPitch(longSample->basePitch, index, seq->distort);

#if defined(CTR_CSEQ_TEST_BUILD)
		if (NativeMusicRegistry_IsExternalCseqActive() &&
		    s_cseqTestPitchLogCount < 64)
		{
			Platform_Log(
				"[CSEQ PITCH] note=%d inst=%d spu=%d base=%d distort=0x%02x pitch=0x%04x vol=%d\n",
				index,
				seq->instrumentID,
				longSample->spuIndex,
				longSample->basePitch,
				seq->distort,
				pitch & 0xffff,
				longSample->volume);
			s_cseqTestPitchLogCount++;
		}
#endif

		attr->spuStartAddr = howl_GetMusicSampleStartAddr(longSample->spuIndex);

		// audio ADSR
		attr->ad = longSample->ad;
		attr->sr = longSample->sr;

		sampleVol = CTR_MipsMulLo(sampleVol, longSample->volume);
	}

	// drums
	else
	{
		struct SampleDrums *shortSample = &sdata->ptrCseqShortSamples[index];

		if (seq->distort == HOWL_SFX_DISTORTION_NONE)
		{
			pitch = shortSample->pitch;
		}

		else
		{
			pitch = CTR_MipsSrl(CTR_MipsMulLo((u16)shortSample->pitch, data.distortConst_OtherFX[seq->distort]), 16);
		}

		attr->spuStartAddr = howl_GetMusicSampleStartAddr(shortSample->spuIndex);

		// audio ADSR
		attr->ad = 0x80ff;
		attr->sr = 0x1fc2;

		sampleVol = CTR_MipsMulLo(sampleVol, shortSample->volume);
	}

	Channel_SetVolume(attr, CTR_MipsSrl(CTR_MipsMulLo(sampleVol, channelVol), 15), seq->LR);

	attr->pitch = pitch;
	attr->reverb = seq->reverb;
}

// change volume
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a170-0x8002a28c
void cseq_opcode_from06and07(struct SongSeq *seq)
{
	struct ChannelStats *curr, *backupNext;
	int soundID = seq->soundID;
	int songIndex = seq->songPoolIndex;

	int sampleVol = CTR_MipsMulLo(CTR_MipsMulLo(sdata->vol_Music, sdata->songPool[songIndex].vol_Curr), seq->vol_Curr);

	for (curr = (struct ChannelStats *)sdata->channelTaken.first; curr != NULL; curr = backupNext)
	{
		backupNext = curr->next;

		if (curr->type != HOWL_CHANNEL_TYPE_MUSIC)
		{
			continue;
		}

		// not the sound needed to turn off
		if (curr->soundID != soundID)
		{
			continue;
		}

		Channel_SetVolume(&sdata->channelAttrNew[curr->channelID], CTR_MipsSra(CTR_MipsMulLo(sampleVol, curr->vol), 18), seq->LR);

		// update volume
		sdata->ChannelUpdateFlags[curr->channelID] |= HOWL_CHANNEL_UPDATE_VOLUME;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a28c-0x8002a3a8
void cseq_opcode05_noteon(struct SongSeq *seq)
{
	u8 *currNote;
	int songIndex = seq->songPoolIndex;
	struct ChannelStats *stats;
	struct ChannelAttr attr;

	if (sdata->vol_Music == 0)
	{
		return;
	}
	if (sdata->songPool[songIndex].vol_Curr == 0)
	{
		return;
	}
	if (seq->vol_Curr == 0)
	{
		return;
	}

	currNote = seq->currNote;

	howl_InitChannelAttr_Music(seq, &attr, currNote[1], currNote[2]);

	stats = Channel_AllocSlot(HOWL_CHANNEL_UPDATE_ALL_ATTRS, &attr);

	if (stats == 0)
	{
		return;
	}

	stats->flags |= 0xe;

	stats->type = HOWL_CHANNEL_TYPE_MUSIC;
	stats->unk2 = 0;

	// dang, what?
	stats->unk1 = seq->unk;

	// echo and reverb is same thing, needs rename
	stats->echo = seq->reverb;

	stats->vol = currNote[2];

	stats->distort = seq->distort;
	stats->LR = seq->LR;
	stats->timeLeft = 0;
	stats->drumIndex_pitchIndex = currNote[1];
	stats->soundID = seq->soundID;

	seq->unk0A = (u8)CTR_MipsAddLo(seq->unk0A, 1);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a3a8-0x8002a3d4
void cseq_opcode06(struct SongSeq *seq)
{
	u8 *note = seq->currNote;
	seq->vol_Curr = note[1];
	cseq_opcode_from06and07(seq);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a3d4-0x8002a400
void cseq_opcode07(struct SongSeq *seq)
{
	u8 *note = seq->currNote;
	seq->LR = note[1];
	cseq_opcode_from06and07(seq);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a400-0x8002a494
void cseq_opcode08(struct SongSeq *seq)
{
	struct ChannelStats *curr, *backupNext;
	u8 *currNote = seq->currNote;
	int soundID = seq->soundID;

	for (curr = (struct ChannelStats *)sdata->channelTaken.first; curr != NULL; curr = backupNext)
	{
		backupNext = curr->next;

		if (curr->type != HOWL_CHANNEL_TYPE_MUSIC)
		{
			continue;
		}

		// not the sound needed to turn off
		if (curr->soundID != soundID)
		{
			continue;
		}

		// set reverb
		sdata->channelAttrNew[curr->channelID].reverb = currNote[1];

		// update Reverb (reverberation = echo)
		sdata->ChannelUpdateFlags[curr->channelID] |= HOWL_CHANNEL_UPDATE_REVERB;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a494-0x8002a4a8
void cseq_opcode09(struct SongSeq *seq)
{
	u8 *currNote = seq->currNote;
	seq->instrumentID = currNote[1];
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a4a8-0x8002a63c
void cseq_opcode0a(struct SongSeq *seq)
{
	int pitch;
	struct ChannelStats *curr, *backupNext;
	int soundID = seq->soundID;

	u8 *currNote = seq->currNote;
	seq->distort = currNote[1];

	for (curr = (struct ChannelStats *)sdata->channelTaken.first; curr != NULL; curr = backupNext)
	{
		backupNext = curr->next;

		if (curr->type != HOWL_CHANNEL_TYPE_MUSIC)
		{
			continue;
		}

		// not the sound needed to turn off
		if (curr->soundID != soundID)
		{
			continue;
		}

		int index = curr->drumIndex_pitchIndex;

		// instrument
		if ((seq->flags & 4) == 0)
		{
			struct SampleInstrument *longSample = &sdata->ptrCseqLongSamples[seq->instrumentID];

			pitch = howl_InstrumentPitch(longSample->basePitch, index, seq->distort);
		}

		// drums
		else
		{
			struct SampleDrums *shortSample = &sdata->ptrCseqShortSamples[index];

			if (seq->distort == HOWL_SFX_DISTORTION_NONE)
			{
				pitch = shortSample->pitch;
			}

			else
			{
				pitch = CTR_MipsSrl(CTR_MipsMulLo((u16)shortSample->pitch, data.distortConst_OtherFX[seq->distort]), 16);
			}
		}

		// save pitch
		sdata->channelAttrNew[curr->channelID].pitch = pitch;

		// update pitch
		sdata->ChannelUpdateFlags[curr->channelID] |= HOWL_CHANNEL_UPDATE_PITCH;
	}
}
