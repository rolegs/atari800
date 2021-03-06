/*
 * sound.c - high-level sound routines for the Atari Falcon port
 *
 * Copyright (C) 1995-1998 David Firth
 * Copyright (C) 1998-2017 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#ifdef SOUND

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>

#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "sound.h"

static char* pPhysical;
static char* pLogical;
static char* pBuffer;
static size_t bufferSize;
static int isLogicalBufferActive;
static SndBufPtr soundBufferPtr;
static size_t soundBufferWritten;
static int audioSaved;
static unsigned short saveAudioBuffer[9];
#define HW_REG8(x) *((volatile unsigned char*)(x))
#define HW_REG16(x) *((volatile unsigned short*)(x))

static void saveAudio(void)
{
	unsigned char* pBuffer8;
	unsigned short* pBuffer16;

	if (!audioSaved) {
		pBuffer16 = saveAudioBuffer;
		*pBuffer16++ = HW_REG16(0xffff8930);
		*pBuffer16++ = HW_REG16(0xffff8932);

		pBuffer8 = (unsigned char*)pBuffer16;
		*pBuffer8++ = HW_REG8(0xffff8934);
		*pBuffer8++ = HW_REG8(0xffff8935);
		*pBuffer8++ = HW_REG8(0xffff8936);
		*pBuffer8++ = HW_REG8(0xffff8937);
		*pBuffer8++ = HW_REG8(0xffff8938);
		*pBuffer8++ = HW_REG8(0xffff8939);
		/**pBuffer8++ = HW_REG8(0xffff893a);*/
		*pBuffer8++ = HW_REG8(0xffff893c);
		*pBuffer8++ = HW_REG8(0xffff8941);
		*pBuffer8++ = HW_REG8(0xffff8943);
		*pBuffer8++ = HW_REG8(0xffff8900);
		*pBuffer8++ = HW_REG8(0xffff8901);
		*pBuffer8++ = HW_REG8(0xffff8920);
		*pBuffer8++ = HW_REG8(0xffff8921);

		audioSaved = TRUE;
	}
}

static void restoreAudio(void)
{
	unsigned char* pBuffer8;
	unsigned short* pBuffer16;

	if (audioSaved) {
		pBuffer16 = saveAudioBuffer;
		HW_REG16(0xffff8930) = *pBuffer16++;
		HW_REG16(0xffff8932) = *pBuffer16++;

		pBuffer8 = (unsigned char*)pBuffer16;
		HW_REG8(0xffff8934) = *pBuffer8++;
		HW_REG8(0xffff8935) = *pBuffer8++;
		HW_REG8(0xffff8936) = *pBuffer8++;
		HW_REG8(0xffff8937) = *pBuffer8++;
		HW_REG8(0xffff8938) = *pBuffer8++;
		HW_REG8(0xffff8939) = *pBuffer8++;
		/*HW_REG8(0xffff893a) = *pBuffer8++;*/
		HW_REG8(0xffff893c) = *pBuffer8++;
		HW_REG8(0xffff8941) = *pBuffer8++;
		HW_REG8(0xffff8943) = *pBuffer8++;
		HW_REG8(0xffff8900) = *pBuffer8++;
		HW_REG8(0xffff8901) = *pBuffer8++;
		HW_REG8(0xffff8920) = *pBuffer8++;
		HW_REG8(0xffff8921) = *pBuffer8++;

		audioSaved = FALSE;
	}
}

unsigned int PLATFORM_SoundAvailable(void)
{
	if (Buffptr(&soundBufferPtr) == 0) {
		if (!isLogicalBufferActive) {
			/* we play from pPhysical (1st buffer) */
			if (soundBufferPtr.play < pLogical) {
				return bufferSize - soundBufferWritten;
			}
		} else {
			/* we play from pLogical (2nd buffer) */
			if (soundBufferPtr.play >= pLogical) {
				return bufferSize - soundBufferWritten;
			}
		}
	}

	return 0;
}

void PLATFORM_SoundWrite(UBYTE const *buffer, unsigned int size)
{
	int written = FALSE;

	if (Buffptr(&soundBufferPtr) == 0) {
		if (!isLogicalBufferActive) {
			/* we play from pPhysical (1st buffer) */
			if (soundBufferPtr.play < pLogical) {
				memcpy(pLogical + soundBufferWritten, buffer, size);
				written = TRUE;
			}
		} else {
			/* we play from pLogical (2nd buffer) */
			if (soundBufferPtr.play >= pLogical) {
				memcpy(pPhysical + soundBufferWritten, buffer, size);
				written = TRUE;
			}
		}
	}

	if (written) {
		if (soundBufferWritten + size < bufferSize) {
			soundBufferWritten += size;
		} else {
			soundBufferWritten = 0;
			isLogicalBufferActive = !isLogicalBufferActive;
		}
	}
}

int PLATFORM_SoundSetup(Sound_setup_t *setup)
{
	long cookie;
	int mode;
	int diff50, diff33, diff25, diff20, diff16, diff12, diff10, diff8;
	int clk;

	if (Sound_enabled) {
		PLATFORM_SoundExit();
	}

	if (Getcookie(C__SND, &cookie) != C_FOUND
		|| (setup->sample_size == 1 && !(cookie & SND_8BIT))
		|| (setup->sample_size == 2 && !(cookie & SND_16BIT))
#ifndef __mcoldfire__
		|| !(cookie & SND_MATRIX)) {
#else
		/* Firebee's FireTOS doesn't set SND_MATRIX even though Devconnect() works? */
		) {
#endif
		return FALSE;
	}

	diff50 = abs(49170 - setup->freq);
	diff33 = abs(32780 - setup->freq);
	diff25 = abs(24585 - setup->freq);
	diff20 = abs(19668 - setup->freq);
	diff16 = abs(16390 - setup->freq);
	diff12 = abs(12292 - setup->freq);
	diff10 = abs(9834 - setup->freq);
	diff8  = abs(8195 - setup->freq);

	if (diff50 < diff33) {
		setup->freq = 49170;
		clk = CLK50K;
	} else if (diff33 < diff25) {
		setup->freq = 32780;
		clk = CLK33K;
	} else if (diff25 < diff20) {
		setup->freq = 24585;
		clk = CLK25K;
	} else if (diff20 < diff16) {
		setup->freq = 19668;
		clk = CLK20K;
	} else if (diff16 < diff12) {
		setup->freq = 16390;
		clk = CLK16K;
	} else if (diff12 < diff10) {
		setup->freq = 12292;
		clk = CLK12K;
	} else if (diff10 < diff8) {
		setup->freq = 9834;
		clk = CLK10K;
	} else {
		setup->freq = 8195;
		clk = CLK8K;
	}

	if (setup->buffer_frames == 0) {
		setup->buffer_frames = setup->freq / 50;	/* buffer for 1/50th of a second */
	}
	setup->buffer_frames = Sound_NextPow2(setup->buffer_frames);

	if (setup->channels == 2) {
		mode = setup->sample_size == 1 ? MODE_STEREO8 : MODE_STEREO16;
	} else if (setup->sample_size == 1) {
		/* 8-bit MONO */
		mode = MODE_MONO;
	} else {
		/* 16-bit MONO is not available */
		mode = MODE_MONO;
		setup->sample_size = 1;
	}

	/* channels * 8/16 bit * freq in Hz * seconds */
	bufferSize = setup->channels * setup->sample_size * setup->buffer_frames;

	pBuffer = (char*)Mxalloc(2*bufferSize, MX_STRAM);
	if (pBuffer == NULL) {
		return FALSE;
	}
	memset(pBuffer, 0, 2*bufferSize);

	pPhysical = pBuffer;
	pLogical = pBuffer + bufferSize;

	Supexec(saveAudio);

	Sndstatus(SND_RESET);

	if (Devconnect(DMAPLAY, DAC, CLK25M, clk, NO_SHAKE) != 0) {
		/* for some reason, Devconnect() returns error in memory protection mode... */
		/*goto error;*/
	}

	if (Setmode(mode) != 0) {
		goto error;
	}

	Soundcmd(ADDERIN, MATIN);

	if (Setbuffer(SR_PLAY, pBuffer, pBuffer + 2*bufferSize) != 0) {
		goto error;
	}

	return TRUE;

error:
	Supexec(restoreAudio);

	Mfree(pBuffer);
	pBuffer = NULL;

	return FALSE;
}

void PLATFORM_SoundExit(void)
{
	Buffoper(0x00);

	Supexec(restoreAudio);

	if (pBuffer != NULL) {
		Mfree(pBuffer);
		pBuffer = NULL;
	}
}

void PLATFORM_SoundPause(void)
{
	Buffoper(0x00);
}

void PLATFORM_SoundContinue(void)
{
	Buffoper(SB_PLA_ENA | SB_PLA_RPT);
}

#endif /* SOUND */
