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

// LV-ROM Player emulation
//
// This deals with the processing of video and audio data for the
// main systems to merge with later, and also deals with the
// emulation of the LV-ROM "F-Codes", which control player actions.
// [ Richard Gellman ]

#define LV_DISC_CNT 4

#define MAX_LV_FRAME_NUM 54000

extern char FCode_Buf[256];
extern char FCode_Reply_Buf[256];

extern unsigned char LV_Audio[44100];
extern int LVA_Ptr;

extern unsigned int CFrame;
extern unsigned int VFrame;
extern unsigned char lvScaleBuff[LVSCALE_IM_W * LVSCALE_IM_H * LVRAW_BWIDTH];
extern int LVE;

extern char lvDiscName[][80];
extern char lvDiscLdiFile[][80];
extern char lvDiscImgDir[][80];
extern char lvDiscUsercode[][80];

extern unsigned int currLvDisc;

void LVROM_PutSerial(unsigned char data);
void Do_FCode_Command(void);
void LVROM_Poll(int NCycles);
unsigned char GetAudioByte(void);
void AckLVReply(void);
void NextVideoPoll(void);
void LVROM_Reset(void);

