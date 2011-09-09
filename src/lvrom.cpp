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
// Code to emulate the Philips VP415 LV-Rom player. (12" video disc system)
// This processes video and audio (? - not fully implemented) data. The binary
// data components of the video disc are implemented in "scsi.cpp".
// The video information is stored in shared memory to allow the BBC Domesday
// Emulator system to merge it with the BBC screen output. This code also deals
// with emulation of some LV-Rom F-codes (sent by the Serial RS423 of the BBC
// Master to a Serial RS232 on the back of the videodisc player). These F-codes
// control the actions of the LV-Rom and also return some information on the
// LV-Rom status.
//
// Developed by Richard Gellman for the CAMiLEON project (10/2002)
// Modified and improved + addition of more F-codes, [DMSergeant] (12/2002)

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "beebwin.h"
#include "main.h"
#include "6502core.h"
#include "lvrom.h"
#include "debugLogs.h"

// Variables that relate the Side of an LV Disc with the correct files
char lvDiscName[LV_DISC_CNT][80], lvDiscLdiFile[LV_DISC_CNT][80];
char lvDiscImgDir[LV_DISC_CNT][80], lvDiscUsercode[LV_DISC_CNT][80];

// The LV Disc side that is currently loaded in the player
unsigned int currLvDisc=0;

// Variables for the Serial communication beetween BBC M128 and LV-Rom
char FCode_Buf[256];		// Data sent from BBC to LV-Rom
char FCode_Reply_Buf[256];	// Reply data sent from LV-Rom to BBC
int FCode_Ptr=0;

unsigned char LV_Audio[44100];
int LVA_Ptr=0;
int LVA_Doubler=0; // Frequency doubler for audio stream
int LastSecCycles=0;
unsigned int CFrame=0;
int LVAudioEnabled=0;

FILE *LVA_OUT;		// For output of audio bytes

#define F_HALT 1

// The different video playback modes
#define V_STILL 0
#define V_FORWARD1 1
#define V_FORWARD2 2
#define V_FORWARD3 3
#define V_BACKWARD1 -1
#define V_BACKWARD2 -2
#define V_BACKWARD3 -3

// Registers in the LV-Rom player
int FrameMode=V_STILL;
unsigned int VFrame=1;
unsigned int StopReg;
int LVA,LVE;

int lvFrameReg = 1, lvChapterReg = 0, lvInfoReg = 0, lvStopReg=54000;
int lvTimeCodeInfoReg;
int lvReplaySwitch, lvReplaySwitchEnabled=1;
int lvTransmissionDelay;
int lvHaltCount;
int lvJumpAmount;
int lvVideo = 1;
int lvAudio;
int lvAudio1;
int lvAudio2;
int lvChapterNumDisplay = 0;
int lvPictureNumDisplay = 0;
// int lvPictureNumDisplay = 1;	// While debugging make this the startup state
int lvRCtoComputer = 0;
int lvLocalControl = 1;
int lvRC = 1;
int lvFastSpeed = 6;
int lvSlowSpeed = 6;
int lvVideoOverlay = 1;
int lvVideoFromExternal = 0;	// 0 = Video from internal
int lvAudio1FromExternal = 0;
int lvAudio2FromExternal = 0;
int lvTxtFromDisc = 1;

unsigned char lvDiscStatus[5]; // see comment below
// Each status byte (x1 to x5) is in the form 0011yyyy
// Response to ?D specification:
// x1 bits 3-0: 1101 = xD or 1011 = xB
// x2 bits 3-0: 1100 = xC or 1010 = xA
// DC = CX noise reduction present, BA = No CX noise reduction
// x3: bit 3: 0 = 12" disc         1 = 8" disc
//     bit 2: 0 = side 1           1 = side 2
//     bit 1: 0 = no TXT present   1 = TXT present
//     bit 0: 0 = FM-FM mpx. off   1 = FM-FM mpx. on
// x4: bit 3: 0 = no program dump  1 = program dump in audio channel 2
//     bit 2: 0 = normal video     1 = video contains digital information
//     bit 1: 0 = (see table)
//     bit 0: 0 = (see table)
// x5: bit 3: even parity check with bits 3,2 & 0 of x4
//     bit 2: even parity check with bits 3,1 & 0 of x4
//     bit 1: even parity check with bits 2,1 & 0 of x4
//     bit 0: 0
//
// x4 bit 3, x3 bit 0, x4 bit 1, and x4 bit 0 (respectively) indicate the
// status of the analogue audio channels
//          | prog dump | FM multiplex | channel 1   channel 2
//     0000      off           off               stereo
//     0001      off           off                mono
//     0010      off           off         no sound carriers 
//     0011      off           off              bilingual
//     0100      off            on         stereo      stereo 
//     0101      off            on         stereo    bilingual 
//     0110      off            on         cross channel stereo
//     0111      off            on       bilingual   bilingual 
//     1000       on           off          mono        dump
//     1001       on           off          mono        dump
//     1010       on           off          (for future use)
//     1011       on           off          mono        dump
//     1100       on            on         stereo       dump
//     1101       on            on         stereo       dump
//     1110       on            on       bilingual      dump
//     1111       on            on       bilingual      dump

unsigned char lvPlayerStatus[5]; // see comment below
// Each status byte (x1 to x5) is in the form 01yyyyyy
// Response to ?D specification:
// x1  bit 5: 1 = normal mode (loaded)
//     bit 4: 0
//     bit 3: 0
//     bit 2: 1 = chapter play
//     bit 1: 1 = Goto action
//     bit 0: 1 = Goto action
// x2 bits 5-3 = 000
//     bit 2: 1 = chapter numbers exist on disc
//     bit 1: 1 = CLV detected
//     bit 0: 1 = CAV detected
// x3 bits 5-3 = 000
//     bit 2: 1 = replay function active (switch is on and enabled)
//     bit 1: 0
//     bit 0: 1 = frame lock
// x4: bit 5: 0
//     bit 4: 1 = RS232-C transmission delay (50 char/s)
//     bit 3: 1 = Remote control handset enabled for player control
//     bit 2: 1 = Remote control commands routed to computer
//     bit 1: 1 = Local front-panel controls enabled
//     bit 0: 0
// x5: bit 5: 1 = audio channel 2 enabled
//     bit 4: 1 = audio channel 1 enabled
//     bit 3: 1 = TXT from disc enabled
//    bits 2-0 = 000

unsigned char lvScaleBuff[LVSCALE_IM_W * LVSCALE_IM_H * LVRAW_BWIDTH];

#ifdef USE_LVROM_LOG
FILE *LVROM_LOG=NULL;
#endif

int FindFirstDigit(char *digstring) {
	int n,p;
	p=0;
	for (n=1;n<16;n++) { if ((p==0) && !(isdigit(*(digstring+n)))) p=n; }
	return p;
}

void LoadNextFrameAudio(int FrameNr)
{
  FILE *FrameAudioFile;
  long audlen;
  if (FrameNr==VFrame)
    return;

  FrameAudioFile=fopen("d:/national.snd","rb");

  if (FrameAudioFile==NULL)
  {
    // sMessageBox(GETHWND,"Could not open audio stream","BBC Emulator",MB_OK|MB_ICONERROR);
  } 
  else
  {
    fseek(FrameAudioFile,0,SEEK_END);
    audlen=ftell(FrameAudioFile);
    if (FrameNr*441<audlen)
    {
      fseek(FrameAudioFile,FrameNr*441,SEEK_SET);
      for (int n=0;n<11025;n++)
      {
        LV_Audio[n]=fgetc(FrameAudioFile);
      }
    }
    fclose(FrameAudioFile);
  }
#ifdef USE_LVROM_LOG
  //fprintf(LVROM_LOG,"Loaded frameaudio #%d\n",FrameNr);
  //fflush(LVROM_LOG);
#endif
  LVA_Ptr=0; LVA_Doubler=0;
}

// typedef struct
// {
//   BITMAPINFOHEADER	bmih;
//   RGBQUAD		bmicolors[256];
// } mybmiData;

void LoadNextVideoFrame(void)
{
  static unsigned char rawLvImage[ (LVRAW_IM_W * LVRAW_IM_H)*3 ];
  static unsigned char Spare_Buf[(768*3*2)];
  char fname[256];
  FILE *VideoFrame;
  int M_VFrame;
  int r,g,b;
  long ptr;

// if (LVROM_LOG)
// {
//   fprintf(LVROM_LOG, "Load request : image %06d (LVE-%d)\n", VFrame, LVE);
//   fflush(LVROM_LOG);
// }

  if ((LVA) && (FrameMode==V_FORWARD1))
    LVAudioEnabled=1;
  LVAudioEnabled=0; // [DMS] - turn off all audio
  // if (VFrame>20000) VFrame=1500;
  if (VFrame>=StopReg)
  {
    StopReg=MAX_LV_FRAME_NUM;
    sprintf(FCode_Reply_Buf,"A2\r");
    FrameMode=V_STILL;
  }
  if ((VFrame % 5) && (FrameMode != V_STILL))
    return;
//  if ((((VFrame/5)*5)!=VFrame) && (FrameMode!=V_STILL))
//    return;

  M_VFrame=VFrame;
  if (M_VFrame<1)
    M_VFrame=1;
  if (LVE==0)
  {
    memset(lvScaleBuff, 0x00, LVSCALE_IM_H*LVSCALE_IM_W*LVRAW_BWIDTH);
    return;
  }

  sprintf(fname,"%s/%02X/%02X/%06d.dim", lvDiscImgDir[currLvDisc],
		M_VFrame >> 16, M_VFrame >> 8, M_VFrame); 
  VideoFrame=fopen(fname,"rb");

//  if (VideoFrame == NULL)
//  {
//    sprintf(fname,"gunzip %s/%02X/%02X/%06d.dim.gz", lvDiscImgDir[currLvDisc],
//		M_VFrame >> 16, M_VFrame >> 8, M_VFrame); 
//    VideoFrame = popen(fname, "rb");
//  }
//  else
//  {
//    sprintf(fname,"%s/%02X/%02X/%06d.dim", lvDiscImgDir[currLvDisc],
//		M_VFrame >> 16, M_VFrame >> 8, M_VFrame); 
  // MessageBox(GETHWND,fname,"BBC Emulator",MB_OK|MB_ICONERROR);
//    VideoFrame=fopen(fname,"rb");
//  }

// if (VideoFrame == NULL)
// {
//   M_VFrame = 250;
//   sprintf(fname,"D:/domesday/videoImages/commSImg/%02X/%02X/%06d.dim\0",
// 	  ((M_VFrame/(256*256)) % 256), ((M_VFrame/256) % 256), M_VFrame);
//   VideoFrame=fopen(fname,"rb");
// }

  if (VideoFrame != NULL)
  {
    ptr=0;

    for (int y=0; y<LVRAW_IM_H; y++)
    {
      // Skip the X margin bytes at the start of the scanline.
      fread(Spare_Buf, 1, LV_IM_MARGIN * LVRAW_BWIDTH, VideoFrame);
      fread(rawLvImage+ptr, 1, (LVRAW_IM_W - (2*LV_IM_MARGIN)) * LVRAW_BWIDTH,
		VideoFrame);
      ptr += (LVRAW_IM_W - (2*LV_IM_MARGIN)) * LVRAW_BWIDTH;
      // Skip the X margin bytes at the end of the scanline.
      fread(Spare_Buf, 1, LV_IM_MARGIN * LVRAW_BWIDTH, VideoFrame);
    }
    fclose(VideoFrame);
  }
  else
  {
    memset(lvScaleBuff, 0x00, LVSCALE_IM_H*LVSCALE_IM_W*LVRAW_BWIDTH);
    return;
  }

  lvBmi.bmiHeader.biWidth = LVRAW_IM_W - (2*LV_IM_MARGIN);

  StretchDIBits(hdcMem,
		0,0, LVSCALE_IM_W, LVSCALE_IM_H,// Destination x,y,w,h
		0,0, LVRAW_IM_W-(2*LV_IM_MARGIN), LVSCALE_IM_H, // Source
		rawLvImage,			// Source pixels
		(BITMAPINFO *) &lvBmi,		// dimensions of source pixels
		DIB_RGB_COLORS,			// Must be RGB
		SRCCOPY);			// Raster copy op code
		
  if (lvPictureNumDisplay)
  {
    char myText[40];

    sprintf(myText, "%06d\0", M_VFrame);
    TextOut(hdcMem, 300, 200, (LPCTSTR) myText, 6);
  }

  lvBmi.bmiHeader.biWidth = LVSCALE_IM_W;

  GetDIBits(hdcMem,			// Source DC
		hbmMem,			// Source Bitmap
		0, LVSCALE_IM_H,	// start line, number of lines
		lvScaleBuff,		// Array to receive the pixels
		(BITMAPINFO *) &lvBmi,	// Dimensions of the array
		DIB_RGB_COLORS);	// Must be RGB

}

void GotoFrame(int FrameNr,int FrameAction)
{
  LVE=1;
  CFrame=FrameNr;
  VFrame=FrameNr;
// LoadNextFrameAudio(FrameNr); // [DMS] - turn off all audio code
  LoadNextVideoFrame();
}

void Do_FCode_Command(void)
{
  unsigned char FrameInstructionCode;
  int FIC_Ptr;
#ifdef USE_LVROM_LOG
  fprintf(LVROM_LOG,"%s\n",FCode_Buf);
  fflush(LVROM_LOG);
#endif

  /**************************
   * The FCodes listed in the documentation are: [DMS 02/12/2002]
   * !xy	- sound insert
   * #xy	- RC-5 command out via A/V Euroconnector
   * $0		- replay switch disable 
   * $1		- replay switch enable 
   * '		- Eject
   * )0		- Transmission delay off
   * )1		- Transmission delay on
   * *		- Halt (still mode)
   * *xxxxx+yy	- Repetitive halt and jump forward
   * *xxxxx-yy	- Repetitive halt and jump backward
   * +yy	- Instant jump forward yy tracks
   * ,0		- Standby (unload)
   * ,1		- On (load)
   * -yy	- Instant jump backward yy tracks
   * /		- Pause (halt + all muted)
   * :		- Reset to default values
   * ?F		- Picture number request
   * ?C		- Chapter number request
   * ?D		- Disc program status request
   * ?P		- Player status request
   * ?U		- User code request
   * ?=		- Revision level request
   * A0		- Audio 1-off
   * A1		- Audio 1-on
   * B0		- Audio 2-off
   * B1		- Audio 2-on
   * C0		- Chapter no display off
   * C1		- Chapter no display on
   * D0		- Picture no display off
   * D1		- Picture no display on
   * E0		- Video off
   * E1		- Video on
   * FxxxxxI	- Load picture number information register
   * FxxxxxS	- Load picture number stop register
   * FxxxxxR	- Load picture number then Still mode
   * FxxxxxN	- Load picture number then Normal play
   * FxxxxxQ	- Load picture number and continue previous play mode
   * H0		- Remote control not routed to computer
   * H1		- Remote control routed to computer
   * I0		- Local front-panel buttons disabled
   * I1		- Local front-panel buttons enabled
   * J0		- Remote control diabled
   * J1		- Remote control enabled
   * L		- Still forward
   * M		- Still reverse
   * N		- Normal play forward
   * Nxxxxx+yy	- Repetitive play forward and jump forward
   * Nxxxxx-yy	- Repetitive play forward and jump backward
   * O		- Play reverse
   * Oxxxxx+yy	- Play reverse and jump forward
   * Oxxxxx-yy	- Play reverse and jump forward jump backward
   * QxxR	- Goto chapter and halt
   * QxxN	- Goto chapter and play
   * QxxyyzzS	- Goto chapter and halt
   * SxxF	- Set fast speed value
   * SxxS	- Set slow speed value
   * TxxyyN	- Set fast speed value
   * TxxyyI	- Set slow speed value
   * U		- Slow motion forward
   * V		- Slow motion reverse
   * VPy	- Video overlay (VP1 is default)
   * VPX	- Request current VP mode
   * W		- Fast forward
   * X		- Clear
   * Z		- Fast reverse
   * [0		- Audio-1 from internal
   * [1		- Audio-1 from external
   * \0		- Video from internal
   * \1		- Video from external
   * ]0		- Audio-2 from internal
   * ]1		- Audio-2 from external
   * _0		- Teletext from disc off
   * _1		- Teletext from disc on
   ******************/

  FCode_Ptr=0;
  if (FCode_Buf[0]=='F')
  {
    // Frame code
    FIC_Ptr=FindFirstDigit(FCode_Buf);
    FrameInstructionCode=FCode_Buf[FIC_Ptr];
    FCode_Buf[FIC_Ptr]=0;
    if (FrameInstructionCode=='R')
    {
      FrameMode=V_STILL;
      GotoFrame(atoi(FCode_Buf+1),F_HALT);
      sprintf(FCode_Reply_Buf,"A0\r");
      LVAudioEnabled=0;
    }
    if (FrameInstructionCode=='S')
    {
      StopReg=atoi(FCode_Buf+1);
      sprintf(FCode_Reply_Buf,"\r");
    }
    if ((FrameInstructionCode=='I') || (FrameInstructionCode=='N') ||
    	(FrameInstructionCode=='Q'))
    {
//      MessageBox(GETHWND, FCode_Buf,"Unimplemented F-code",MB_OK|MB_ICONERROR);
    }
  }
  if ((FCode_Buf[0]=='N') && (FCode_Buf[1]<32))
  {
	  FrameMode=V_FORWARD1;
	  sprintf(FCode_Reply_Buf,"\r");
  }
  if ((FCode_Buf[0]=='A') || (FCode_Buf[0]=='B'))
  {
	  // Audio command - get state
	  LVA=FCode_Buf[1]-48;
	  sprintf(FCode_Reply_Buf,"\r");
  }
  if (FCode_Buf[0]=='E')
  {
	  // Video command - get state
	  LVE=FCode_Buf[1]-48;
	  sprintf(FCode_Reply_Buf,"\r");
  }
  if (strncmp(FCode_Buf,"?U",2)==0)
  {
    sprintf(FCode_Reply_Buf, "%s\r", lvDiscUsercode[currLvDisc]);
  }
  if (strncmp(FCode_Buf,"VP",2)==0)
  {
    unsigned char testchar;
    // Select Mix Modee
    testchar=FCode_Buf[2];
    if (testchar=='X')
    {
      // report it
      sprintf(FCode_Reply_Buf, "VP%c\n", 48+MixMode);
    }
    else
    {
      if ((testchar>='1') && (testchar<='5'))
      {
	MixMode=testchar-48;
	sprintf(FCode_Reply_Buf,"\r");
      }
    }
  }
  if (FCode_Buf[0]=='D')
  {
	  // Picture Number/Time Code Display Off/On
          lvPictureNumDisplay = FCode_Buf[1]-48;
	  sprintf(FCode_Reply_Buf,"\r");
  }
  if (strncmp(FCode_Buf,"?=",2)==0)
    sprintf(FCode_Reply_Buf,"=01718\r");

  if (FCode_Buf[0] == 'X')
  {
    StopReg=MAX_LV_FRAME_NUM;
    LVAudioEnabled=0;
    FrameMode=V_STILL;
  }
}

void AckLVReply(void) {
//	if (LVROM_LOG!=NULL) fprintf(LVROM_LOG,"Read reply: %s\n",FCode_Reply_Buf);
//	  fflush(LVROM_LOG);
}

void LVROM_PutSerial(unsigned char data)
{
#ifdef USE_LVROM_LOG
  fprintf(LVROM_LOG,"Wrote %02x to serial port\n",data);
  fflush(LVROM_LOG);
#endif

  FCode_Buf[FCode_Ptr++]=data;
  if (FCode_Buf[FCode_Ptr-1==13])
  {
    FCode_Buf[FCode_Ptr-1]=0;
    Do_FCode_Command();
  }
}

unsigned char GetAudioByte(void) {
	unsigned char TmpByte;
	// if (LVA_OUT==NULL) LVA_OUT=fopen("/lva.snd","wb");
	TmpByte=LV_Audio[LVA_Ptr];
	if (LVA_Doubler++>2) { LVA_Ptr++; LVA_Doubler=0; }
	if (LVA_Ptr>440) { LVA_Ptr=0;
		CFrame+=FrameMode;
		LoadNextFrameAudio(CFrame); }
	if (LVAudioEnabled==0) { TmpByte=128; }
	// fputc(TmpByte,LVA_OUT);
	return TmpByte;
}

void LVROM_Poll(int NCycles)
{
	// Check the cycles for this second, and output the audio stream
	if (NCycles<LastSecCycles) {
		// next second of audio ready
		//CFrame++;
		//LoadNextFrameAudio(CFrame);
	}
	LastSecCycles=NCycles;
}

void LVROM_Reset(void)
{
  FILE *lvcfg=NULL;
  int i, j;
  unsigned char hex_conv1, hex_conv2, hex_convR;

#ifdef USE_LVROM_LOG
  openLog(LVROM_LOG, LVROM_LOG_FN);
#endif

/***************** Sample LV.cfg file *****************************
communityS F:/laserDiscImages/communitySouth.ldi G:/commSthImg/ 55313D303636
communityN F:/laserDiscImages/communityNorth.ldi G:/commNthImg/ 55313D303637
nationalA  F:/laserDiscImages/nationalA.ldi      G:/natAImg/    55313D393836
nationalB  NotApplicable                         G:/natBImg/    55313D393837
 ******************************************************************/
  lvcfg = fopen("LV.cfg", "r");
  if (lvcfg != NULL)
  {
    for (i=0; i<LV_DISC_CNT; i++)
    {
      fscanf(lvcfg, " %s %s %s %s", lvDiscName[i], lvDiscLdiFile[i],
					lvDiscImgDir[i], lvDiscUsercode[i]);
    }
    
    currLvDisc=0;

    // Convert the Usercodes into the six byte string
    for (i=0; i<LV_DISC_CNT; i++)
    {
      for (j=0; j<6; j++)
      {
        hex_conv1 = lvDiscUsercode[i][j*2];
        hex_conv2 = lvDiscUsercode[i][j*2+1];

        if ((hex_conv1 >= '0') && (hex_conv1 <= '9'))
	{
	  hex_conv1 = hex_conv1 - '0';
	}
	else
	{
	  hex_conv1 = (hex_conv1 - 'A') + 10;
	}
        if ((hex_conv2 >= '0') && (hex_conv2 <= '9'))
	{
	  hex_conv2 = hex_conv2 - '0';
	}
	else
	{
	  hex_conv2 = (hex_conv2 - 'A') + 10;
	}
	hex_convR = (hex_conv1 << 4) + hex_conv2;
        lvDiscUsercode[i][j] = hex_convR;
      }
      lvDiscUsercode[i][6] = '\0';
      // MessageBox(GETHWND, lvDiscUsercode[i], "User code", MB_OK|MB_ICONERROR);
    }
  }
  else
  {
    MessageBox(GETHWND, "Could not load LV.cfg","BBC Emulator",
		MB_OK|MB_ICONERROR);
  }

  GotoFrame(1,0);
  MixMode=MIXMODE_LV;
//  MixMode=2;
//  MixMode=4;	// Start up with an even mix between Video and BBC

  StopReg=MAX_LV_FRAME_NUM;
  LVA=0;
  LVE=0;
//  CFrame=0;
//  VFrame=0;
}

void NextVideoPoll(void)
{
  unsigned int OFrame;
  OFrame=VFrame;
  VFrame+=FrameMode;
  if (VFrame!=OFrame)
    LoadNextVideoFrame();
}

