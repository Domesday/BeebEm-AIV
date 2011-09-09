
/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 1994  David Alan Gilbert
Copyright (C) 1997  Mike Wyatt
Copyright (C) 2001  Richard Gellman

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public 
License along with this program; if not, write to the Free 
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA  02110-1301, USA.
****************************************************************/
/* Sound emulation for the beeb - David Alan Gilbert 26/11/94 */

#ifndef SOUND_HEADER
#define SOUND_HEADER

#ifdef WIN32
/* Always compile sound code - it is switched on and off using SoundEnabled */
#define SOUNDSUPPORT
#include <windows.h>
#endif

#define MUTED 0
#define UNMUTED 1

#include <stdio.h>

extern int SoundDefault; // Default sound state
extern int SoundEnabled;    /* Sound on/off flag */
extern int DirectSoundEnabled;  /* DirectSound enabled for Win32 */
extern int RelaySoundEnabled; // Relay Click noise enable
extern int SoundSampleRate; /* Sample rate, 11025, 22050 or 44100 Hz */
extern int SoundVolume;     /* Volume, 1(full),2,3 or 4(low) */

extern __int64 SoundTrigger; /* Cycle based trigger on sound */
extern double SoundTuning;
extern __int64 SoundCycles;

void SoundInit();
void SoundReset();

/* Called in sysvia.cc when a write to one of the 76489's registers occurs */
void Sound_RegWrite(int Value);
void DumpSound(void);
void SoundTrigger_Real(void);
void ClickRelay(unsigned char RState);

void Sound_Trigger(int NCycles);

extern volatile BOOL bDoSound;
extern void AdjustSoundCycles(void);

void SetSound(char State);

struct AudioType {
	char Signal; // Signal type: data, gap, or tone.
	char BytePos; // Position in data byte
	bool Enabled; // Enable state of audio deooder
	int Data; // The actual data itself
	int Samples; // Samples counted in current pattern till changepoint
	char CurrentBit; // Current bit in data being processed
	char ByteCount; // Byte repeat counter
};

extern struct AudioType TapeAudio;
extern bool TapeSoundEnabled;
void SoundChipReset(void);
void SwitchOnSound(void);
extern int UseHostClock;
extern int UsePrimaryBuffer;
void LoadSoundUEF(FILE *SUEF);
void SaveSoundUEF(FILE *SUEF);
extern int PartSamples;
extern int SBSize;
void MuteSound(void);
#endif
