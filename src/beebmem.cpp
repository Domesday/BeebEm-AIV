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
/* Beebemulator - memory subsystem - David Alan Gilbert 16/10/94 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "iostream.h"

#include "6502core.h"
#include "main.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"
#include "beebmem.h"
#include "disc1770.h"
#include "serial.h"
#include "tube.h"
#include "errno.h"
#include "uefstate.h"
#include "aivscsi.h"
#include "debugLogs.h"

#ifdef USE_FDC2_LOG
FILE *fdclog2;
#endif

int WritableRoms = 0;

/* Each Rom now has a Ram/Rom flag */
int RomWritable[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

int PagedRomReg;

static int RomModified=0; /* Rom changed - needs copying back */
static int SWRamModified=0; /* SW Ram changed - needs saving and restoring */
unsigned char WholeRam[65536];
unsigned char Roms[16][16384];
unsigned char ROMSEL;
/* Master 128 Specific Stuff */
unsigned char FSRam[12228]; // 12K Filing System RAM
unsigned char PrivateRAM[4096]; // 4K Private RAM (VDU Use mainly)
int CMOSRAM[64]; // 50 Bytes CMOS RAM
int CMOSDefault[64]={0,0,0,0,0,0xc9,0xff,0xfe,0x32,0,7,0xc1,0x1e,5,0,0x58,0xa2}; // Backup of CMOS Defaults
unsigned char ShadowRAM[32768]; // 20K Shadow RAM
unsigned char MOSROM[16384]; // 12K MOS Store for swapping FS ram in and out
unsigned char ACCCON; // ACCess CONtrol register
unsigned char UseShadow; // 1 to use shadow ram, 0 to use main ram
unsigned char MainRAM[32768]; // Main RAM temporary store when using shadow RAM
// ShadowRAM and MainRAM have to be 32K for reasons to do with addressing
struct CMOSType CMOS;
unsigned char Sh_Display,Sh_CPUX,Sh_CPUE,PRAM,FRAM;
/* End of Master 128 Specific Stuff, note initilised anyway regardless of Model Type in use */
char RomPath[512];
// FDD Extension board variables
int EFDCAddr; // 1770 FDC location
int EDCAddr; // Drive control location
bool NativeFDC; // TRUE for 8271, FALSE for DLL extension

/*----------------------------------------------------------------------------*/
/* Perform hardware address wrap around */
static unsigned int WrapAddr(int in) {
  unsigned int offsets[]={0x4000,0x6000,0x3000,0x5800};
  if (in<0x8000) return(in);
  in+=offsets[(IC32State & 0x30)>>4];
  in&=0x7fff;
  return(in);
}; /* WrapAddr */

/*----------------------------------------------------------------------------*/
/* This is for the use of the video routines.  It returns a pointer to
   a continuous area of 'n' bytes containing the contents of the
   'n' bytes of beeb memory starting at address 'a', with wrap around
   at 0x8000.  Potentially this routine may return a pointer into  a static
   buffer - so use the contents before recalling it
   'n' must be less than 1K in length.
   See 'BeebMemPtrWithWrapMo7' for use in Mode 7 - its a special case.
*/

char *BeebMemPtrWithWrap(int a, int n) {
  unsigned char NeedShadow=0; // 0 to read WholeRam, 1 to read ShadowRAM ; 2 to read MainRAM
  static char tmpBuf[1024];
  char *tmpBufPtr;
  int EndAddr=a+n-1;
  int toCopy;

  a=WrapAddr(a);
  EndAddr=WrapAddr(EndAddr);

  if (a<=EndAddr && Sh_Display==0) {
    return((char *)WholeRam+a);
  };
  if (a<=EndAddr && Sh_Display>0) {
    return((char *)ShadowRAM+a);
  };

  toCopy=0x8000-a;
  if (toCopy>n) toCopy=n;
  if (toCopy>0 && Sh_Display==0) memcpy(tmpBuf,WholeRam+a,toCopy);
  if (toCopy>0 && Sh_Display>0) memcpy(tmpBuf,ShadowRAM+a,toCopy);
  tmpBufPtr=tmpBuf+toCopy;
  toCopy=n-toCopy;
  if (toCopy>0 && Sh_Display==0) memcpy(tmpBufPtr,WholeRam+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */
  if (toCopy>0 && Sh_Display>0) memcpy(tmpBufPtr,ShadowRAM+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */
  // Tripling is for Shadow RAM handling
  return(tmpBuf);
}; /* BeebMemPtrWithWrap */

/*----------------------------------------------------------------------------*/
/* Perform hardware address wrap around - for mode 7*/
static unsigned int WrapAddrMo7(int in) {
  if (in<0x8000) return(in);
  in+=0x7c00;
  in&=0x7fff;
  return(in);
}; /* WrapAddrMo7 */

/*----------------------------------------------------------------------------*/
/* Special case of BeebMemPtrWithWrap for use in mode 7
*/

char *BeebMemPtrWithWrapMo7(int a, int n) {
  static char tmpBuf[1024];
  char *tmpBufPtr;
  int EndAddr=a+n-1;
  int toCopy;

  a=WrapAddrMo7(a);
  EndAddr=WrapAddrMo7(EndAddr);

  if (a<=EndAddr && Sh_Display==0) {
    return((char *)WholeRam+a);
  };
  if (a<=EndAddr && Sh_Display>0) {
    return((char *)ShadowRAM+a);
  };

  toCopy=0x8000-a;
  if (toCopy>n && Sh_Display==0) return((char *)WholeRam+a);
  if (toCopy>n && Sh_Display>0) return((char *)ShadowRAM+a);
  if (toCopy>0 && Sh_Display==0) memcpy(tmpBuf,WholeRam+a,toCopy);
  if (toCopy>0 && Sh_Display>0) memcpy(tmpBuf,ShadowRAM+a,toCopy);
  tmpBufPtr=tmpBuf+toCopy;
  toCopy=n-toCopy;
  if (toCopy>0 && Sh_Display==0) memcpy(tmpBufPtr,WholeRam+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */
  if (toCopy>0 && Sh_Display>0) memcpy(tmpBufPtr,ShadowRAM+EndAddr-(toCopy-1),toCopy); /* Should that -1 be there ? */
  return(tmpBuf);
}; /* BeebMemPtrWithWrapMo7 */

/*----------------------------------------------------------------------------*/
int BeebReadMem(int Address)
{
  static int extracycleprompt=0;

  /* This function assumes that the caller has checked to see if the address
   * is below 0xFC00. (If so, assumes that the caller does a direct read)
   */
#ifdef USE_FDC2_LOG
  // Just as a precaution, while logging is enabled, double check
  if (Address < 0xFC00)
  {
    fprintf(fdclog2, "BeebReadMem: Expected Addr > FC00, addr=%04X\n", Address);
    fflush(fdclog2);
  }
#endif
  if (Address >= 0xff00)
    return(WholeRam[Address]);

  /* When Addr is in the range 0xFC00 - 0xFEFF, this is a read from I/O mem */
  /* Need to check the system devices */

  /* Check the VIA's first - games seem to do really heavy reading of these */
  /* Can read from a via using either of the two 16 bytes blocks */
  if ((Address & ~0xf)==0xfe40 || (Address & ~0xf)==0xfe50)
    return(SysVIARead(Address & 0xf));
  if ((Address & ~0xf)==0xfe60 || (Address & ~0xf)==0xfe70)
    return(UserVIARead(Address & 0xf));
  if ((Address & ~7)==0xfe00)
    return(CRTCRead(Address & 0x7));
  if ((Address & ~3)==0xfe20)
    return(VideoULARead(Address & 0xf)); // Master uses fe24 to fe2f for FDC
  if ((Address & ~3)==0xfe30)
    return(PagedRomReg); /* report back ROMSEL - I'm sure the beeb allows */
               /* ROMSEL read.. correct me if im wrong. - Richard Gellman */
  if ((Address & ~3)==0xfe34 && MachineType==1)
    return(ACCCON);

  // In the Master at least, ROMSEL/ACCCON seem to be duplicated
  // over a 4 byte block.
  if ((Address & ~7)==0xfe28 && MachineType==1)
    return(Read1770Register(Address & 0x7));
  if (Address==0xfe24 && MachineType==1)
    return(ReadFDCControlReg());
  if ((Address & ~0x1f)==0xfea0)
    return(0xfe); /* Disable econet */
  if ((Address>=0xfee0) && (Address<0xfef0))
    return(ReadTubeFromHostSide(Address&7)); //Read From Tube

  // Tube seems to return FF on a master (?)
  if (Address==0xfe08)
    return(Read_ACIA_Status());
  if (Address==0xfe09)
    return(Read_ACIA_Rx_Data());
  if (Address==0xfe10)
    return(Read_SERPROC());
  if ((Address>=0xfe80) && (Address<=0xfe83))
    return(SCSI_Read(Address&3));
  return(0);
} /* BeebReadMem */

/*-------------------------------------------------------------------------*/
static void DoRomChange(int NewBank)
{
  /* Speed up hack - if we are switching to the same rom, then don't bother */
  ROMSEL=NewBank&0xf;
  PagedRomReg=NewBank;
  PRAM=(PagedRomReg & 128);
} /* DoRomChange */

/*-------------------------------------------------------------------------*/
static void FiddleACCCON(unsigned char newValue) {
	// Master specific, should only execute in Master128 mode
	// ignore bits TST (6) IFJ (5) and ITU (4)
	newValue&=143;
	unsigned char oldshd;
	if ((newValue & 128)==128) intStatus|=1<<Iaccon; else intStatus&=~(1<<Iaccon);
	ACCCON=newValue & 127; // mask out the IRR bit so that interrupts dont occur repeatedly
	//if (newValue & 128) intStatus|=128; else intStatus&=127;
	oldshd=Sh_Display;
	Sh_Display=ACCCON & 1;
	if (Sh_Display!=oldshd) RedoMPTR();
	Sh_CPUX=ACCCON & 4;
	Sh_CPUE=ACCCON & 2;
	FRAM=ACCCON & 8;
}

/*----------------------------------------------------------------------------*/
void BeebWriteMem(int Address, int Value)
{
  static int extracycleprompt=0;
/*  fprintf(stderr,"Write %x to 0x%x\n",Value,Address); */

  /* Now we presume that the caller has validated the address as
   * being within main ram and hence the following line is not required */
  if (Address<0x8000)
  {
    WholeRam[Address]=Value;
    return;
  }
  // See dmsDomesday/src/beebmem.cpp for the older code

  if ((Address>=0xfc00) && (Address<=0xfeff))
  {
    /* Check for some hardware */
    if ((Address & ~0x3)==0xfe20)
    {
      VideoULAWrite(Address & 0xf, Value);
      return;
    }

    /* Can write to a via using either of the two 16 bytes blocks */
    if ((Address & ~0xf)==0xfe40 || (Address & ~0xf)==0xfe50)
    {
      SysVIAWrite((Address & 0xf),Value);
      Cycles++;
      return;
    }

    /* Can write to a via using either of the two 16 bytes blocks */
    if ((Address & ~0xf)==0xfe60 || (Address & ~0xf)==0xfe70)
    {
      UserVIAWrite((Address & 0xf),Value);
      Cycles++;
      return;
    }

    if (Address>=0xfe30 && Address<0xfe34)
    {
      DoRomChange(Value);
      return;
    }

    if (Address>=0xfe34 && Address<0xfe38 && MachineType==1)
    {
      FiddleACCCON(Value);
      return;
    }
    // In the Master at least, ROMSEL/ACCCON seem to be duplicated over
    // a 4 byte block.
    if ((Address & ~0x7)==0xfe00)
    {
      CRTCWrite(Address & 0x7, Value);
      return;
    }

    if ((Address & ~0x7)==0xfe28 && MachineType==1)
    {
      Write1770Register(Address & 7,Value);
      return;
    } 

    if (Address==0xfe24 && MachineType==1)
    {
      WriteFDCControlReg(Value);
      return;
    }

    if (Address==0xfe08)
      Write_ACIA_Control(Value);
    if (Address==0xfe09)
      Write_ACIA_Tx_Data(Value);
    if (Address==0xfe10)
      Write_SERPROC(Value);
    if ((Address&~0x1f)==0xfee0)
      WriteTubeFromHostSide(Address&7,Value);

    if ((Address>=0xfe80) && (Address<=0xfe83))
      SCSI_Write(Address&3,Value);

    return;
  }
} // end of : void BeebWriteMem(int Address, int Value)

/*---------------------------------------------------------------------------*/
// ReadRom was replaced with BeebReadRoms.
/*---------------------------------------------------------------------------*/
char *ReadRomTitle( int bank, char *Title, int BufSize )
{
	int i;

	// Copy the ROM Title to the Buffer
	for( i=0; (( i<(BufSize-1)) && ( Roms[bank][i+9]>30)); i++ )
		Title[i] = Roms[bank][i+9];

	// Add terminating NULL.
	Title[i] = '\0';

	return Title;
}

/*----------------------------------------------------------------------------*/
void BeebReadRoms(void)
{
  FILE *InFile,*RomCfg;
  char TmpPath[256];
  char fullname[256];
  int romslot = 0xf;
  char RomNameBuf[80];
  char *RomName=RomNameBuf;
  unsigned char sc,isrom;
  unsigned char Shortener=1; // Amount to shorten filename by

  /* Read all ROM files in the beebfile directory */
  // This section rewritten for V.1.32 to take account of roms.cfg file.
  strcpy(TmpPath, RomPath);
  strcat(TmpPath, "Roms.cfg");
  RomCfg=fopen(TmpPath, "rt");

  // Take out the code that duplicates "Example.cfg" to "Roms.cfg"
  // This is certainly not needed for the BBCDomesEm demo

  if (RomCfg!=NULL)
  {
    // The Rom config file is open properly. Need to then read the correct
    // roms into the correct part of the Beeb's memory.

    // The BBC B roms are listed first in the file, the Master 128 are
    // then listed.

    // MachineType==1 (i.e. Master 128) we need to skip the 17 lines for BBC B
    if (MachineType==1)
    {
      for (romslot=0;romslot<17;romslot++)
      {
        fgets(RomName,80,RomCfg);
      }
    }

    // The first Rom to read is the Operating System Rom.
    fgets(RomName, 80, RomCfg);
    strcpy(fullname, RomName);
    if ((RomName[0]!='\\') && (RomName[1]!=':'))
    {
      strcpy(fullname,RomPath);
      strcat(fullname,"/beebfile/");
      strcat(fullname,RomName);
    }

    // Make sure that there are no '\' chars in the file name, so that the
    // Roms.cfg file will work on both Unix and Win
    for (sc = 0; fullname[sc]; sc++)
    {
      if (fullname[sc] == '\\')
        fullname[sc] = '/';
    }
    fullname[strlen(fullname)-1]=0;	// Strip off the newline (0x0A) char
    InFile=fopen(fullname,"rb");
    if (InFile!=NULL)
    {
      fread(WholeRam + STD_OS_LDADDR, 1, STD_ROM_SIZE, InFile);
      fclose(InFile);
    }
    else
    {
      char errstr[200];
      sprintf(errstr, "Cannot open specified OS ROM:\n %s",fullname);
      MessageBox(GETHWND, errstr, "BBC Emulator", MB_OK|MB_ICONERROR);
    }

    // Read in the 16 paged ROMs
    for (romslot=15;romslot>=0;romslot--)
    {
      fgets(RomName, 80, RomCfg);
      strcpy(fullname, RomName);
      if ((RomName[0]!='\\') && (RomName[1]!=':'))
      {
	strcpy(fullname,RomPath);
	strcat(fullname,"/beebfile/");
	strcat(fullname,RomName);
      }
      isrom=1;
      RomWritable[romslot]=0;
      Shortener=1;
      if (strncmp(RomName,"EMPTY",5)==0)
      {
        RomWritable[romslot]=0; isrom=0;
      }
      if (strncmp(RomName,"RAM",3)==0)
      {
        RomWritable[romslot]=1; isrom=0;
      }
      if (strncmp(RomName+(strlen(RomName)-5),":RAM",4)==0)
      {
	// Writable ROM (as requested by Mark De Weger)
	RomWritable[romslot]=1; // Make it writable
	isrom=1; // Make it a ROM still
	Shortener=5; // Shorten filename
      }
      if (isrom == 1)	// Only try to read a file for actual ROMs
      {
	for (sc = 0; fullname[sc]; sc++)
	{
	  if (fullname[sc] == '\\')
	    fullname[sc] = '/';
	}
	fullname[strlen(fullname)-Shortener]=0;
	InFile=fopen(fullname,"rb");
	if (InFile!=NULL)
	{
	  fread(Roms[romslot], 1, STD_ROM_SIZE, InFile);
	  fclose(InFile);
	}
	else
	{
	  char errstr[200];
	  sprintf(errstr, "Cannot open specified ROM:\n %s",fullname);
	  MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
	}
      }
    }
    fclose(RomCfg);
  }
  else
  {
    char errstr[200];
    sprintf(errstr, "Cannot open ROM Configuration file %s", TmpPath);
    MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
    exit(1);
  }
} // end of : void BeebReadRoms(void)

/*------------------------------------------------------------------------*/
void BeebMemInit(unsigned char LoadRoms)
{
  /* Remove the non-win32 stuff here, soz, im not gonna do multi-platform
   * master128 upgrades u want for linux? u do yourself! ;P - Richard Gellman
   */

#ifdef USE_FDC2_LOG
  openLog(fdclog2, FDC2_LOG_FN);
#endif
  
  char TmpPath[256];
  unsigned char RomBlankingSlot;
  long CMOSLength;
  FILE *CMDF3;
  unsigned char CMA3;
  if (LoadRoms)
  {
    for (CMA3=0; CMA3<16; CMA3++)
    {
      RomWritable[CMA3]=1;
    }
    for (RomBlankingSlot=0xf; RomBlankingSlot<0x10; RomBlankingSlot--)
    {
      memset(Roms[RomBlankingSlot],0,0x4000);
    }
    // This shouldn't be required for sideways RAM.
    BeebReadRoms(); // Only load roms on start
  }

  /* Put first ROM in */
  PagedRomReg=0xF;
  memcpy(WholeRam + STD_PAGE_ROM_LDADDR, Roms[PagedRomReg], STD_ROM_SIZE);
  RomModified=0;

  // Initialise BBC Master 128 stuff
  if (MachineType == 1)
  {
    ACCCON=0;
    UseShadow=0; // Select all main memory
  }

  // This CMOS stuff can be done anyway
  // Ah, bug with cmos.ram you say?	
  strcpy(TmpPath, RomPath);
  strcat(TmpPath, "/beebstate/cmos.ram");
  CMDF3 = fopen(TmpPath, "rb");
  if (CMDF3 != NULL)
  {
    fseek(CMDF3,0,SEEK_END);
    CMOSLength=ftell(CMDF3);
    fseek(CMDF3,0,SEEK_SET);
    if (CMOSLength==50)
    {
      for(CMA3=0xe;CMA3<64;CMA3++)
      {
        CMOSRAM[CMA3]=fgetc(CMDF3);
      }
    }
    fclose(CMDF3);
  }
  else
  {
    for(CMA3=0xe;CMA3<64;CMA3++)
    {
      CMOSRAM[CMA3]=CMOSDefault[CMA3-0xe];
    }
  }
} /* BeebMemInit */

/*-------------------------------------------------------------------------*/
void SaveMemState(unsigned char *RomState,
		  unsigned char *MemState,
		  unsigned char *SWRamState)
{
  memcpy(MemState, WholeRam, 32768);

  /* Save SW RAM state if it is selected and it has been modified */
  if (SWRamModified && RomWritable[PagedRomReg])
  {
    RomState[0] = 1;
    memcpy(SWRamState, WholeRam+0x8000, 16384);
  }
}


void SaveMemUEF(FILE *SUEF) {
	unsigned char RAMCount;
	fput16(0x0461,SUEF); // Memory Control State
	fput32(2,SUEF);
	fputc(PagedRomReg,SUEF);
	fputc(ACCCON,SUEF);
	fput16(0x0462,SUEF); // Main Memory
	fput32(32768,SUEF);
	fwrite(WholeRam,1,32768,SUEF);
	if (MachineType==1) {
		fput16(0x0463,SUEF); // Shadow RAM
		fput32(32770,SUEF);
		fput16(0,SUEF);
		fwrite(ShadowRAM,1,32768,SUEF);
		fput16(0x0464,SUEF); // Private RAM
		fput32(4096,SUEF);
		fwrite(PrivateRAM,1,4096,SUEF);
		fput16(0x0465,SUEF); // Filing System RAM
		fput32(8192,SUEF);
		fwrite(FSRam,1,8192,SUEF);
	}
	for (RAMCount=0;RAMCount<16;RAMCount++) {
		if (RomWritable[RAMCount]) {
			fput16(0x0466,SUEF); // ROM bank
			fput32(16385,SUEF);
			fputc(RAMCount,SUEF);
			fwrite(Roms[RAMCount],1,16384,SUEF);
		}
	}
}

void LoadRomRegsUEF(FILE *SUEF) {
	PagedRomReg=fgetc(SUEF);
	ACCCON=fgetc(SUEF);
}

void LoadMainMemUEF(FILE *SUEF) {
	fread(WholeRam,1,32768,SUEF);
}

void LoadShadMemUEF(FILE *SUEF) {
	int SAddr;
	SAddr=fget16(SUEF);
	fread(ShadowRAM+SAddr,1,32768,SUEF);
}

void LoadPrivMemUEF(FILE *SUEF) {
	fread(PrivateRAM,1,4096,SUEF);
}

void LoadFileMemUEF(FILE *SUEF) {
	fread(FSRam,1,8192,SUEF);
}

void LoadSWRMMemUEF(FILE *SUEF) {
	int Rom;
	Rom=fgetc(SUEF);
	RomWritable[Rom]=1;
	fread(Roms[Rom],1,16384,SUEF);
}

/*-------------------------------------------------------------------------*/
void RestoreMemState(unsigned char *RomState,
					 unsigned char *MemState,
					 unsigned char *SWRamState)
{
	memcpy(WholeRam, MemState, 32768);

	/* Restore SW RAM state if it is in use */
	if (RomState[0] == 1)
	{
		RomModified = 1;
		SWRamModified = 1;
		PagedRomReg = 0;      /* Use rom slot 0 */
		RomWritable[0] = 1;
		memcpy(WholeRam+0x8000, SWRamState, 16384);
	}
}

/*-------------------------------------------------------------------------*/
/* dump the contents of mainram into 2 16 K files */
void beebmem_dumpstate(void)
{
  FILE *bottom,*top;

  bottom=fopen("memdump_bottom", "wb");
  top=fopen("memdump_top", "wb");
  if ((bottom==NULL) || (top==NULL))
  {
    cerr << "Couldn't open memory dump files\n";
    return;
  }

  fwrite(WholeRam,1,16384,bottom);
  fwrite(WholeRam+16384,1,16384,top);
  fclose(bottom);
  fclose(top);
} /* beebmem_dumpstate */

