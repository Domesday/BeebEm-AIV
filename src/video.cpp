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
/* Video handling -          David Alan Gilbert */

/* Ver 2 - 24/12/94 - Designed to emulate start of frame interrupt correctly */

/* Mike Wyatt 7/6/97 - Added cursor display and Win32 port */

/* Richard Gellman 4/2/2001 AAAARGH SHADOW RAM! HELP! */

#include "iostream.h"
#include <stdlib.h>
#include <stdio.h>

#include "6502core.h"
#include "beebmem.h"
#include "beebwin.h"
#include "main.h"
#include "sysvia.h"
#include "video.h"
#include "uefstate.h"
#include "beebsound.h"
#include "debugLogs.h"

#ifdef BEEB_DOTIME
#include <sys/times.h>
#ifdef SUNOS
#include <sys/param.h>
#endif
#ifdef HPUX
#include <unistd.h>
#endif
#endif

/* Bit assignments in control reg:
   0 - Flash colour (0=first colour, 1=second)
   1 - Teletext select (0=on chip serialiser, 1=teletext)
 2,3 - Bytes per line (2,3=1,1 is 80, 1,0=40, 0,1=20, 0,0=10)
   4 - CRTC Clock chip select (0 = low frequency, 1= high frequency)
 5,6 - Cursor width in bytes (0,0 = 1 byte, 0,1=not defined, 1,0=2, 1,1=4)
   7 - Master cursor width (if set causes large cursor)
*/
EightUChars FastTable[256];
SixteenUChars FastTableDWidth[256]; /* For mode 4,5,6 */
int FastTable_Valid=0;

typedef void (*LineRoutinePtr)(void);
LineRoutinePtr LineRoutine;

unsigned char int_scan=0;

/* Translates middle bits of VideoULA_ControlReg to number of colours */
static int NColsLookup[]={ 16, 4, 2, 0 /* Not supported 16? */,
			    0 /* ???? */, 16, 4, 2}; /* Based on AUG 379 */

unsigned char VideoULA_ControlReg=0x9c;
unsigned char VideoULA_Palette[16];

unsigned char CRTCControlReg=0;
unsigned char CRTC_HorizontalTotal=127;     /* R0 */
unsigned char CRTC_HorizontalDisplayed=80;  /* R1 */
unsigned char CRTC_HorizontalSyncPos=98;    /* R2 */
unsigned char CRTC_SyncWidth=0x28;          /* R3 */
/* R3 - top 4 bits are Vert (in scan lines) and bottom 4 are horiz in chars */
unsigned char CRTC_VerticalTotal=38;        /* R4 */
unsigned char CRTC_VerticalTotalAdjust=0;   /* R5 */
unsigned char CRTC_VerticalDisplayed=32;    /* R6 */
unsigned char CRTC_VerticalSyncPos=34;      /* R7 */
unsigned char CRTC_InterlaceAndDelay=0;     /* R8 */
/* R8 - 0,1 are interlace modes, 4,5 display blanking delay, 6,7 cursor blanking delay */
unsigned char CRTC_ScanLinesPerChar=7;      /* R9 */
unsigned char CRTC_CursorStart=0;           /* R10 */
unsigned char CRTC_CursorEnd=0;             /* R11 */
unsigned char CRTC_ScreenStartHigh=6;       /* R12 */
unsigned char CRTC_ScreenStartLow=0;        /* R13 */
unsigned char CRTC_CursorPosHigh=0;         /* R14 */
unsigned char CRTC_CursorPosLow=0;          /* R15 */
unsigned char CRTC_LightPenHigh=0;          /* R16 */
unsigned char CRTC_LightPenLow=0;           /* R17 */

unsigned int ActualScreenWidth=640;   /* var never referenced [DMS 18/11/02] */
int InitialOffset=0,InitialVOffset=0;
long ScreenAdjust=0; // Mode 7 Defaults.
long VScreenAdjust=0;
int InPic=0;
int VStart,HStart;
unsigned char HSyncModifier=9;
int LSL;
unsigned char TeletextEnabled;
char TeletextStyle=1;	// If set, teletext skips intermediate lines to speed up
int THalfMode=0;	// 1 if to use half-mode (TeletextStyle=1 all the time)

int ova,ovn;		// mem ptr buffers
int SoundLineCount=25;	// Sound cycle calculation

typedef struct
{
  int Addr;       /* Addr of next visible char line in beeb memory - raw */
  int PixmapLine; /* Current line in the pixmap */
  int PreviousFinalPixmapLine; /* The last pixmap line on the previous frame */
  int IsTeletext; /* This frame is a teletext frame - do things differently */
  char *DataPtr;  /* Pointer into host memory of video data */

/* CharLine counts from the 'reference point' - i.e. the point at which
 * we reset the address pointer - NOT the point of the sync. If it
 * is -ve its actually in the adjust time */
  int CharLine;   /* 6845 counts in chars vertically - 0 at top, incs by 1 */
		  /* -1 means in a state before the actual display starts */
  int InCharLine; /* Scanline within a char line - counts down */
  int InCharLineUp;/* Scanline within a char line - counts up */
  int VSyncState; /* Cannot =0 in MSVC $NRM; When >0 VSync is high */
} VideoStateT;

static VideoStateT VideoState;

__int64 VideoTriggerCount=9999; /* Num of cycles before next scanline service */

/* first subscript is graphics flag (1 for graphics, 2 for separated graphics),
 * next is character, then scanline */
/* character is (valu &127)-32 */
static unsigned int EM7Font[3][96][20]; // 20 rows to account for "half pixels"

static int Mode7FlashOn=1;   /* True if a flashing character in mode 7 is on */
static int Mode7DoubleHeightFlags[80];    /* Pessimistic size for this flags */
		    /* if 1 then corresponding char on NEXT line is top half */
static int CurrentLineBottom=0;
static int NextLineBottom=0;/* 1 if nxt line of dbl height is be bottoms only*/

/* Flash every half second(???) i.e. 25 x 50Hz fields */
/* No! On time needs to be longer than off time. - according to my datasheet,
 * it is 0.75Hz with 3:1 ON:OFF ratio. - Richard Gellman */
/* Which means : on for 0.75 secs, off for 0.25 secs ? */ /* (Check on Beeb) */
#define MODE7FLASHFREQUENCY 25
#define MODE7ONFIELDS 37
#define MODE7OFFFIELDS 13
unsigned char CursorOnFields, CursorOffFields;
int CursorFieldCount=32;
unsigned char CursorOnState=1;
int Mode7FlashTrigger=MODE7ONFIELDS;

/* If 1 then refresh on every display, else refresh every n'th display */
int Video_RefreshFrequency=1;
/* Num of the curr frame - starts at Video_RefreshFrequency - at 0 do refresh */
static int FrameNum=0;

static void LowLevelDoScanLineNarrow();
static void LowLevelDoScanLineWide();
static void LowLevelDoScanLineNarrowNot4Bytes();
static void LowLevelDoScanLineWideNot4Bytes();
static void VideoAddCursor(void);
void AdjustVideo();
void VideoAddLEDs(void);
int VSyncCount=0;

unsigned char beebscreen[640*512];	// Magic numbers? Why not 320*256 [DMS]

/*---------------------------------------------------------------------*/
static void BuildMode7Font(void)
{
  FILE *m7File;
  unsigned char m7cc,m7cy;
  unsigned int m7cb;
  unsigned int row1,row2,row3; // row builders for mode 7 graphics
  char TxtFnt[256];

  // Build enhanced mode 7 font
  strcpy(TxtFnt,RomPath);
  strcat(TxtFnt,"teletext.fnt");
  m7File=fopen(TxtFnt,"rb");
  for (m7cc=32;m7cc<=127;m7cc++)
  {
    for (m7cy=0;m7cy<=17;m7cy++)
    {
      m7cb=fgetc(m7File);
      m7cb|=fgetc(m7File)<<8;
      EM7Font[0][m7cc-32][m7cy+2]=m7cb<<2;
      EM7Font[1][m7cc-32][m7cy+2]=m7cb<<2;
      EM7Font[2][m7cc-32][m7cy+2]=m7cb<<2;
    }
    EM7Font[0][m7cc-32][0]=EM7Font[1][m7cc-32][0]=EM7Font[2][m7cc-32][0]=0;
    EM7Font[0][m7cc-32][1]=EM7Font[1][m7cc-32][1]=EM7Font[2][m7cc-32][1]=0;
  }
  fclose(m7File);

  // Now fill in the graphics - this is built from an algorithm, but
  // has certain lines/columns blanked for separated graphics.
  for (m7cc=0;m7cc<96;m7cc++)
  {
    // here's how it works: top two blocks:  1 &  2
    //                   middle two blocks:  4 &  8
    //                   bottom two blocks: 16 & 64
    // its only a grpahics character if bit 5 (32) is clear.
    if (!(m7cc & 32))
    {
      row1=0; row2=0; row3=0;
      // left block has a value of 4032, right 63 and both 4095
      if (m7cc & 1)  row1|=4032;
      if (m7cc & 2)  row1|=63;
      if (m7cc & 4)  row2|=4032;
      if (m7cc & 8)  row2|=63;
      if (m7cc & 16) row3|=4032;
      if (m7cc & 64) row3|=63;
      // now input these values into the array
      // top row of blocks - continuous
      EM7Font[1][m7cc][0]=EM7Font[1][m7cc][1]=EM7Font[1][m7cc][2]=row1;
      EM7Font[1][m7cc][3]=EM7Font[1][m7cc][4]=EM7Font[1][m7cc][5]=row1;
      // Separated
      row1&=975; // insert gaps
      EM7Font[2][m7cc][0]=EM7Font[2][m7cc][1]=EM7Font[2][m7cc][2]=row1;
      EM7Font[2][m7cc][3]=row1; EM7Font[2][m7cc][4]=EM7Font[2][m7cc][5]=0;
      // middle row of blocks - continuous
      EM7Font[1][m7cc][6]=EM7Font[1][m7cc][7]=EM7Font[1][m7cc][8]=row2;
      EM7Font[1][m7cc][9]=EM7Font[1][m7cc][10]=EM7Font[1][m7cc][11]=row2;
      EM7Font[1][m7cc][12]=EM7Font[1][m7cc][13]=row2;
      // Separated
      row2&=975; // insert gaps
      EM7Font[2][m7cc][6]=EM7Font[2][m7cc][7]=EM7Font[2][m7cc][8]=row2;
      EM7Font[2][m7cc][9]=EM7Font[2][m7cc][10]=EM7Font[2][m7cc][11]=row2;
      EM7Font[2][m7cc][12]=EM7Font[2][m7cc][13]=0;
      // Bottom row - continuous
      EM7Font[1][m7cc][14]=EM7Font[1][m7cc][15]=EM7Font[1][m7cc][16]=row3;
      EM7Font[1][m7cc][17]=EM7Font[1][m7cc][18]=EM7Font[1][m7cc][19]=row3;
      // Separated
      row3&=975; // insert gaps
      EM7Font[2][m7cc][14]=EM7Font[2][m7cc][15]=EM7Font[2][m7cc][16]=row3;
      EM7Font[2][m7cc][17]=row3; EM7Font[2][m7cc][18]=EM7Font[2][m7cc][19]=0;
    } // check for valid char to modify
  } // character loop.
} /* BuildMode7Font */
/*--------------------------------------------------------------------------*/
static void DoFastTable16(void)
{
  unsigned int beebpixvl,beebpixvr;
  unsigned int bplvopen,bplvtotal;
  unsigned char tmp;

  for(beebpixvl=0;beebpixvl<16;beebpixvl++)
  {
    bplvopen=((beebpixvl & 8)?128:0) |
             ((beebpixvl & 4)?32:0) |
             ((beebpixvl & 2)?8:0) |
             ((beebpixvl & 1)?2:0);
    for(beebpixvr=0;beebpixvr<16;beebpixvr++)
    {
      bplvtotal=bplvopen |
             ((beebpixvr & 8)?64:0) |
             ((beebpixvr & 4)?16:0) |
             ((beebpixvr & 2)?4:0) |
             ((beebpixvr & 1)?1:0);
      tmp=VideoULA_Palette[beebpixvl];
      if (tmp>7)
      {
        tmp&=7;
        if (VideoULA_ControlReg & 1)
	  tmp^=7;
      }
      FastTable[bplvtotal].data[0]=FastTable[bplvtotal].data[1]=
	FastTable[bplvtotal].data[2]=FastTable[bplvtotal].data[3]=
							mainWin->cols[tmp];

      tmp=VideoULA_Palette[beebpixvr];
      if (tmp>7)
      {
        tmp&=7;
        if (VideoULA_ControlReg & 1)
	  tmp^=7;
      }
      FastTable[bplvtotal].data[4]=FastTable[bplvtotal].data[5]=
        FastTable[bplvtotal].data[6]=FastTable[bplvtotal].data[7]=
							mainWin->cols[tmp];
    } /* beebpixvr */
  } /* beebpixvl */
} /* DoFastTable16 */

/*-----------------------------------------------------------------------*/
static void DoFastTable16XStep8(void)
{
  unsigned int beebpixvl,beebpixvr;
  unsigned int bplvopen,bplvtotal;
  unsigned char tmp;

  for(beebpixvl=0;beebpixvl<16;beebpixvl++)
  {
    bplvopen=((beebpixvl & 8)?128:0) |
             ((beebpixvl & 4)?32:0) |
             ((beebpixvl & 2)?8:0) |
             ((beebpixvl & 1)?2:0);
    for(beebpixvr=0;beebpixvr<16;beebpixvr++)
    {
      bplvtotal=bplvopen |
             ((beebpixvr & 8)?64:0) |
             ((beebpixvr & 4)?16:0) |
             ((beebpixvr & 2)?4:0) |
             ((beebpixvr & 1)?1:0);
      tmp=VideoULA_Palette[beebpixvl];
      if (tmp>7)
      {
        tmp&=7;
        if (VideoULA_ControlReg & 1)
	  tmp^=7;
      }
      FastTableDWidth[bplvtotal].data[0]=FastTableDWidth[bplvtotal].data[1]=
        FastTableDWidth[bplvtotal].data[2]=FastTableDWidth[bplvtotal].data[3]=
	FastTableDWidth[bplvtotal].data[4]=FastTableDWidth[bplvtotal].data[5]=
        FastTableDWidth[bplvtotal].data[6]=FastTableDWidth[bplvtotal].data[7]=
							mainWin->cols[tmp];

      tmp=VideoULA_Palette[beebpixvr];
      if (tmp>7)
      {
        tmp&=7;
        if (VideoULA_ControlReg & 1)
	  tmp^=7;
      }
      FastTableDWidth[bplvtotal].data[8]=FastTableDWidth[bplvtotal].data[9]=
        FastTableDWidth[bplvtotal].data[10]=FastTableDWidth[bplvtotal].data[11]=
	FastTableDWidth[bplvtotal].data[12]=FastTableDWidth[bplvtotal].data[13]=
        FastTableDWidth[bplvtotal].data[14]=FastTableDWidth[bplvtotal].data[15]=
							mainWin->cols[tmp];
    } /* beebpixvr */
  } /* beebpixvl */
} /* DoFastTable16XStep8 */
/*--------------------------------------------------------------------------*/
/* Some guess work and experimentation has determined that the left most
 * pixel uses bits 7,5,3,1 for the palette address, the next uses 6,4,2,0,
 * the next uses 5,3,1,H (H=High), then 5,2,0,H *****************************/
static void DoFastTable4(void)
{
  unsigned char tmp;
  unsigned long beebpixv,pentry;

  for(beebpixv=0;beebpixv<256;beebpixv++)
  {
    pentry=((beebpixv & 128)?8:0)
           | ((beebpixv & 32)?4:0)
           | ((beebpixv & 8)?2:0)
           | ((beebpixv & 2)?1:0);
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
        tmp^=7;
    }
    FastTable[beebpixv].data[0]=FastTable[beebpixv].data[1]=mainWin->cols[tmp];

    pentry=((beebpixv & 64)?8:0)
           | ((beebpixv & 16)?4:0)
           | ((beebpixv & 4)?2:0)
           | ((beebpixv & 1)?1:0);
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
        tmp^=7;
    }
    FastTable[beebpixv].data[2]=FastTable[beebpixv].data[3]=mainWin->cols[tmp];

    pentry=((beebpixv & 32)?8:0)
           | ((beebpixv & 8)?4:0)
           | ((beebpixv & 2)?2:0)
           | 1;
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
        tmp^=7;
    }
    FastTable[beebpixv].data[4]=FastTable[beebpixv].data[5]=mainWin->cols[tmp];
    pentry=((beebpixv & 16)?8:0)
           | ((beebpixv & 4)?4:0)
           | ((beebpixv & 1)?2:0)
           | 1;
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
	tmp^=7;
    }
    FastTable[beebpixv].data[6]=FastTable[beebpixv].data[7]=mainWin->cols[tmp];
  } /* beebpixv */
} /* DoFastTable4 */

/*--------------------------------------------------------------------------*/
/* Some guess work and experimentation has determined that the left most
 * pixel uses bits 7,5,3,1 for the palette address, the next uses 6,4,2,0,
 * the next uses 5,3,1,H (H=High), then 5,2,0,H          ********************/
static void DoFastTable4XStep4(void)
{
  unsigned char tmp;
  unsigned long beebpixv,pentry;

  for(beebpixv=0;beebpixv<256;beebpixv++)
  {
    pentry=((beebpixv & 128)?8:0)
           | ((beebpixv & 32)?4:0)
           | ((beebpixv & 8)?2:0)
           | ((beebpixv & 2)?1:0);
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
        tmp^=7;
    }
    FastTableDWidth[beebpixv].data[0]=FastTableDWidth[beebpixv].data[1]=
	FastTableDWidth[beebpixv].data[2]=FastTableDWidth[beebpixv].data[3]=
							mainWin->cols[tmp];

    pentry=((beebpixv & 64)?8:0)
           | ((beebpixv & 16)?4:0)
           | ((beebpixv & 4)?2:0)
           | ((beebpixv & 1)?1:0);
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
        tmp^=7;
    }
    FastTableDWidth[beebpixv].data[4]=FastTableDWidth[beebpixv].data[5]=
    	FastTableDWidth[beebpixv].data[6]=FastTableDWidth[beebpixv].data[7]=
							mainWin->cols[tmp];

    pentry=((beebpixv & 32)?8:0)
           | ((beebpixv & 8)?4:0)
           | ((beebpixv & 2)?2:0)
           | 1;
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
        tmp^=7;
    }
    FastTableDWidth[beebpixv].data[8]=FastTableDWidth[beebpixv].data[9]=
	FastTableDWidth[beebpixv].data[10]=FastTableDWidth[beebpixv].data[11]=
							mainWin->cols[tmp];
    pentry=((beebpixv & 16)?8:0)
           | ((beebpixv & 4)?4:0)
           | ((beebpixv & 1)?2:0)
           | 1;
    tmp=VideoULA_Palette[pentry];
    if (tmp>7)
    {
      tmp&=7;
      if (VideoULA_ControlReg & 1)
        tmp^=7;
    }
    FastTableDWidth[beebpixv].data[12]=FastTableDWidth[beebpixv].data[13]=
	FastTableDWidth[beebpixv].data[14]=FastTableDWidth[beebpixv].data[15]=
							mainWin->cols[tmp];
  } /* beebpixv */
} /* DoFastTable4XStep4 */

/*--------------------------------------------------------------------------*/
/* Some guess work and experimentation has determined that the left most pixel
 * uses the same pattern as mode 1 all the way upto the 5th pixel which uses
 * 31hh then 20hh and then 1hhh then 0hhhh                          *********/
static void DoFastTable2(void)
{
  unsigned char tmp;
  unsigned long beebpixv,beebpixvt,pentry;
  int pix;

  for(beebpixv=0;beebpixv<256;beebpixv++)
  {
    beebpixvt=beebpixv;
    for(pix=0;pix<8;pix++) {
      pentry=((beebpixvt & 128)?8:0)
             | ((beebpixvt & 32)?4:0)
             | ((beebpixvt & 8)?2:0)
             | ((beebpixvt & 2)?1:0);
      beebpixvt<<=1;
      beebpixvt|=1;
      tmp=VideoULA_Palette[pentry];
      if (tmp>7)
      {
        tmp&=7;
        if (VideoULA_ControlReg & 1)
	  tmp^=7;
      }
      FastTable[beebpixv].data[pix]=mainWin->cols[tmp];
    } /* pix */
  } /* beebpixv */
} /* DoFastTable2 */

/*--------------------------------------------------------------------------*/
/* Some guess work and experimentation has determined that the left most pixel
 * uses the same pattern as mode 1 all the way upto the 5th pixel which uses
 * 31hh then 20hh and then 1hhh then 0hhhh                          *********/
static void DoFastTable2XStep2(void)
{
  unsigned char tmp;
  unsigned long beebpixv,beebpixvt,pentry;
  int pix;

  for(beebpixv=0;beebpixv<256;beebpixv++)
  {
    beebpixvt=beebpixv;
    for(pix=0;pix<8;pix++) {
      pentry=((beebpixvt & 128)?8:0)
             | ((beebpixvt & 32)?4:0)
             | ((beebpixvt & 8)?2:0)
             | ((beebpixvt & 2)?1:0);
      beebpixvt<<=1;
      beebpixvt|=1;
      tmp=VideoULA_Palette[pentry];
      if (tmp>7)
      {
        tmp&=7;
        if (VideoULA_ControlReg & 1)
	  tmp^=7;
      }
      FastTableDWidth[beebpixv].data[pix*2]=
      	FastTableDWidth[beebpixv].data[pix*2+1]=mainWin->cols[tmp];
    } /* pix */
  } /* beebpixv */
} /* DoFastTable2XStep2 */

/*--------------------------------------------------------------------------*/
/* Check validity of fast table, and if invalid rebuild.
 * The fast table accelerates the translation of beeb video memory
 * values into X pixel values */
static void DoFastTable(void)
{
  /* if it's already OK then quit */
  if (FastTable_Valid)
    return;

  if (!(CRTC_HorizontalDisplayed & 3))
  {
    LineRoutine= (VideoULA_ControlReg & 0x10) ? LowLevelDoScanLineNarrow :
    						LowLevelDoScanLineWide;
  }
  else
  {
    LineRoutine =(VideoULA_ControlReg & 0x10)?LowLevelDoScanLineNarrowNot4Bytes:
    						LowLevelDoScanLineWideNot4Bytes;
  }

  /* What happens next dpends on the number of colours */
  switch (NColsLookup[(VideoULA_ControlReg & 0x1c) >> 2])
  {
    case 2:
      if (VideoULA_ControlReg & 0x10)
      {
        DoFastTable2();
      }
      else
      {
        DoFastTable2XStep2();
      }
      FastTable_Valid=1;     
      break;
    case 4:
      if (VideoULA_ControlReg & 0x10)
      {
        DoFastTable4();
      }
      else
      {
        DoFastTable4XStep4();
      }
      FastTable_Valid=1;     
      break;
    case 16:
      if (VideoULA_ControlReg & 0x10)
      {
        DoFastTable16();
      }
      else
      {
        DoFastTable16XStep8();
      }
      FastTable_Valid=1;
      break;
    default:
      break;
  } /* Colours/pixel switch */
} /* DoFastTable */

/*-------------------------------------------------------------------*/
#define BEEB_DOTIME_SAMPLESIZE 50

static void VideoStartOfFrame(void)
{
  static int InterlaceFrame=0;
  int CurStart;
  int IL_Multiplier;
#ifdef BEEB_DOTIME
  static int Have_GotTime=0;
  static struct tms previous,now;
  static int Time_FrameCount=0;

  double frametime;
  static CycleCountT OldCycles=0;
  
  int CurStart; 

  if (!Have_GotTime)
  {
    times(&previous);
    Time_FrameCount=-1;
    Have_GotTime=1;
  }

  if (Time_FrameCount==(BEEB_DOTIME_SAMPLESIZE-1))
  {
    times(&now);
    frametime=now.tms_utime-previous.tms_utime;
#ifndef SUNOS
#ifndef HPUX
    frametime/=(double)CLOCKS_PER_SEC;
#else
    frametime/=(double)sysconf(_SC_CLK_TCK);
#endif
#else 
    frametime/=(double)HZ;
#endif
    frametime/=(double)BEEB_DOTIME_SAMPLESIZE;
    cerr << "Frametime: " << frametime << "s fps=" << (1/frametime)
    	 << "Total cycles=" << TotalCycles << "Cycles in last unit="
	 << (TotalCycles-OldCycles) << "\n";
    OldCycles=TotalCycles;
    previous=now;
    Time_FrameCount=0;
  }
  else
    Time_FrameCount++;

#endif

#ifdef WIN32
  /* FrameNum is determined by the window handler */
  if (mainWin)
    FrameNum = mainWin->StartOfFrame();
#else
  /* If FrameNum hits 0 we actually refresh */
  if (FrameNum--==0)
  {
    FrameNum=Video_RefreshFrequency-1;
  }
#endif

  // Cursor update for blink. I thought I'd put it here, as this is where
  // the mode 7 flash field thingy is too - Richard Gellman
  CursorFieldCount--;
  if (CursorFieldCount<0)
  {
    CurStart = CRTC_CursorStart & 0x60;
    // 0 is cursor displays, but does not blink
    // 32 is no cursor
    // 64 is 1/16 fast blink
    // 96 is 1/32 slow blink
    if (CurStart==0) { CursorFieldCount=CursorOnFields; CursorOnState=1; }
    if (CurStart==32) { CursorFieldCount=CursorOffFields; CursorOnState=0; }
    if (CurStart==64) { CursorFieldCount=8; CursorOnState^=1; }
    if (CurStart==96) { CursorFieldCount=16; CursorOnState^=1; }
  }

  if (CRTC_VerticalTotalAdjust==0)
  {
    VideoState.CharLine=0;
    VideoState.InCharLine=CRTC_ScanLinesPerChar;
    VideoState.InCharLineUp=0;
  }
  else
  {
    VideoState.CharLine=-1;
    VideoState.InCharLine=CRTC_VerticalTotalAdjust;
    VideoState.InCharLineUp=0;
  }
  
  VideoState.IsTeletext=(VideoULA_ControlReg &2)>0;
  if (!VideoState.IsTeletext)
  {
    VideoState.Addr=CRTC_ScreenStartLow+(CRTC_ScreenStartHigh<<8);
  }
  else
  {
    int tmphigh=CRTC_ScreenStartHigh;
    /* undo wrangling of start address - I don't understand why this
     * should be - see p.372 of AUG for this info */
    tmphigh^=0x20;
    tmphigh+=0x74;
    tmphigh&=255;
    VideoState.Addr=CRTC_ScreenStartLow+(tmphigh<<8);

    // Modify the mode 7 flash section, for the new flash settings - R Gellman
    Mode7FlashTrigger--;
    if (Mode7FlashTrigger<0)
    {
      Mode7FlashTrigger=(Mode7FlashOn)?MODE7OFFFIELDS:MODE7ONFIELDS;
      Mode7FlashOn^=1; /* toggle flash state */
    }
  }

  InterlaceFrame^=1;
  IL_Multiplier=(CRTC_InterlaceAndDelay&1)?2:1;
  if (InterlaceFrame)
  { /* Number of 2MHz cycles until another scanline needs doing */
    IncTrigger( (IL_Multiplier*(CRTC_HorizontalTotal+1)*
    					((VideoULA_ControlReg & 16)?1:2)),
		VideoTriggerCount);
  }
  else
  { /* Number of 2MHz cycles until another scanline needs doing */
    IncTrigger( ((CRTC_HorizontalTotal+1)*((VideoULA_ControlReg & 16)?1:2)),
    		VideoTriggerCount);
  }

  //VScreenAdjust+=CRTC_VerticalTotalAdjust;
  VScreenAdjust+=(!CRTC_VerticalTotalAdjust)?1:0;
  //IncTrigger((CRTC_VerticalTotalAdjust*(CRTC_HorizontalTotal+1)*((VideoULA_ControlReg & 16)?1:2)),VideoTriggerCount);
} /* VideoStartOfFrame */

/*--------------------------------------------------------------------------*/
/* Scanline processing for modes with fast 6845 clock - i.e. narrow pixels  */
static void LowLevelDoScanLineNarrow()
{
  unsigned char *CurrentPtr;
  int BytesToGo=CRTC_HorizontalDisplayed;
  EightUChars *vidPtr=mainWin->GetLinePtr(VideoState.PixmapLine);

  /* If the step is 4 then each byte corresponds to one entry in the fasttable
   * and thus we can copy it really easily (and fast!) */
  CurrentPtr=(unsigned char *)VideoState.DataPtr+VideoState.InCharLineUp;

  /* This should help the compiler - it doesn't need to test for end of loop
     except every 4 entries */
  BytesToGo/=4;
  for(;BytesToGo;CurrentPtr+=32,BytesToGo--)
  {
    *(vidPtr++)=FastTable[*CurrentPtr];			
    *(vidPtr++)=FastTable[*(CurrentPtr+8)];		
    *(vidPtr++)=FastTable[*(CurrentPtr+16)];	
    *(vidPtr++)=FastTable[*(CurrentPtr+24)];
	//vidPtr+=4;
  }
} /* LowLevelDoScanLineNarrow() */

/*--------------------------------------------------------------------------*/
/* Scanline processing for modes with fast 6845 clock - i.e. narrow pixels  */
/* This version handles screen modes where there is not a multiple of 4     */
/* bytes per scanline.                                                      */
static void LowLevelDoScanLineNarrowNot4Bytes()
{
  unsigned char *CurrentPtr;
  int BytesToGo=CRTC_HorizontalDisplayed;
  EightUChars *vidPtr=mainWin->GetLinePtr(VideoState.PixmapLine);

  /* If the step is 4 then each byte corresponds to one entry in the fasttable
   * and thus we can copy it really easily (and fast!) */
  CurrentPtr=(unsigned char *)VideoState.DataPtr+VideoState.InCharLineUp;

  for(;BytesToGo;CurrentPtr+=8,BytesToGo--)
    (vidPtr++)->eightbyte=FastTable[*CurrentPtr].eightbyte;
} /* LowLevelDoScanLineNarrowNot4Bytes() */

/*---------------------------------------------------------------------------*/
/* Scanline processing for the low clock rate modes                          */
static void LowLevelDoScanLineWide()
{
  unsigned char *CurrentPtr;
  int BytesToGo=CRTC_HorizontalDisplayed;
  SixteenUChars *vidPtr=mainWin->GetLinePtr16(VideoState.PixmapLine);

  /* If the step is 4 then each byte corresponds to one entry in the fasttable
   * and thus we can copy it really easily (and fast!) */
  CurrentPtr=(unsigned char *)VideoState.DataPtr+VideoState.InCharLineUp;

  /* This should help the compiler - it doesn't need to test for end of loop
   * except every 4 entries */
  BytesToGo/=4;
  for(;BytesToGo;CurrentPtr+=32,BytesToGo--)
  {
    *(vidPtr++)=FastTableDWidth[*CurrentPtr];
    *(vidPtr++)=FastTableDWidth[*(CurrentPtr+8)];
    *(vidPtr++)=FastTableDWidth[*(CurrentPtr+16)];
    *(vidPtr++)=FastTableDWidth[*(CurrentPtr+24)];
  }
} /* LowLevelDoScanLineWide */

/*--------------------------------------------------------------------------*/
/* Scanline processing for the low clock rate modes                         */
/* This version handles cases where the screen width is not divisible by 4  */
static void LowLevelDoScanLineWideNot4Bytes()
{
  unsigned char *CurrentPtr;
  int BytesToGo=CRTC_HorizontalDisplayed;
  SixteenUChars *vidPtr=mainWin->GetLinePtr16(VideoState.PixmapLine);

  CurrentPtr=(unsigned char *)VideoState.DataPtr+VideoState.InCharLineUp;

  for(;BytesToGo;CurrentPtr+=8,BytesToGo--)
    *(vidPtr++)=FastTableDWidth[*CurrentPtr];
} /* LowLevelDoScanLineWideNot4Bytes */

/*--------------------------------------------------------------------------*/
/* Do all the pixel rows for one row of teletext characters                 */
static void DoMode7Row(void)
{
  char *CurrentPtr=VideoState.DataPtr;
  int CurrentChar;
  int XStep;
  unsigned char byte;
  unsigned int tmp;

  unsigned int Foreground=mainWin->cols[7];
  unsigned int ActualForeground;
  unsigned int Background=mainWin->cols[0];
  int Flash=0;		/* i.e. steady */
  int DoubleHeight=0;	/* Normal */
  int Graphics=0;	/* I.e. alpha */
  int Separated=0;	/* i.e. continuous graphics */
  int HoldGraph=0;	/* I.e. don't hold graphics */
  // What is hold graphics anyway? Nobody appears to know! Richard Gellman.
  // Is it to hold listings? With <CTRL><SH> - or to simulate Ceefax? [DMS]
  int HoldGraphChar=32; // AHA! we know what it is now.
  			// this is the character to "hold" during control codes
  unsigned int CurrentCol[20]={0xffffff,0xffffff,0xffffff,0xffffff,
  	0xffffff,0xffffff,0xffffff,0xffffff,
	0xffffff,0xffffff,0xffffff,0xffffff,
	0xffffff,0xffffff,0xffffff,0xffffff,
	0xffffff,0xffffff,0xffffff,0xffffff};
  int CurrentLen[20]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  int CurrentStartX[20]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  int CurrentScanLine;
  int CurrentX=0;
  int CurrentPixel;
  unsigned int col;
  int FontTypeIndex=0; /* 0=alpha, 1=contiguous graphics, 2=separated graphs */

  /* Not possible on beeb - and would break the double height lookup array */
  if (CRTC_HorizontalDisplayed>80)
    return;

  XStep=1;

  for(CurrentChar=0;CurrentChar<CRTC_HorizontalDisplayed;CurrentChar++)
  {
    byte=CurrentPtr[CurrentChar]; 
    if (byte<32)
      byte+=128;// fix naughty progs that use 7-bit ctrl codes - Richard Gellman
    if ((byte & 32) && (Graphics))
      HoldGraphChar=byte;
    if ((byte>=128) && (byte<=159))
    {
      switch (byte)
      {
	case 129:
        case 130:
        case 131:
        case 132:
        case 133:
        case 134:
        case 135:
          Foreground=mainWin->cols[byte-128];
          Graphics=0;
          break;
        case 136:
          Flash=1;
          break;
        case 137:
          Flash=0;
          break;
        case 140:
          DoubleHeight=0;
          break;
        case 141:
	  if (!CurrentLineBottom)
	    NextLineBottom=1;
          DoubleHeight=1;
          break;
        case 145:
        case 146:
        case 147:
        case 148:
        case 149:
        case 150:
        case 151:
          Foreground=mainWin->cols[byte-144];
          Graphics=1;
          break;
        case 152: /* Conceal display - not sure about this */
          Foreground=Background;
          break;
        case 153:
          Separated=0;
          break;
        case 154:
          Separated=1;
          break;
        case 156:
          Background=mainWin->cols[0];
          break;
        case 157:
          Background=Foreground;
          break;
        case 158:
          HoldGraph=1;
          break;
        case 159:
          HoldGraph=0;
          break;
      } /* Special character switch */
  // This next line hides any non double height characters on the bottom line
      /* Fudge so that the special character is just displayed in background */
      if ((HoldGraph==1) && (Graphics==1))
        byte=HoldGraphChar;
      else
        byte=32;
      FontTypeIndex=Graphics?(Separated?2:1):0;
    } /* test for special character */
    if ((CurrentLineBottom) && ((byte&127)>31) && (!DoubleHeight))
      byte=32;
    if ((CRTC_ScanLinesPerChar<=9) || (THalfMode))
      TeletextStyle=2;
    else
      TeletextStyle=1;
    /* Top bit never reaches character generator */
    byte&=127;
    /* Our font table goes from character 32 up */
    if (byte<32)
      byte=0;
    else
      byte-=32; 

    /* Conceal flashed text if necessary */
    ActualForeground= (Flash && !Mode7FlashOn) ? Background : Foreground;
    if (!DoubleHeight)
    {
      for(CurrentScanLine=0+(TeletextStyle-1);CurrentScanLine<20;CurrentScanLine+=TeletextStyle)
      {
        tmp=EM7Font[FontTypeIndex][byte][CurrentScanLine];
		//tmp=1365;
        if ((tmp==0) || (tmp==255))
	{
          col=(tmp==0)?Background:ActualForeground;
          if (col==CurrentCol[CurrentScanLine])
	    CurrentLen[CurrentScanLine]+=12*XStep;
	  else
	  {
            if (CurrentLen[CurrentScanLine])
              mainWin->doHorizLine(CurrentCol[CurrentScanLine],
	      				VideoState.PixmapLine+CurrentScanLine,
					CurrentStartX[CurrentScanLine],
					CurrentLen[CurrentScanLine]);
            CurrentCol[CurrentScanLine]=col;
            CurrentStartX[CurrentScanLine]=CurrentX;
            CurrentLen[CurrentScanLine]=12*XStep;
          } /* same colour */
        }
	else
	{
          for(CurrentPixel=0x800;CurrentPixel;CurrentPixel=CurrentPixel>>1)
	  {
            /* Background or foreground ? */
            col=(tmp & CurrentPixel)?ActualForeground:Background;

            /* Do we need to draw ? */
            if (col==CurrentCol[CurrentScanLine])
	      CurrentLen[CurrentScanLine]+=XStep;
	    else
	    {
              if (CurrentLen[CurrentScanLine]) 
                mainWin->doHorizLine(CurrentCol[CurrentScanLine],
					VideoState.PixmapLine+CurrentScanLine,
					CurrentStartX[CurrentScanLine],
					CurrentLen[CurrentScanLine]);
              CurrentCol[CurrentScanLine]=col;
              CurrentStartX[CurrentScanLine]=CurrentX;
              CurrentLen[CurrentScanLine]=XStep;
            } /* Fore/back ground */
            CurrentX+=XStep;
          } /* Pixel within byte */
          CurrentX-=12*XStep;
        } /* tmp!=0 */
      } /* Scanline for */
      CurrentX+=12*XStep;
      Mode7DoubleHeightFlags[CurrentChar]=1;  /* Not dbl height - so if next */
      				          /* line is dbl it will be top half */
    }
    else
    {
      int ActualScanLine;
      /* Double height! */
      for(CurrentPixel=0x800;CurrentPixel;CurrentPixel=CurrentPixel>>1)
      {
        for(CurrentScanLine=0+(TeletextStyle-1);CurrentScanLine<20;CurrentScanLine+=TeletextStyle)
	{
          if (!CurrentLineBottom)
	    ActualScanLine=CurrentScanLine >> 1;
	  else
	    ActualScanLine=10+(CurrentScanLine>>1);
          /* Background or foreground ? */
          col=(EM7Font[FontTypeIndex][byte][ActualScanLine] & CurrentPixel) ?
	  					ActualForeground : Background;

          /* Do we need to draw ? */
          if (col==CurrentCol[CurrentScanLine])
	    CurrentLen[CurrentScanLine]+=XStep;
	  else
	  {
            if (CurrentLen[CurrentScanLine])
	    {
              mainWin->doHorizLine(CurrentCol[CurrentScanLine],
	      			VideoState.PixmapLine+CurrentScanLine,
				CurrentStartX[CurrentScanLine],
				CurrentLen[CurrentScanLine]);
            }
            CurrentCol[CurrentScanLine]=col;
            CurrentStartX[CurrentScanLine]=CurrentX;
            CurrentLen[CurrentScanLine]=XStep;
          } /* Fore/back ground */
        } /* Scanline for */
        CurrentX+=XStep;
      } /* Pixel within byte */
      Mode7DoubleHeightFlags[CurrentChar]^=1; /* Not dbl height */
    }
  } /* character loop */

  /* Finish off right bits of scan line */
  for(CurrentScanLine=0+(TeletextStyle-1);CurrentScanLine<20;CurrentScanLine+=TeletextStyle)
  {
    if (CurrentLen[CurrentScanLine])
      mainWin->doHorizLine(CurrentCol[CurrentScanLine],
      			VideoState.PixmapLine+CurrentScanLine,
			CurrentStartX[CurrentScanLine],
			CurrentLen[CurrentScanLine]);
  }
  CurrentLineBottom=NextLineBottom;
  NextLineBottom=0;
} /* DoMode7Row */
/*---------------------------------------------------------------------------*/
/* Actually does the work of decoding beeb memory and plotting the line to X */
static void LowLevelDoScanLine()
{
  /* Update acceleration tables */
  DoFastTable();
  if (FastTable_Valid)
    LineRoutine();
} /* LowLevelDoScanLine */

void RedoMPTR(void)
{
  if (VideoState.IsTeletext)
    VideoState.DataPtr=BeebMemPtrWithWrapMo7(ova,ovn);
  if (!VideoState.IsTeletext)
    VideoState.DataPtr=BeebMemPtrWithWrap(ova,ovn);
  //FastTable_Valid=0;
}

/*--------------------------------------------------------------------------*/
void VideoDoScanLine(void)
{
  int movedist;
  int mempos,memdstart,memdlen;
  if (VideoState.IsTeletext)
  {
    // if (SoundLineCount>3) SoundLineCount=3;
    static int DoCA1Int=0;
    if (DoCA1Int)
    {
      SysVIATriggerCA1Int(0);
      DoCA1Int=0;
    } 

    if ((VideoState.CharLine!=-1) &&
	(VideoState.CharLine<CRTC_VerticalDisplayed))
    {
      ova=VideoState.Addr; ovn=CRTC_HorizontalDisplayed;
      VideoState.DataPtr=BeebMemPtrWithWrapMo7(VideoState.Addr,
      						CRTC_HorizontalDisplayed);
      VideoState.Addr+=CRTC_HorizontalDisplayed;
      if (!FrameNum)
        DoMode7Row();
      VideoState.PixmapLine+=20;
/*	  if (SoundLineCount--<=0) {
		  AdjustSoundCycles();
		  SoundLineCount=3;
	  } */
    }

    /* Move onto next physical scanline as far as timing is concerned */
    if (VideoState.CharLine==-1)
    {
      VideoState.InCharLine-=1;
      VideoState.InCharLineUp+=1;
    }
    else 
      VideoState.InCharLine=-1;

    if (VideoState.InCharLine<0)
    {
      VideoState.CharLine++;
      VideoState.InCharLine=CRTC_ScanLinesPerChar;
      VideoState.InCharLineUp=0;
    }


    if (VideoState.CharLine>CRTC_VerticalTotal)
    {
      int_scan=1-int_scan;
      VScreenAdjust = -80 + (((CRTC_VerticalTotal+1)-(CRTC_VerticalSyncPos-1))*
      				(20/TeletextStyle));
      AdjustVideo();
      if (!FrameNum)
      {
	VideoAddCursor();
	VideoAddLEDs();
	mainWin->updateLines(0,(500/TeletextStyle));

	/* Fill unscanned lines under picture.  Cursor will displayed on one
	 * of these lines when its on the last line of the screen so they
	 * are cleared after they are displayed, ready for the next screen. */
	/*        if (VideoState.PixmapLine<256)
	{
	  memset(mainWin->imageData()+VideoState.PixmapLine*800,
	  mainWin->cols[0], (256-VideoState.PixmapLine)*800);
	} ***************************************************************/
      }
      VideoStartOfFrame();
      VideoState.PreviousFinalPixmapLine=VideoState.PixmapLine;
      VideoState.PixmapLine=0;
      SysVIATriggerCA1Int(1);
      DoCA1Int=1;
    }
    else
    {
      if (VideoState.CharLine!=-1)
      {
        IncTrigger((CRTC_HorizontalTotal+1) * ((VideoULA_ControlReg & 16)?1:2)*
					((VideoState.CharLine==1)?9:10),
			VideoTriggerCount);
      }
      else
      {
        IncTrigger((CRTC_HorizontalTotal+1)*((VideoULA_ControlReg & 16)?1:2),
			VideoTriggerCount);
      }
    }
  }
  else // else block for: if (teletext)
  {
     /* Non teletext */
    if (VideoState.CharLine!=-1)
    {
      if (VideoState.CharLine<CRTC_VerticalDisplayed)
      {
	/* If first row of character then get the data pointer from memory */
	if (VideoState.InCharLine==CRTC_ScanLinesPerChar)
	{
	  ova=VideoState.Addr*8; ovn=CRTC_HorizontalDisplayed*8;
	  VideoState.DataPtr=BeebMemPtrWithWrap(VideoState.Addr*8,
					       CRTC_HorizontalDisplayed*8);
	  VideoState.Addr+=CRTC_HorizontalDisplayed;
	}
 
	if ( ((VideoState.InCharLine) > (CRTC_ScanLinesPerChar-8)) &&
	     ((CRTC_InterlaceAndDelay & 0x30)!=48) )
	{ 
	  if (!FrameNum)
	    LowLevelDoScanLine();
	  VideoState.PixmapLine+=1;
	}
	else
	{
	  //if (!FrameNum) mainWin->doHorizLine(mainWin->cols[0],VideoState.PixmapLine++,0,640);
	  VideoState.PixmapLine++;
	}
      }
    }
    else
    {
      //if (!FrameNum) mainWin->doHorizLine(mainWin->cols[0],VideoState.PixmapLine++,0,640);
      VideoState.PixmapLine++;
    }
	
    /* Move onto next physical scanline as far as timing is concerned */
    VideoState.InCharLine-=1;
    VideoState.InCharLineUp+=1;
    if (VideoState.VSyncState)
    {
      if (!(--VideoState.VSyncState))
      {
        SysVIATriggerCA1Int(0);
      }
    }

    if (InPic)
    {
      VScreenAdjust++;
      if (VSyncCount==0)
      {
	SysVIATriggerCA1Int(1);
	VideoState.VSyncState=2;
	VSyncCount=-1;
      } 
      if (VSyncCount>0)
      {
	VSyncCount--;
      }
    }


    if (VideoState.InCharLine<0)
    {
      VideoState.CharLine++;
      /* Suspect the -1 in sync pos is a fudge factor - careful! - DAG */
      /* This has been taken out now */
      if ( (VideoState.VSyncState==0) &&
	   (VideoState.CharLine==(CRTC_VerticalSyncPos)))
     {
	/* Fill unscanned lines under picture */
/*         if (VideoState.PixmapLine<VideoState.PreviousFinalPixmapLine)
       {
	  int CurrentLine;
	  for(CurrentLine=VideoState.PixmapLine;
	  (CurrentLine<256) && (CurrentLine<VideoState.PreviousFinalPixmapLine);
	      CurrentLine++)
	  {
	    mainWin->doHorizLine(mainWin->cols[0],CurrentLine,0,800);
	  }
	} **************************************************************/
	VideoState.PreviousFinalPixmapLine=VideoState.PixmapLine;
	VideoState.PixmapLine=0;
	InPic=1; VSyncCount=1;
	VScreenAdjust=-40;
	int_scan=1-int_scan;
      }

      VideoState.InCharLine=CRTC_ScanLinesPerChar;
      VideoState.InCharLineUp=0;
    }

    if (VideoState.CharLine>CRTC_VerticalTotal)
    {
      if (!FrameNum)
      {
        VideoAddCursor();
	VideoAddLEDs();
        mainWin->updateLines(0,256);
      }
      VideoStartOfFrame();
      InPic=0; AdjustVideo();
    }
    else
    {
      IncTrigger((CRTC_HorizontalTotal+1)*((VideoULA_ControlReg & 16)?1:2),
      		VideoTriggerCount);
    }
  } /* (else block) Teletext if */
} /* VideoDoScanLine */

void AdjustVideo()
{
  InitialOffset=0-(((CRTC_HorizontalTotal+1)/2)-((HSyncModifier==8)?40:20));
  HStart = InitialOffset +
  	((CRTC_HorizontalTotal+1)-(CRTC_HorizontalSyncPos+(CRTC_SyncWidth&15)));
  HStart += (HSyncModifier==8)?2:1;
  if (TeletextEnabled)
    HStart+=2;

  // Having sorted the horizontal component [Richard] 
  // goes on to swear at the vertical section. P.S. the
  // author of Uridium is gonna die for that sneaky
  // double the dot clock'd mode 5 trick....
  // I removed the vertical bit coz its not needed no more
  if (HStart<LSL)
    HStart=LSL;
  ScreenAdjust=(HStart*HSyncModifier)+((VScreenAdjust>0)?(VScreenAdjust*640):0);
}
/*--------------------------------------------------------------------------*/
void VideoInit(void)
{
//  char *environptr;
  VideoStartOfFrame();
  ova=0x3000; ovn=640;
  VideoState.DataPtr=BeebMemPtrWithWrap(0x3000,640);
  SetTrigger(99,VideoTriggerCount); /* Put a time delay in to allow the OS */
			/* to set up the Mode before the video kicks in. */ 
  FastTable_Valid=0;
  BuildMode7Font();

#ifndef WIN32
  environptr=getenv("BeebVideoRefreshFreq");
  if (environptr!=NULL)
    Video_RefreshFrequency=atoi(environptr);
  if (Video_RefreshFrequency<1)
    Video_RefreshFrequency=1;
#endif

  FrameNum=Video_RefreshFrequency;
  VideoState.PixmapLine=0;
  VideoState.PreviousFinalPixmapLine=255;
  //AdjustVideo();
} /* VideoInit */

/*--------------------------------------------------------------------------*/
void CRTCWrite(int Address, int Value) {
  Value&=0xff;
  if (Address & 1)
  {
    switch (CRTCControlReg)
    {
      case 0:
        CRTC_HorizontalTotal=Value;
	InitialOffset = 0-
		(((CRTC_HorizontalTotal+1)/2)-((HSyncModifier==8)?40:20));
	AdjustVideo();
        break;
      case 1:
        CRTC_HorizontalDisplayed=Value;
        FastTable_Valid=0;
	AdjustVideo();
	break;
      case 2:
	CRTC_HorizontalSyncPos=Value;
	AdjustVideo();
        break;
      case 3:
        CRTC_SyncWidth=Value;
	AdjustVideo();
        break;
      case 4:
        CRTC_VerticalTotal=Value;
	AdjustVideo();
        break;
      case 5:
        CRTC_VerticalTotalAdjust=Value;
	AdjustVideo();
        break;
      case 6:
        CRTC_VerticalDisplayed=Value;
	AdjustVideo();
	break;
      case 7:
        CRTC_VerticalSyncPos=Value;
        AdjustVideo();
	break;
      case 8:
        CRTC_InterlaceAndDelay=Value;
        break;
      case 9:
        CRTC_ScanLinesPerChar=Value;
	AdjustVideo();
        break;
      case 10:
        CRTC_CursorStart=Value;
        break;
      case 11:
        CRTC_CursorEnd=Value;
        break;
      case 12:
        CRTC_ScreenStartHigh=Value;
        break;
      case 13:
        CRTC_ScreenStartLow=Value;
        break;
      case 14:
        CRTC_CursorPosHigh=Value & 0x3f; /* Cursor high only has 6 bits */
        break;
      case 15:
        CRTC_CursorPosLow=Value & 0xff;
        break;
      default: /* In case the user wrote a duff control register value */
        break;
    } /* CRTCWrite switch */
  }
  else
  {
    CRTCControlReg=Value & 0x1f;
  }
} /* CRTCWrite */

/*--------------------------------------------------------------------------*/
int CRTCRead(int Address)
{
  if (Address & 1)
  {
    switch (CRTCControlReg)
    {
      case 14:
        return(CRTC_CursorPosHigh);
      case 15:
        return(CRTC_CursorPosLow);
      case 16:
        return(CRTC_LightPenHigh); /* Perhaps tie to mouse pointer ? */
      case 17:
        return(CRTC_LightPenLow);
      default:
        break;
    } /* CRTC Read switch */
  }
  else
  {
    return(0);  /* Rockwell part has bits 5,6,7 used */
		/* bit 6 is set when LPEN recv, bit 5 when in vert retrace */
  }
  return(0);	// Keeep MSVC happy $NRM
} /* CRTCRead */

/*--------------------------------------------------------------------------*/
void VideoULAWrite(int Address, int Value)
{
  int oldValue;
  if (Address & 1)
  {
    VideoULA_Palette[(Value & 0xf0)>>4]=(Value & 0xf) ^ 7;
    FastTable_Valid=0;
  }
  else
  {
    oldValue=VideoULA_ControlReg;
    VideoULA_ControlReg=Value;
    FastTable_Valid=0; /* Could choose to only do when no.of.cols bit changes */
    // Adjust HSyncModifier
    if (VideoULA_ControlReg & 16)
      HSyncModifier=8;
    else
      HSyncModifier=16;
    if (VideoULA_ControlReg & 2)
      HSyncModifier=12;
    // number of pixels per CRTC character (on our screen)
    if (Value & 2)
      TeletextEnabled=1;
    else
      TeletextEnabled=0;
    if ((Value&2)^(oldValue&2))
    {
      ScreenAdjust=0; LSL=0;
    }
    AdjustVideo();
  }
} /* VidULAWrite */

/*---------------------------------------------------------------------*/
int VideoULARead(int Address)
{
  return(Address); /* Read not defined from Video ULA */
} /* VidULARead */

/*---------------------------------------------------------------------*/
static void VideoAddCursor(void)
{
  static int CurSizes[] = { 2,1,0,0,4,2,0,4 };
  int ScrAddr,CurAddr,RelAddr;
  int CurX,CurY;
  int CurSize;
  int CurStart, CurEnd;

  /* Check if cursor has been hidden */
  if ((VideoULA_ControlReg & 0xe0) == 0 || (CRTC_CursorStart & 0x40) == 0)
    return;

  /* Use clock bit and cursor bits to work out size */
  if (VideoULA_ControlReg & 0x80)
    CurSize = CurSizes[(VideoULA_ControlReg & 0x70)>>4] * 8;
  else
    CurSize = 2 * 8; /* Mode 7 */

  if (VideoState.IsTeletext)
  {
    ScrAddr = CRTC_ScreenStartLow +
    		(((CRTC_ScreenStartHigh ^ 0x20) + 0x74 & 0xff)<<8);
    CurAddr = CRTC_CursorPosLow +
    		(((CRTC_CursorPosHigh ^ 0x20) + 0x74 & 0xff)<<8);

    CurStart = (CRTC_CursorStart & 0x1f) / 2;
    CurEnd = CRTC_CursorEnd ;
    CurSize-=4;
  }
  else
  {
    ScrAddr=CRTC_ScreenStartLow+(CRTC_ScreenStartHigh<<8);
    CurAddr=CRTC_CursorPosLow+(CRTC_CursorPosHigh<<8);

    CurStart = CRTC_CursorStart & 0x1f;
    CurEnd = CRTC_CursorEnd;
  }
	  
  RelAddr=CurAddr-ScrAddr;
  if (RelAddr < 0 || CRTC_HorizontalDisplayed == 0)
    return;

  /* Work out char positions */
  CurX = RelAddr % CRTC_HorizontalDisplayed;
  CurY = RelAddr / CRTC_HorizontalDisplayed;

  /* Convert to pixel positions */
  CurX = CurX*((VideoState.IsTeletext)?12:HSyncModifier);
  CurY = CurY * (VideoState.IsTeletext ? 20 :  (CRTC_ScanLinesPerChar + 1));
  if (!(VideoState.IsTeletext))
    CurY+=CRTC_VerticalTotalAdjust;
  if (VideoState.IsTeletext)
    CurY+=9;
  /* Limit cursor size */ // This should be 11, not 9 - Richard Gellman
  if (CurEnd > 11)
    CurEnd = 11;

  if (CurX + CurSize >= 640)
    CurSize = 640 - CurX;
  // Cursor delay
  CurX+=((CRTC_InterlaceAndDelay&192)>>6)*HSyncModifier;
  if (VideoState.IsTeletext)
    CurX-=2*HSyncModifier;
  if (CurSize > 0)
  {
    for (int y = CurStart; y <= CurEnd && CurY + y < 500; ++y)
    {
      if (CurY + y >= 0)
      {
	if (CursorOnState)
	  mainWin->doInvHorizLine(mainWin->cols[7], CurY + y, CurX, CurSize);
      }
    }
  }
}

void VideoAddLEDs(void)
{
  // now add some keyboard leds
  if (LEDs.ShowKB)
  {
    if (MachineType==1)
      mainWin->doLED(4,TRUE);
    else
      mainWin->doLED(4,LEDs.Motor);
    mainWin->doLED(14,LEDs.CapsLock);
    mainWin->doLED(24,LEDs.ShiftLock);
  }
  if (LEDs.ShowDisc)
  {
    mainWin->doLED((TeletextEnabled)?532:618,LEDs.Disc0);
    mainWin->doLED((TeletextEnabled)?542:628,LEDs.Disc1);
  }
}

/*-------------------------------------------------------------------------*/
void SaveVideoState(unsigned char *StateData)
{
  /* 6845 state */
  StateData[0] = CRTCControlReg;
  StateData[1] = CRTC_HorizontalTotal;
  StateData[2] = CRTC_HorizontalDisplayed;
  StateData[3] = CRTC_HorizontalSyncPos;
  StateData[4] = CRTC_SyncWidth;
  StateData[5] = CRTC_VerticalTotal;
  StateData[6] = CRTC_VerticalTotalAdjust;
  StateData[7] = CRTC_VerticalDisplayed;
  StateData[8] = CRTC_VerticalSyncPos;
  StateData[9] = CRTC_InterlaceAndDelay;
  StateData[10] = CRTC_ScanLinesPerChar;
  StateData[11] = CRTC_CursorStart;
  StateData[12] = CRTC_CursorEnd;
  StateData[13] = CRTC_ScreenStartHigh;
  StateData[14] = CRTC_ScreenStartLow;
  StateData[15] = CRTC_CursorPosHigh;
  StateData[16] = CRTC_CursorPosLow;
  StateData[17] = CRTC_LightPenHigh;
  StateData[18] = CRTC_LightPenLow;

  /* Video ULA state */
  StateData[32] = VideoULA_ControlReg;
  for (int col = 0; col < 16; ++col)
    StateData[33+col] = VideoULA_Palette[col] ^ 7; /* Use real ULA values */
}

void SaveVideoUEF(FILE *SUEF)
{
  fput16(0x0468,SUEF);
  fput32(47,SUEF);
  // save the registers now
  fputc(CRTC_HorizontalTotal,SUEF);
  fputc(CRTC_HorizontalDisplayed,SUEF);
  fputc(CRTC_HorizontalSyncPos,SUEF);
  fputc(CRTC_SyncWidth,SUEF);
  fputc(CRTC_VerticalTotal,SUEF);
  fputc(CRTC_VerticalTotalAdjust,SUEF);
  fputc(CRTC_VerticalDisplayed,SUEF);
  fputc(CRTC_VerticalSyncPos,SUEF);
  fputc(CRTC_InterlaceAndDelay,SUEF);
  fputc(CRTC_ScanLinesPerChar,SUEF);
  fputc(CRTC_CursorStart,SUEF);
  fputc(CRTC_CursorEnd,SUEF);
  fputc(CRTC_ScreenStartHigh,SUEF);
  fputc(CRTC_ScreenStartLow,SUEF);
  fputc(CRTC_CursorPosHigh,SUEF);
  fputc(CRTC_CursorPosLow,SUEF);
  fputc(CRTC_LightPenHigh,SUEF);
  fputc(CRTC_LightPenLow,SUEF);
  // VIDPROC
  fputc(VideoULA_ControlReg,SUEF);
  for (int col = 0; col < 16; ++col)
    fputc(VideoULA_Palette[col] ^ 7,SUEF); /* Use real ULA values */
  fput16(ActualScreenWidth,SUEF);
  fput32(ScreenAdjust,SUEF);
  fputc(CRTCControlReg,SUEF);
  fputc(TeletextStyle,SUEF);
  fput32(0,SUEF); // Pad out
}

void LoadVideoUEF(FILE *SUEF)
{
  CRTC_HorizontalTotal=fgetc(SUEF);
  CRTC_HorizontalDisplayed=fgetc(SUEF);
  CRTC_HorizontalSyncPos=fgetc(SUEF);
  CRTC_SyncWidth=fgetc(SUEF);
  CRTC_VerticalTotal=fgetc(SUEF);
  CRTC_VerticalTotalAdjust=fgetc(SUEF);
  CRTC_VerticalDisplayed=fgetc(SUEF);
  CRTC_VerticalSyncPos=fgetc(SUEF);
  CRTC_InterlaceAndDelay=fgetc(SUEF);
  CRTC_ScanLinesPerChar=fgetc(SUEF);
  CRTC_CursorStart=fgetc(SUEF);
  CRTC_CursorEnd=fgetc(SUEF);
  CRTC_ScreenStartHigh=fgetc(SUEF);
  CRTC_ScreenStartLow=fgetc(SUEF);
  CRTC_CursorPosHigh=fgetc(SUEF);
  CRTC_CursorPosLow=fgetc(SUEF);
  CRTC_LightPenHigh=fgetc(SUEF);
  CRTC_LightPenLow=fgetc(SUEF);
  // VIDPROC
  VideoULA_ControlReg=fgetc(SUEF);
  for (int col = 0; col < 16; ++col)
    VideoULA_Palette[col]=fgetc(SUEF)^7; /* Use real ULA values */
  ActualScreenWidth=fget16(SUEF);
  ScreenAdjust=fget32(SUEF);
  CRTCControlReg=fgetc(SUEF);
  TeletextStyle=fgetc(SUEF);
  if (VideoULA_ControlReg & 2)
    TeletextEnabled=1;
  else TeletextEnabled=0;
  VideoInit();
  //SetTrigger(99,VideoTriggerCount);
}

/*-------------------------------------------------------------------------*/
void RestoreVideoState(unsigned char *StateData)
{
  /* 6845 state */
  CRTCControlReg = StateData[0];
  CRTC_HorizontalTotal = StateData[1];
  CRTC_HorizontalDisplayed = StateData[2];
  CRTC_HorizontalSyncPos = StateData[3];
  CRTC_SyncWidth = StateData[4];
  CRTC_VerticalTotal = StateData[5];
  CRTC_VerticalTotalAdjust = StateData[6];
  CRTC_VerticalDisplayed = StateData[7];
  CRTC_VerticalSyncPos = StateData[8];
  CRTC_InterlaceAndDelay = StateData[9];
  CRTC_ScanLinesPerChar = StateData[10];
  CRTC_CursorStart = StateData[11];
  CRTC_CursorEnd = StateData[12];
  CRTC_ScreenStartHigh = StateData[13];
  CRTC_ScreenStartLow = StateData[14];
  CRTC_CursorPosHigh = StateData[15];
  CRTC_CursorPosLow = StateData[16];
  CRTC_LightPenHigh = StateData[17];
  CRTC_LightPenLow = StateData[18];

  /* Video ULA state */
  VideoULA_ControlReg = StateData[32];
  for (int col = 0; col < 16; ++col)
    VideoULA_Palette[col] = StateData[33+col] ^ 7; /*Convert ULA vals to cols*/

  /* Reset the other video state variables */
  VideoInit();
}

/*--------------------------------------------------------------------------*/
void video_dumpstate(void)
{
  int tmp;
  cerr << "video:\n";
  cerr << "  VideoULA_ControlReg=" << int(VideoULA_ControlReg) << "\n";
  cerr << "  VideoULA_Palette=";
  for(tmp=0;tmp<16;tmp++)
    cerr << int(VideoULA_Palette[tmp]) << " ";
  cerr << "\n  CRTC Control=" << int(CRTCControlReg) << "\n";
  cerr << "  CRTC_HorizontalTotal=" << int(CRTC_HorizontalTotal) << "\n";
  cerr << "  CRTC_HorizontalDisplayed=" << int(CRTC_HorizontalDisplayed)<< "\n";
  cerr << "  CRTC_HorizontalSyncPos=" << int(CRTC_HorizontalSyncPos)<< "\n";
  cerr << "  CRTC_SyncWidth=" << int(CRTC_SyncWidth)<< "\n";
  cerr << "  CRTC_VerticalTotal=" << int(CRTC_VerticalTotal)<< "\n";
  cerr << "  CRTC_VerticalTotalAdjust=" << int(CRTC_VerticalTotalAdjust)<< "\n";
  cerr << "  CRTC_VerticalDisplayed=" << int(CRTC_VerticalDisplayed)<< "\n";
  cerr << "  CRTC_VerticalSyncPos=" << int(CRTC_VerticalSyncPos)<< "\n";
  cerr << "  CRTC_InterlaceAndDelay=" << int(CRTC_InterlaceAndDelay)<< "\n";
  cerr << "  CRTC_ScanLinesPerChar=" << int(CRTC_ScanLinesPerChar)<< "\n";
  cerr << "  CRTC_CursorStart=" << int(CRTC_CursorStart)<< "\n";
  cerr << "  CRTC_CursorEnd=" << int(CRTC_CursorEnd)<< "\n";
  cerr << "  CRTC_ScreenStartHigh=" << int(CRTC_ScreenStartHigh)<< "\n";
  cerr << "  CRTC_ScreenStartLow=" << int(CRTC_ScreenStartLow)<< "\n";
  cerr << "  CRTC_CursorPosHigh=" << int(CRTC_CursorPosHigh)<< "\n";
  cerr << "  CRTC_CursorPosLow=" << int(CRTC_CursorPosLow)<< "\n";
  cerr << "  CRTC_LightPenHigh=" << int(CRTC_LightPenHigh)<< "\n";
  cerr << "  CRTC_LightPenLow=" << int(CRTC_LightPenLow)<< "\n";
} /* video_dumpstate */

