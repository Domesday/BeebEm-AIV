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
/* 6502 core - 6502 emulator core - David Alan Gilbert 16/10/94 */

/* Mike Wyatt 7/6/97 - Added undocumented instructions */

#include <iostream.h>
#include <stdio.h>
#include <stdlib.h>

#include "6502core.h"
#include "beebmem.h"
#include "beebsound.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"
#include "main.h"
#include "disc1770.h"
#include "serial.h"
#include "tube.h"
#include "uefstate.h"
#include "aivscsi.h"
#include "lvrom.h"
#include "debugLogs.h"

//#ifdef WIN32
//#include <windows.h>
//#define INLINE inline
//#else
#define INLINE static
//#endif

int CPUDebug=0;
int BeginDump=0;
#ifdef USE_CORE_LOG
FILE *core6502log;
#endif
#ifdef USE_INSTR_LOG
FILE *core6502instr;
#endif

static unsigned int InstrCount;
int IgnoreIllegalInstructions = 1;

extern int DumpAfterEach;

__int64 TotalCycles=0;
__int64 VFrameTrigger=80000;

int ProgramCounter;
static int Accumulator,XReg,YReg;
static unsigned char StackReg,PSR;
static unsigned char IRQCycles;
int DisplayCycles=0;
int SwitchOnCycles=2000000; // Reset delay
int SecCycles=0; // Cycles elapsed since start of second
bool HoldingCPU=FALSE; // TRUE if CPU should be temporarily suspended

unsigned char intStatus=0; /* bit set (nums in IRQ_Nums) if interrupt being caused */
unsigned char NMIStatus=0; /* bit set (nums in NMI_Nums) if NMI being caused */
unsigned int NMILock=0; /* Well I think NMI's are maskable - to stop repeated NMI's - the lock is released when an RTI is done */
typedef int int16;
/* Stats */
static int Stats[256];
INLINE void SBCInstrHandler(int16 operand);
enum PSRFlags {
  FlagC=1,
  FlagZ=2,
  FlagI=4,
  FlagD=8,
  FlagB=16,
  FlagV=64,
  FlagN=128
};

/* Note how GETCFLAG is special since being bit 0 we don't need to test it to get a clean 0/1 */
#define GETCFLAG ((PSR & FlagC))
#define GETZFLAG ((PSR & FlagZ)>0)
#define GETIFLAG ((PSR & FlagI)>0)
#define GETDFLAG ((PSR & FlagD)>0)
#define GETBFLAG ((PSR & FlagB)>0)
#define GETVFLAG ((PSR & FlagV)>0)
#define GETNFLAG ((PSR & FlagN)>0)

/* Types for internal function arrays */
typedef void (*InstrHandlerFuncType)(int16 Operand);
typedef int16 (*AddrModeHandlerFuncType)(int WantsAddr);

static int CyclesTable[]={
  7,6,1,1,5,3,5,5,3,2,2,1,6,4,6,1, /* 0 */
  2,5,5,1,5,4,6,1,2,4,2,1,6,4,7,1, /* 1 */
  6,6,1,1,3,3,5,1,4,2,2,1,4,4,6,1, /* 2 */
  2,5,5,1,4,4,6,1,2,4,2,1,4,4,7,1, /* 3 */
  6,6,1,1,1,3,5,1,3,2,2,2,3,4,6,1, /* 4 */
  2,5,5,1,1,4,6,1,2,4,3,1,1,4,7,1, /* 5 */
  6,6,1,1,3,3,5,1,4,2,2,1,5,4,6,1, /* 6 */
  2,5,5,1,4,4,6,1,2,4,4,1,6,4,7,1, /* 7 */
  2,6,1,1,3,3,3,3,2,2,2,1,4,4,4,1, /* 8 */
  2,6,5,1,4,4,4,1,2,5,2,1,4,5,5,1, /* 9 */
  2,6,2,1,3,3,3,1,2,2,2,1,4,4,4,1, /* a */
  2,5,5,1,4,4,4,1,2,4,2,1,4,4,4,1, /* b */
  2,6,1,1,3,3,5,1,2,2,2,1,4,4,6,1, /* c */
  2,5,5,1,1,4,6,1,2,4,3,1,4,4,7,1, /* d */
  2,6,1,1,3,3,5,1,2,2,2,1,4,4,6,1, /* e */
  2,5,5,1,1,4,6,1,2,4,4,1,4,4,7,1  /* f */
}; /* CyclesTable */


/* The number of cycles to be used by the current instruction - exported to
   allow fernangling by memory subsystem */
unsigned int Cycles;
int PrePC;
int tmpaddr;

static unsigned char Branched,Carried;
// Branched - 1 if the instruction branched
// Carried - 1 if the instruction carried over to high byte in index calculation
static unsigned char FirstCycle;
int OpCodes=2; // 0 = documented only, 1 = commonoly used undocumenteds, 2 = full set
int BHardware=0; // 0 = all hardware, 1 = basic hardware only
// 1 if first cycle happened

/* A macro to speed up writes - uses a local variable called 'tmpaddr' */
#define FASTWRITE(addr,val) tmpaddr=addr; if (tmpaddr<0x8000) BEEBWRITEMEM_DIRECT(tmpaddr,val) else BeebWriteMem(tmpaddr,val);

/* Get a two byte address from the program counter, and then post inc the program counter */
#define GETTWOBYTEFROMPC(var) \
  var=ReadPaged(ProgramCounter); \
  var|=(ReadPaged(ProgramCounter+1)<<8); \
  ProgramCounter+=2;

#define WritePaged(addr,val) if (MachineType==0) { \
FASTWRITE(addr,val) \
} \
 else WritePagedP(addr,val)

void WritePagedP(int Address, unsigned char value) {
		// Master 128, gotta be complicated
		switch ((Address&0xf000)>>12) {
		case 0:
		case 1:
		case 2:
			WholeRam[Address]=value; // Low memory - not paged.
			break;
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			if ((!Sh_CPUX) && (!Sh_CPUE)) WholeRam[Address]=value;
			if (Sh_CPUX) ShadowRAM[Address]=value;
			if ((Sh_CPUE>0) && (Sh_CPUX==0)) { 
				if ((PrePC>=0xc000) && (PrePC<0xe000)) ShadowRAM[Address]=value; else WholeRam[Address]=value;
			} 
			break;
		case 8:
			if (PRAM) { PrivateRAM[Address-0x8000]=value; }
			else {
				if (RomWritable[ROMSEL]) Roms[PagedRomReg&15][Address-0x8000]=value;
			}
			break;
		case 9:
		case 0xa:
		case 0xb:
			if (RomWritable[PagedRomReg&15]) Roms[ROMSEL][Address-0x8000]=value;
			break;
		case 0xc:
		case 0xd:
			if (FRAM) FSRam[Address-0xc000]=value;
			break;
		case 0xf:
			if ((Address>=0xfc00) && (Address<0xff00)) BeebWriteMem(Address,value);
			break;
		}
}

int ReadPaged(int Address)
{
  // This code only works for the Master128 - the code for the BBC B has been
  // commented out.
  // if (MachineType==0)
  // {
  //   // The BBC B is easier to read than the Master128. Have used a Macro fn
  //   return(BEEBREADMEM_FAST(Address));
  // }

  // The Master 128 might read a different bank of memory, so needs handling
  // specially. (i.e. Master 128, gotta be complicated)
  switch ((Address&0xf000)>>12)
  {
    case 0x0:
    case 0x1:
    case 0x2:
      return(WholeRam[Address]); // Low memory - not paged.
      break;
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
      if ((!Sh_CPUX) && (!Sh_CPUE))
        return(WholeRam[Address]);
      if (Sh_CPUX)
        return(ShadowRAM[Address]);
      if ((Sh_CPUE) && (!Sh_CPUX))
      {
	if ((PrePC>=0xc000) && (PrePC<0xe000))
	  return(ShadowRAM[Address]);
	else
	  return(WholeRam[Address]);
      }
      break;
    case 0x8:
      if (PRAM>0)
      { 
	return(PrivateRAM[Address-0x8000]); 
      }
      else
      { 
	return(Roms[ROMSEL][Address-0x8000]);
      }
      break;
    case 0x9:
    case 0xa:
    case 0xb:
      return(Roms[ROMSEL][Address-0x8000]);
      break;
    case 0xc:
    case 0xd:
      if (FRAM)
        return(FSRam[Address-0xc000]);
      else
	return(WholeRam[Address]);
      break;
    case 0xe:
      return(WholeRam[Address]);
      break;
    case 0xf:
      if (Address<0xfc00)
      {
        return(WholeRam[Address]);
	break;
      }
      if (Address<0xff00)
      {
        return(BeebReadMem(Address));
	break;
      }
      return(WholeRam[Address]);
      break;
    default:
      return(0);
  }
  return(0); // Keep MSVC happy.
} // end of - int ReadPaged(int Address)

/*-----------------------------------------------------------------------*/
INLINE  int SignExtendByte(signed char in) {
  /*if (in & 0x80) return(in | 0xffffff00); else return(in); */
  /* I think this should sign extend by virtue of the casts - gcc does anyway - the code
  above will definitly do the trick */
  return((int)in);
} /* SignExtendByte */

/*-------------------------------------------------------------------------*/
/* Set the Z flag if 'in' is 0, and N if bit 7 is set - leave all other bits  */
/* untouched.                                                                 */
INLINE  void SetPSRZN(const unsigned char in) {
  PSR&=~(FlagZ | FlagN);
  PSR|=((in==0)<<1) | (in & 128);
}; /* SetPSRZN */

/*----------------------------------------------------------------------------*/
/* Note: n is 128 for true - not 1                                            */
INLINE  void SetPSR(int mask,int c,int z,int i,int d,int b, int v, int n) {
  PSR&=~mask;
  PSR|=c | (z<<1) | (i<<2) | (d<<3) | (b<<4) | (v<<6) | n;
} /* SetPSR */

/*----------------------------------------------------------------------------*/
/* NOTE!!!!! n is 128 or 0 - not 1 or 0                                       */
INLINE  void SetPSRCZN(int c,int z, int n) {
  PSR&=~(FlagC | FlagZ | FlagN);
  PSR|=c | (z<<1) | n;
} /* SetPSRCZN */

/*----------------------------------------------------------------------------*/
void DumpRegs(void) {
   char FlagNames[]="CZIDB-VNczidb-vn";
  int FlagNum;

  fprintf(stderr,"  PC=0x%x A=0x%x X=0x%x Y=0x%x S=0x%x PSR=0x%x=",
    ProgramCounter,Accumulator,XReg,YReg,StackReg,PSR);
  for(FlagNum=0;FlagNum<8;FlagNum++)
    fputc(FlagNames[FlagNum+8*((PSR & (1<<FlagNum))==0)],stderr);
  fputc('\n',stderr);
} /* DumpRegs */

/*----------------------------------------------------------------------------*/
INLINE  void Push(unsigned char ToPush) {
  BEEBWRITEMEM_DIRECT(0x100+StackReg,ToPush);
  StackReg--;
} /* Push */

/*----------------------------------------------------------------------------*/
INLINE  unsigned char Pop(void) {
  StackReg++;
  return(WholeRam[0x100+StackReg]);
} /* Pop */

/*----------------------------------------------------------------------------*/
INLINE  void PushWord(int16 topush) {
  Push((topush>>8) & 255);
  Push(topush & 255);
} /* PushWord */

/*----------------------------------------------------------------------------*/
INLINE  int16 PopWord() {
  int16 RetValue;

  RetValue=Pop();
  RetValue|=(Pop()<<8);
  return(RetValue);
} /* PopWord */

/*-------------------------------------------------------------------------*/
/* Relative addressing mode handler                                        */
INLINE  int16 RelAddrModeHandler_Data(void) {
  int EffectiveAddress;

  /* For branches - is this correct - i.e. is the program counter incremented
     at the correct time? */
  EffectiveAddress=SignExtendByte((signed char)ReadPaged(ProgramCounter++));
  EffectiveAddress+=ProgramCounter;
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* RelAddrModeHandler */

/*----------------------------------------------------------------------------*/
INLINE  void ADCInstrHandler(int16 operand) {
  /* NOTE! Not sure about C and V flags */
  int TmpResultV,TmpResultC;
  if (!GETDFLAG) {
    TmpResultC=Accumulator+operand+GETCFLAG;
    TmpResultV=(signed char)Accumulator+(signed char)operand+GETCFLAG;
    Accumulator=TmpResultC & 255;
    SetPSR(FlagC | FlagZ | FlagV | FlagN, (TmpResultC & 256)>0,Accumulator==0,0,0,0,((Accumulator & 128)>0) ^ (TmpResultV<0),(Accumulator & 128));
  } else {
    int ZFlag=0,NFlag=0,CFlag=0,VFlag=0;
    int TmpResult,TmpCarry=0;
    int ln,hn;

    /* Z flag determined from 2's compl result, not BCD result! */
    TmpResult=Accumulator+operand+GETCFLAG;
    ZFlag=((TmpResult & 0xff)==0);

    ln=(Accumulator & 0xf)+(operand & 0xf)+GETCFLAG;
    if (ln>9) {
      ln += 6;
      ln &= 0xf;
      TmpCarry=0x10;
    }
    hn=(Accumulator & 0xf0)+(operand & 0xf0)+TmpCarry;
    /* N and V flags are determined before high nibble is adjusted.
       NOTE: V is not always correct */
    NFlag=hn & 128;
    VFlag=(hn ^ Accumulator) & 128 && !((Accumulator ^ operand) & 128);
    if (hn>0x90) {
      hn += 0x60;
      hn &= 0xf0;
      CFlag=1;
    }
    Accumulator=hn|ln;
	ZFlag=(Accumulator==0);
	NFlag=(Accumulator&128);
    SetPSR(FlagC | FlagZ | FlagV | FlagN,CFlag,ZFlag,0,0,0,VFlag,NFlag);
  }
} /* ADCInstrHandler */

/*----------------------------------------------------------------------------*/
INLINE  void ANDInstrHandler(int16 operand) {
  Accumulator=Accumulator & operand;
  PSR&=~(FlagZ | FlagN);
  PSR|=((Accumulator==0)<<1) | (Accumulator & 128);
} /* ANDInstrHandler */

INLINE  void ASLInstrHandler(int16 address) {
  unsigned char oldVal,newVal;
  oldVal=ReadPaged(address);
  newVal=(((unsigned int)oldVal)<<1)&255;
  WritePaged(address,newVal);
  SetPSRCZN((oldVal & 128)>0, newVal==0,newVal & 128);
} /* ASLInstrHandler */

INLINE  void TRBInstrHandler(int16 address) {
	unsigned char oldVal,newVal;
	oldVal=ReadPaged(address);
	newVal=(Accumulator ^ 255) & oldVal;
    WritePaged(address,newVal);
    PSR&=253;
	PSR|=((Accumulator & oldVal)==0) ? 2 : 0;
} // TRBInstrHandler

INLINE  void TSBInstrHandler(int16 address) {
	unsigned char oldVal,newVal;
	oldVal=ReadPaged(address);
	newVal=Accumulator | oldVal;
    WritePaged(address,newVal);
    PSR&=253;
	PSR|=((Accumulator & oldVal)==0) ? 2 : 0;
} // TSBInstrHandler

INLINE  void ASLInstrHandler_Acc(void) {
  unsigned char oldVal,newVal;
  /* Accumulator */
  oldVal=Accumulator;
  Accumulator=newVal=(((unsigned int)Accumulator)<<1)&255;
  SetPSRCZN((oldVal & 128)>0, newVal==0,newVal & 128);
} /* ASLInstrHandler_Acc */

INLINE  void BCCInstrHandler(void) {
  if (!GETCFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
} /* BCCInstrHandler */

INLINE  void BCSInstrHandler(void) {
  if (GETCFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
} /* BCSInstrHandler */

INLINE  void BEQInstrHandler(void) {
  if (GETZFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
} /* BEQInstrHandler */

INLINE  void BITInstrHandler(int16 operand) {
  PSR&=~(FlagZ | FlagN | FlagV);
  /* z if result 0, and NV to top bits of operand */
  PSR|=(((Accumulator & operand)==0)<<1) | (operand & 192);
} /* BITInstrHandler */

INLINE  void BMIInstrHandler(void) {
  if (GETNFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
} /* BMIInstrHandler */

INLINE  void BNEInstrHandler(void) {
  if (!GETZFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
} /* BNEInstrHandler */

INLINE  void BPLInstrHandler(void) {
  if (!GETNFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
}; /* BPLInstrHandler */

INLINE void BRKInstrHandler(void) {
#ifdef USE_CORE_LOG
  char errstr[250];
  sprintf(errstr, "Host BRK t %04x - closing Log file", ProgramCounter);
  MessageBox(GETHWND, errstr, "BBC Emulator", MB_OKCANCEL | MB_ICONERROR);
  fprintf(core6502instr, "Reached BRKInstrHandler - closing log\n");
  closeLog(core6502instr);
#endif
  PushWord(ProgramCounter+1);
  SetPSR(FlagB,0,0,0,0,1,0,0); /* Set B before pushing */
  Push(PSR);
  SetPSR(FlagI,0,0,1,0,0,0,0); /* Set I after pushing - see Birnbaum */
  ProgramCounter=BeebReadMem(0xfffe) | (BeebReadMem(0xffff)<<8);
} /* BRKInstrHandler */

INLINE  void BVCInstrHandler(void) {
  if (!GETVFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
} /* BVCInstrHandler */

INLINE  void BVSInstrHandler(void) {
  if (GETVFLAG) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
  } else ProgramCounter++;
} /* BVSInstrHandler */

INLINE  void BRAInstrHandler(void) {
    ProgramCounter=RelAddrModeHandler_Data();
    Branched=1;
} /* BRAnstrHandler */

INLINE  void CMPInstrHandler(int16 operand) {
  /* NOTE! Should we consult D flag ? */
/*  unsigned char TmpAcc;
  PSR|=1;
  TmpAcc=Accumulator;
  SBCInstrHandler(operand);
  Accumulator=TmpAcc; */
  unsigned char result=Accumulator-operand;
  unsigned char CFlag;
  CFlag=0; if (Accumulator>=operand) CFlag=FlagC;
  SetPSRCZN(CFlag,Accumulator==operand,result & 128); 
} /* CMPInstrHandler */

INLINE  void CPXInstrHandler(int16 operand) {
  unsigned char result=(XReg-operand);
  SetPSRCZN(XReg>=operand,XReg==operand,result & 128);
} /* CPXInstrHandler */

INLINE  void CPYInstrHandler(int16 operand) {
  unsigned char result=(YReg-operand);
  SetPSRCZN(YReg>=operand,YReg==operand,result & 128);
} /* CPYInstrHandler */

INLINE  void DECInstrHandler(int16 address) {
  int val;
  val=ReadPaged(address);
  val=(val-1);
  if (val<0) val=256+val;
  WritePaged(address,val);
  SetPSRZN(val);
} /* DECInstrHandler */

INLINE void DEXInstrHandler(void)
{
  int val;
  val=XReg-1;
  if (val<0)
    XReg=256+val;
  else
    XReg=val;
  SetPSRZN(XReg);
} /* DEXInstrHandler */

INLINE void DEAInstrHandler(void)
{
  int val;
  val=Accumulator-1;
  if (val<0)
    Accumulator=256+val;
  else
    Accumulator=val;
  SetPSRZN(Accumulator);
} /* DEAInstrHandler */

INLINE  void EORInstrHandler(int16 operand) {
  Accumulator^=operand;
  SetPSRZN(Accumulator);
} /* EORInstrHandler */

INLINE  void INCInstrHandler(int16 address) {
  unsigned char val;

  val=ReadPaged(address);

  val=(val+1) & 255;

  WritePaged(address,val);
  SetPSRZN(val);
} /* INCInstrHandler */

INLINE  void INXInstrHandler(void) {
  XReg+=1;
  XReg&=255;
  SetPSRZN(XReg);
} /* INXInstrHandler */

INLINE  void INAInstrHandler(void) {
  Accumulator+=1;
  Accumulator&=255;
  SetPSRZN(Accumulator);
} /* INAInstrHandler */

INLINE  void JSRInstrHandler(int16 address) {
  PushWord(ProgramCounter-1);
  ProgramCounter=address;
} /* JSRInstrHandler */

INLINE  void LDAInstrHandler(int16 operand) {
  Accumulator=operand;
  SetPSRZN(Accumulator);
} /* LDAInstrHandler */

INLINE  void LDXInstrHandler(int16 operand) {
  XReg=operand;
  SetPSRZN(XReg);
} /* LDXInstrHandler */

INLINE  void LDYInstrHandler(int16 operand) {
  YReg=operand;
  SetPSRZN(YReg);
} /* LDYInstrHandler */

INLINE  void LSRInstrHandler(int16 address) {
  unsigned char oldVal,newVal;
  oldVal=ReadPaged(address);
  newVal=(((unsigned int)oldVal)>>1)&255;
  WritePaged(address,newVal);
  SetPSRCZN((oldVal & 1)>0, newVal==0,0);
} /* LSRInstrHandler */

INLINE  void LSRInstrHandler_Acc(void) {
  unsigned char oldVal,newVal;
  /* Accumulator */
  oldVal=Accumulator;
  Accumulator=newVal=(((unsigned int)Accumulator)>>1) & 255;
  SetPSRCZN((oldVal & 1)>0, newVal==0,0);
} /* LSRInstrHandler_Acc */

INLINE  void ORAInstrHandler(int16 operand) {
  Accumulator=Accumulator | operand;
  SetPSRZN(Accumulator);
} /* ORAInstrHandler */

INLINE  void ROLInstrHandler(int16 address) {
  unsigned char oldVal,newVal;

  oldVal=ReadPaged(address);
  newVal=((unsigned int)oldVal<<1) & 254;
  newVal+=GETCFLAG;
  WritePaged(address,newVal);
  SetPSRCZN((oldVal & 128)>0,newVal==0,newVal & 128);
} /* ROLInstrHandler */

INLINE  void ROLInstrHandler_Acc(void) {
  unsigned char oldVal,newVal;

  oldVal=Accumulator;
  newVal=((unsigned int)oldVal<<1) & 254;
  newVal+=GETCFLAG;
  Accumulator=newVal;
  SetPSRCZN((oldVal & 128)>0,newVal==0,newVal & 128);
} /* ROLInstrHandler_Acc */

INLINE  void RORInstrHandler(int16 address) {
  unsigned char oldVal,newVal;

  oldVal=ReadPaged(address);
  newVal=((unsigned int)oldVal>>1) & 127;
  newVal+=GETCFLAG*128;
  WritePaged(address,newVal);
  SetPSRCZN(oldVal & 1,newVal==0,newVal & 128);
} /* RORInstrHandler */

INLINE  void RORInstrHandler_Acc(void) {
  unsigned char oldVal,newVal;

  oldVal=Accumulator;
  newVal=((unsigned int)oldVal>>1) & 127;
  newVal+=GETCFLAG*128;
  Accumulator=newVal;
  SetPSRCZN(oldVal & 1,newVal==0,newVal & 128);
} /* RORInstrHandler_Acc */

INLINE  void SBCInstrHandler(int16 operand) {
  /* NOTE! Not sure about C and V flags */
  int TmpResultV,TmpResultC;
  unsigned char nhn,nln;
  if (!GETDFLAG) {
    TmpResultV=(signed char)Accumulator-(signed char)operand-(1-GETCFLAG);
    TmpResultC=Accumulator-operand-(1-GETCFLAG);
    Accumulator=TmpResultC & 255;
    SetPSR(FlagC | FlagZ | FlagV | FlagN, TmpResultC>=0,Accumulator==0,0,0,0,
      ((Accumulator & 128)>0) ^ ((TmpResultV & 256)!=0),(Accumulator & 128));
  } else {
    int ZFlag=0,NFlag=0,CFlag=1,VFlag=0;
    int TmpResult,TmpCarry=0;
    int ln,hn,oln,ohn;
	nhn=(Accumulator>>4)&15; nln=Accumulator & 15;

    /* Z flag determined from 2's compl result, not BCD result! */
    TmpResult=Accumulator-operand-(1-GETCFLAG);
    ZFlag=((TmpResult & 0xff)==0);

	ohn=operand & 0xf0; oln = operand & 0xf;
	if ((oln>9) && ((Accumulator&15)<10)) { oln-=10; ohn+=0x10; } 
	// promote the lower nibble to the next ten, and increase the higher nibble
    ln=(Accumulator & 0xf)-oln-(1-GETCFLAG);
    if (ln<0) {
	  if ((Accumulator & 15)<10) ln-=6;
      ln&=0xf;
      TmpCarry=0x10;
    }
    hn=(Accumulator & 0xf0)-ohn-TmpCarry;
    /* N and V flags are determined before high nibble is adjusted.
       NOTE: V is not always correct */
    NFlag=hn & 128;
	TmpResultV=(signed char)Accumulator-(signed char)operand-(1-GETCFLAG);
	if ((TmpResultV<-128)||(TmpResultV>127)) VFlag=1; else VFlag=0;
    if (hn<0) {
      hn-=0x60;
      hn&=0xf0;
      CFlag=0;
    }
    Accumulator=hn|ln;
	if (Accumulator==0) ZFlag=1;
	NFlag=(hn &128);
	CFlag=(TmpResult&256)==0;
    SetPSR(FlagC | FlagZ | FlagV | FlagN,CFlag,ZFlag,0,0,0,VFlag,NFlag);
  }
} /* SBCInstrHandler */

INLINE  void STXInstrHandler(int16 address) {
  WritePaged(address,XReg);
} /* STXInstrHandler */

INLINE  void STYInstrHandler(int16 address) {
  WritePaged(address,YReg);
} /* STYInstrHandler */

INLINE  void BadInstrHandler(int opcode) {
	if (!IgnoreIllegalInstructions)
	{
#ifdef WIN32
		char errstr[250];
		sprintf(errstr,"Unsupported 6502 instruction 0x%02X at 0x%04X\n"
			"  OK - instruction will be skipped\n"
			"  Cancel - dump memory and exit",opcode,ProgramCounter-1);
		if (MessageBox(GETHWND,errstr,"BBC Emulator",MB_OKCANCEL|MB_ICONERROR) == IDCANCEL)
		{
			beebmem_dumpstate();
			exit(0);
		}
#else
		fprintf(stderr,"Bad instruction handler called:\n");
		DumpRegs();
		fprintf(stderr,"Dumping main memory\n");
		beebmem_dumpstate();
		// abort();
#endif
	}

	/* Do not know what the instruction does but can guess if it is 1,2 or 3 bytes */
	switch (opcode & 0xf)
	{
	/* One byte instructions */
	case 0xa:
		break;

	/* Two byte instructions */
	case 0x0:
	case 0x2:  /* Inst 0xf2 causes the 6502 to hang! Try it on your BBC Micro */
	case 0x3:
	case 0x4:
	case 0x7:
	case 0x9:
	case 0xb:
		ProgramCounter++;
		break;

	/* Three byte instructions */
	case 0xc:
	case 0xe:
	case 0xf:
		ProgramCounter+=2;
		break;
	}
} /* BadInstrHandler */

/*-------------------------------------------------------------------------*/
/* Absolute  addressing mode handler                                       */
INLINE  int16 AbsAddrModeHandler_Data(void) {
  int FullAddress;

  /* Get the address from after the instruction */
  
  GETTWOBYTEFROMPC(FullAddress)

  /* And then read it */
  return(ReadPaged(FullAddress));
} /* AbsAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute  addressing mode handler                                       */
INLINE  int16 AbsAddrModeHandler_Address(void) {
  int FullAddress;

  /* Get the address from after the instruction */
  GETTWOBYTEFROMPC(FullAddress)

  /* And then read it */
  return(FullAddress);
} /* AbsAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page addressing mode handler                                       */
INLINE  int16 ZeroPgAddrModeHandler_Address(void) {
  return(ReadPaged(ProgramCounter++));
} /* ZeroPgAddrModeHandler_Address */

/*-------------------------------------------------------------------------*/
/* Indexed with X preinc addressing mode handler                           */
INLINE  int16 IndXAddrModeHandler_Data(void) {
  unsigned char ZeroPageAddress;
  int EffectiveAddress;

  ZeroPageAddress=(ReadPaged(ProgramCounter++)+XReg) & 255;

  EffectiveAddress=WholeRam[ZeroPageAddress] | (WholeRam[ZeroPageAddress+1]<<8);
  EffectiveAddress&=0xffff;
  return(ReadPaged(EffectiveAddress));
} /* IndXAddrModeHandler_Data */

/*-------------------------------------------------------------------------*/
/* Indexed with X preinc addressing mode handler                           */
INLINE  int16 IndXAddrModeHandler_Address(void) {
  unsigned char ZeroPageAddress;
  int EffectiveAddress;

  ZeroPageAddress=(ReadPaged(ProgramCounter++)+XReg) & 255;

  EffectiveAddress=WholeRam[ZeroPageAddress] | (WholeRam[ZeroPageAddress+1]<<8);
  EffectiveAddress&=0xffff;
  return(EffectiveAddress);
} /* IndXAddrModeHandler_Address */

/*-------------------------------------------------------------------------*/
/* Indexed with Y postinc addressing mode handler                          */
INLINE  int16 IndYAddrModeHandler_Data(void) {
  int EffectiveAddress;
  unsigned char ZPAddr=ReadPaged(ProgramCounter++);
  EffectiveAddress=WholeRam[ZPAddr]+YReg;
  if (EffectiveAddress>0xff) Carried=1;
  EffectiveAddress+=(WholeRam[ZPAddr+1]<<8);
  EffectiveAddress&=0xffff;

  return(ReadPaged(EffectiveAddress));
} /* IndYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Indexed with Y postinc addressing mode handler                          */
INLINE  int16 IndYAddrModeHandler_Address(void) {
  int EffectiveAddress;
  unsigned char ZPAddr=ReadPaged(ProgramCounter++);
  EffectiveAddress=WholeRam[ZPAddr]+YReg;
  if (EffectiveAddress>0xff) Carried=1;
  EffectiveAddress+=(WholeRam[ZPAddr+1]<<8);
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* IndYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page wih X offset addressing mode handler                          */
INLINE  int16 ZeroPgXAddrModeHandler_Data(void) {
  int EffectiveAddress;
  EffectiveAddress=(ReadPaged(ProgramCounter++)+XReg) & 255;
  return(WholeRam[EffectiveAddress]);
} /* ZeroPgXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page wih X offset addressing mode handler                          */
INLINE  int16 ZeroPgXAddrModeHandler_Address(void) {
  int EffectiveAddress;
  EffectiveAddress=(ReadPaged(ProgramCounter++)+XReg) & 255;
  return(EffectiveAddress);
} /* ZeroPgXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with X offset addressing mode handler                          */
INLINE  int16 AbsXAddrModeHandler_Data(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMPC(EffectiveAddress);
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+XReg) & 0xff00)) Carried=1;
  EffectiveAddress+=XReg;
  EffectiveAddress&=0xffff;

  return(ReadPaged(EffectiveAddress));
} /* AbsXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with X offset addressing mode handler                          */
INLINE  int16 AbsXAddrModeHandler_Address(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMPC(EffectiveAddress)
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+XReg) & 0xff00)) Carried=1;
  EffectiveAddress+=XReg;
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* AbsXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with Y offset addressing mode handler                          */
INLINE  int16 AbsYAddrModeHandler_Data(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMPC(EffectiveAddress);
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+YReg) & 0xff00)) Carried=1;
  EffectiveAddress+=YReg;
  EffectiveAddress&=0xffff;

  return(ReadPaged(EffectiveAddress));
} /* AbsYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with Y offset addressing mode handler                          */
INLINE  int16 AbsYAddrModeHandler_Address(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMPC(EffectiveAddress)
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+YReg) & 0xff00)) Carried=1;
  EffectiveAddress+=YReg;
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* AbsYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Indirect addressing mode handler                                        */
INLINE  int16 IndAddrModeHandler_Address(void) {
  /* For jump indirect only */
  int VectorLocation;
  int EffectiveAddress;

  GETTWOBYTEFROMPC(VectorLocation)

  /* Ok kiddies, deliberate bug time.
  According to my BBC Master Reference Manual Part 2
  the 6502 has a bug concerning this addressing mode and VectorLocation==xxFF
  so, we're going to emulate that bug -- Richard Gellman */
  if ((VectorLocation & 0xff)!=0xff || MachineType==1) {
   EffectiveAddress=ReadPaged(VectorLocation);
   EffectiveAddress|=ReadPaged(VectorLocation+1) << 8; }
  else {
   EffectiveAddress=ReadPaged(VectorLocation);
   EffectiveAddress|=ReadPaged(VectorLocation-255) << 8;
  }
  return(EffectiveAddress);
} /* IndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page Indirect addressing mode handler                                        */
INLINE  int16 ZPIndAddrModeHandler_Address(void) {
  int VectorLocation;
  int EffectiveAddress;

  VectorLocation=ReadPaged(ProgramCounter++);
  EffectiveAddress=ReadPaged(VectorLocation)+(ReadPaged(VectorLocation+1)<<8);

   // EffectiveAddress|=ReadPaged(VectorLocation+1) << 8; }
  return(EffectiveAddress);
} /* ZPIndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page Indirect addressing mode handler                                        */
INLINE  int16 ZPIndAddrModeHandler_Data(void) {
  int VectorLocation;
  int EffectiveAddress;

  VectorLocation=ReadPaged(ProgramCounter++);
  EffectiveAddress=ReadPaged(VectorLocation)+(ReadPaged(VectorLocation+1)<<8);

   // EffectiveAddress|=ReadPaged(VectorLocation+1) << 8; }
  return(ReadPaged(EffectiveAddress));
} /* ZPIndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Pre-indexed absolute Indirect addressing mode handler                                        */
INLINE  int16 IndAddrXModeHandler_Address(void) {
  /* For jump indirect only */
  int VectorLocation;
  int EffectiveAddress;

  GETTWOBYTEFROMPC(VectorLocation)

  EffectiveAddress=ReadPaged(VectorLocation+XReg);
  EffectiveAddress|=ReadPaged(VectorLocation+1+XReg) << 8; 
  EffectiveAddress&=0xffff;
   // EffectiveAddress|=ReadPaged(VectorLocation+1) << 8; }
  return(EffectiveAddress);
} /* ZPIndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page with Y offset addressing mode handler                         */
INLINE  int16 ZeroPgYAddrModeHandler_Data(void) {
  int EffectiveAddress;
  EffectiveAddress=(ReadPaged(ProgramCounter++)+YReg) & 255;
  return(WholeRam[EffectiveAddress]);
} /* ZeroPgYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page with Y offset addressing mode handler                         */
INLINE  int16 ZeroPgYAddrModeHandler_Address(void) {
  int EffectiveAddress;
  EffectiveAddress=(ReadPaged(ProgramCounter++)+YReg) & 255;
  return(EffectiveAddress);
} /* ZeroPgYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Initialise 6502core                                                     */
void Init6502core(void)
{

#ifdef USE_CORE_LOG
  openLog(core6502log, CORE_LOG_FN);
#endif
#ifdef USE_INSTR_LOG
  openLog(core6502instr, INSTR_LOG_FN);
#endif

  ProgramCounter=BeebReadMem(0xfffc) | (BeebReadMem(0xfffd)<<8);
  Accumulator=XReg=YReg=0; /* For consistancy of execution */
  StackReg=0xff; /* Initial value ? */
  PSR=FlagI; /* Interrupts off for starters */

  intStatus=0;
  NMIStatus=0;
  NMILock=0;
  // MachineType=MachineType;
  FirstCycle=40;
} /* Init6502core */

// #include "via.h" // Already included from "sysvia.h"-D.M.Sergeant(17/10/02)

/*-------------------------------------------------------------------------*/
void DoInterrupt(void) {
  PushWord(ProgramCounter);
  Push(PSR & ~FlagB);
  ProgramCounter=BeebReadMem(0xfffe) | (BeebReadMem(0xffff)<<8);
  SetPSR(FlagI,0,0,1,0,0,0,0);
  IRQCycles=7;
} /* DoInterrupt */

/*-------------------------------------------------------------------------*/
void DoNMI(void) {
  /*cerr << "Doing NMI\n"; */
  NMILock=1;
  PushWord(ProgramCounter);
  Push(PSR);
  ProgramCounter=BeebReadMem(0xfffa) | (BeebReadMem(0xfffb)<<8);
  SetPSR(FlagI,0,0,1,0,0,0,0); /* Normal interrupts should be disabled during NMI ? */
  IRQCycles=7;
} /* DoNMI */

/*-------------------------------------------------------------------------*/
/* Execute one 6502 instruction, move program counter on                   */
void Exec6502Instruction(void) {
  static int CurrentInstruction;
  static int tmpaddr;
  static int OldNMIStatus;
  int BadCount=0;
  int OldPC;
  int loop;
  unsigned char TempPSR;
  for(loop=0;loop<512;loop++)
  {
    /* Read an instruction and post inc program couter */
    PrePC=ProgramCounter;
    CurrentInstruction=ReadPaged(ProgramCounter++);
    Cycles=CyclesTable[CurrentInstruction]; 

#ifdef MONITOR_INSTR_CALL_FREQUENCY
    Stats[CurrentInstruction]++;
#endif

    OldPC=ProgramCounter;
    Carried=0; Branched=0;
    /**** if ((ProgramCounter>=0x600) && (ProgramCounter<0x700))
     *    {
     ****/
#ifdef USE_INSTR_LOG
    if ((logInstr) && (PagedRomReg!= 15) && (PrePC>= 0x800) && (PrePC < 0xC00))
    {
      fprintf(core6502instr, "(Rom0X) - %4x %02x %02x %02x %02x\n",
      		PagedRomReg,
		PrePC, CurrentInstruction,
		ReadPaged(ProgramCounter), ReadPaged(ProgramCounter+1),
		YReg);
      fflush(core6502instr);
    }
#endif
    /**** } ****/

    BadCount=0;

    if (OpCodes >= 1)
    { // Documented opcodes
      switch (CurrentInstruction)
      {
	case 0x00:
	  BRKInstrHandler();
	  break;
	case 0x01:
	  ORAInstrHandler(IndXAddrModeHandler_Data());
	  break;
	case 0x04:
	  if (MachineType==1)
	    TSBInstrHandler(ZeroPgAddrModeHandler_Address());
	  else
	    ProgramCounter+=1;
	  break;
	case 0x05:
	  ORAInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0x06:
	  ASLInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
	case 0x08:
	  Push(PSR); /* PHP */
	  break;
	case 0x09:
	  ORAInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0x0a:
	  ASLInstrHandler_Acc();
	  break;
	case 0x0c:
	  if (MachineType==1)
	    TSBInstrHandler(AbsAddrModeHandler_Address());
	  else
	    ProgramCounter+=2;
	  break;
	case 0x0d:
	  ORAInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0x0e:
	  ASLInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0x10:
	  BPLInstrHandler();
	  break;
	case 0x30:
	  BMIInstrHandler();
	  break;
	case 0x50:
	  BVCInstrHandler();
	  break;
	case 0x70:
	  BVSInstrHandler();
	  break;
	case 0x80:
	  BRAInstrHandler();
	  break;
	case 0x90:
	  BCCInstrHandler();
	  break;
	case 0xb0:
	  BCSInstrHandler();
	  break;
	case 0xd0:
	  BNEInstrHandler();
	  break;
	case 0xf0:
	  BEQInstrHandler();
	  break;
	case 0x11:
	  ORAInstrHandler(IndYAddrModeHandler_Data());
	  break;
	case 0x12:
	  if (MachineType==1)
	    ORAInstrHandler(ZPIndAddrModeHandler_Data());
	  break;
	case 0x14:
	  if (MachineType==1)
	    TRBInstrHandler(ZeroPgAddrModeHandler_Address());
	  else
	    ProgramCounter+=1;
	  break;
	case 0x15:
	  ORAInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0x16:
	  ASLInstrHandler(ZeroPgXAddrModeHandler_Address());
	  break;
	case 0x18:
	  PSR&=255-FlagC; /* CLC */
	  break;
	case 0x19:
	  ORAInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0x1a:
	  if (MachineType==1) INAInstrHandler();
	  break;
	case 0x1c:
	  if (MachineType==1)
	    TRBInstrHandler(AbsAddrModeHandler_Address());
	  else
	    ProgramCounter+=2;
	  break;
	case 0x1d:
	  ORAInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0x1e:
	  ASLInstrHandler(AbsXAddrModeHandler_Address());
	  break;
	case 0x20:
	  JSRInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0x21:
	  ANDInstrHandler(IndXAddrModeHandler_Data());
	  break;
	case 0x24:
	  BITInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0x25:
	  ANDInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0x26:
	  ROLInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
	case 0x28:
	  PSR=Pop(); /* PLP */
	  break;
	case 0x29:
	  ANDInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0x2a:
	  ROLInstrHandler_Acc();
	  break;
	case 0x2c:
	  BITInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0x2d:
	  ANDInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0x2e:
	  ROLInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0x31:
	  ANDInstrHandler(IndYAddrModeHandler_Data());
	  break;
	case 0x32:
	  if (MachineType==1)
	    ANDInstrHandler(ZPIndAddrModeHandler_Data());
	  break;
	case 0x34: /* BIT Absolute,X */
	  if (MachineType==1)
	    BITInstrHandler(ZeroPgXAddrModeHandler_Data());
	  else
	    ProgramCounter+=1;
	  break;
	case 0x35:
	  ANDInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0x36:
	  ROLInstrHandler(ZeroPgXAddrModeHandler_Address());
	  break;
	case 0x38:
	  PSR|=FlagC; /* SEC */
	  break;
	case 0x39:
	  ANDInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0x3a:
	  if (MachineType==1) DEAInstrHandler();
	  break;
	case 0x3c: /* BIT Absolute,X */
	  if (MachineType==1)
	    BITInstrHandler(AbsXAddrModeHandler_Data());
	  else
	    ProgramCounter+=2;
	  break;
	case 0x3d:
	  ANDInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0x3e:
	  ROLInstrHandler(AbsXAddrModeHandler_Address());
	  break;
	case 0x40:
	  PSR=Pop(); /* RTI */
	  ProgramCounter=PopWord();
	  NMILock=0;
	  break;
	case 0x41:
	  EORInstrHandler(IndXAddrModeHandler_Data());
	  break;
	case 0x45:
	  EORInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0x46:
	  LSRInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
	case 0x48:
	  Push(Accumulator); /* PHA */
	  break;
	case 0x49:
	  EORInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0x4a:
	  LSRInstrHandler_Acc();
	  break;
	case 0x4c:
	  ProgramCounter=AbsAddrModeHandler_Address(); /* JMP */
	  // Next section of code was put there to debug Elite
/*******************
          if (ProgramCounter==0xffdd)
	  { // OSCLI logging for elite debugging
	    unsigned char *bptr;
	    char pcbuf[256]; char *pcptr=pcbuf;
	    int blk=((YReg*256)+XReg);
	    bptr=WholeRam+((WholeRam[blk+1]*256)+WholeRam[blk]);
	    while((*bptr != 13) && ((pcptr-pcbuf)<254)) {
		    *pcptr=*bptr; pcptr++;bptr++; 
	    } 
	    *pcptr=0;
#ifdef USE_CORE_LOG
	    fprintf(core6502log, "%s\n", pcbuf);
#endif
	  }
          if (ProgramCounter==0xffdd)
	  {
	    char errstr[250];
	    sprintf(errstr,"OSFILE called\n");
	    MessageBox(GETHWND,errstr,"BBC Emulator",MB_OKCANCEL|MB_ICONERROR);
	  }
 **********************/

	  break;
	case 0x4d:
	  EORInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0x4e:
	  LSRInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0x51:
	  EORInstrHandler(IndYAddrModeHandler_Data());
	  break;
	case 0x52:
	  if (MachineType==1)
	    EORInstrHandler(ZPIndAddrModeHandler_Data());
	  break;
	case 0x55:
	  EORInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0x56:
	  LSRInstrHandler(ZeroPgXAddrModeHandler_Address());
	  break;
	case 0x58:
	  PSR&=255-FlagI; /* CLI */
	  break;
	case 0x59:
	  EORInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0x5a:
	  if (MachineType==1)
	    Push(YReg); /* PHY */
	  break;
	case 0x5d:
	  EORInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0x5e:
	  LSRInstrHandler(AbsXAddrModeHandler_Address());
	  break;
	case 0x60:
	  ProgramCounter=PopWord()+1; /* RTS */
	  break;
	case 0x61:
	  ADCInstrHandler(IndXAddrModeHandler_Data());
	  break;
	case 0x64:
	  if (MachineType==1)                     /* STZ Zero Page */
	    BEEBWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),0);
	  break;
	case 0x65:
	  ADCInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0x66:
	  RORInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
	case 0x68:
	  Accumulator=Pop(); /* PLA */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((Accumulator==0)<<1) | (Accumulator & 128);
	  break;
	case 0x69:
	  ADCInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0x6a:
	  RORInstrHandler_Acc();
	  break;
	case 0x6c:
	  ProgramCounter=IndAddrModeHandler_Address(); /* JMP */
	  break;
	case 0x6d:
	  ADCInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0x6e:
	  RORInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0x71:
	  ADCInstrHandler(IndYAddrModeHandler_Data());
	  break;
	case 0x72:
	  if (MachineType==1)
	    ADCInstrHandler(ZPIndAddrModeHandler_Data());
	  break;
	case 0x74:
	  if (MachineType==1) /* STZ Zpg,X */
	  {
	    FASTWRITE(ZeroPgXAddrModeHandler_Address(),0);
	  }
	  else
	    ProgramCounter+=1;
	  break;
	case 0x75:
	  ADCInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0x76:
	  RORInstrHandler(ZeroPgXAddrModeHandler_Address());
	  break;
	case 0x78:
	  PSR|=FlagI; /* SEI */
	  break;
	case 0x79:
	  ADCInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0x7a:
	  if (MachineType==1)
	  {
	    YReg=Pop(); /* PLY */
	    PSR &= ~(FlagZ | FlagN);
	    PSR |= ((YReg==0)<<1) | (YReg & 128);
	  }
	  break;
	case 0x7c:
	  if (MachineType==1)
	    ProgramCounter=IndAddrXModeHandler_Address(); /* JMP abs,X*/
	  else
	    ProgramCounter+=2;
	  break;
	case 0x7d:
	  ADCInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0x7e:
	  RORInstrHandler(AbsXAddrModeHandler_Address());
	  break;
	case 0x81:
	  WritePaged(IndXAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x84:
	  BEEBWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),YReg);
	  break;
	case 0x85:
	  BEEBWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x86:
	  BEEBWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),XReg);
	  break;
	case 0x88:
	  YReg=(YReg-1) & 255; /* DEY */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((YReg==0)<<1) | (YReg & 128);
	  break;
	case 0x89: /* BIT Immediate */
	  if (MachineType==1)
	  {
	    TempPSR=PSR&192;
	    BITInstrHandler(ReadPaged(ProgramCounter++));
	    PSR=(PSR&63)|TempPSR;
	  }
	  break;
	case 0x8a:
	  Accumulator=XReg; /* TXA */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((Accumulator==0)<<1) | (Accumulator & 128);
	  break;
	case 0x8c:
	  STYInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0x8d:
	  WritePaged(AbsAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x8e:
	  STXInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0x91:
	  WritePaged(IndYAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x92:
	  if (MachineType==1)
	    WritePaged(ZPIndAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x94:
	  STYInstrHandler(ZeroPgXAddrModeHandler_Address());
	  break;
	case 0x95:
	  WritePaged(ZeroPgXAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x96:
	  STXInstrHandler(ZeroPgYAddrModeHandler_Address());
	  break;
	case 0x98:
	  Accumulator=YReg; /* TYA */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((Accumulator==0)<<1) | (Accumulator & 128);
	  break;
	case 0x99:
	  WritePaged(AbsYAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x9a:
	  StackReg=XReg; /* TXS */
	  break;
	case 0x9c:
	  WritePaged(AbsAddrModeHandler_Address(),0); /* STZ Absolute */
	  /* Here is a curiosity - STZ Abs is on the 6502 unofficially, and
	   * is on the 65C12 officially. This is something that we need to
	   * know. - Richard Gellman */
	  break;
	case 0x9d:
	  WritePaged(AbsXAddrModeHandler_Address(),Accumulator); /* STA */
	  break;
	case 0x9e:
	  if (MachineType==1)
	  {
	    WritePaged(AbsXAddrModeHandler_Address(), 0); /* STZ Abs,X */ 
	  }
	  else
	    WritePaged(AbsXAddrModeHandler_Address(), Accumulator & XReg);
	  break;
	case 0xa0:
	  LDYInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0xa1:
	  LDAInstrHandler(IndXAddrModeHandler_Data());
	  break;
	case 0xa2:
	  LDXInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0xa4:
	  LDYInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0xa5:
	  LDAInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0xa6:
	  LDXInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0xa8:
	  YReg=Accumulator; /* TAY */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((Accumulator==0)<<1) | (Accumulator & 128);
	  break;
	case 0xa9:
	  LDAInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0xaa:
	  XReg=Accumulator; /* TXA */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((Accumulator==0)<<1) | (Accumulator & 128);
	  break;
	case 0xac:
	  LDYInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0xad:
	  LDAInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0xae:
	  LDXInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0xb1:
	  LDAInstrHandler(IndYAddrModeHandler_Data());
	  break;
	case 0xb2:
	  if (MachineType==1)
	    LDAInstrHandler(ZPIndAddrModeHandler_Data());
	  break;
	case 0xb4:
	  LDYInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0xb5:
	  LDAInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0xb6:
	  LDXInstrHandler(ZeroPgYAddrModeHandler_Data());
	  break;
	case 0xb8:
	  PSR&=255-FlagV; /* CLV */
	  break;
	case 0xb9:
	  LDAInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0xba:
	  XReg=StackReg; /* TSX */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((XReg==0)<<1) | (XReg & 128);
	  break;
	case 0xbc:
	  LDYInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0xbd:
	  LDAInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0xbe:
	  LDXInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0xc0:
	  CPYInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0xc1:
	  CMPInstrHandler(IndXAddrModeHandler_Data());
	  break;
	case 0xc4:
	  CPYInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0xc5:
	  CMPInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0xc6:
	  DECInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
	case 0xc8:
	  YReg+=1; /* INY */
	  YReg&=255;
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((YReg==0)<<1) | (YReg & 128);
	  break;
	case 0xc9:
	  CMPInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0xca:
	  DEXInstrHandler();
	  break;
	case 0xcc:
	  CPYInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0xcd:
	  CMPInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0xce:
	  DECInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0xd1:
	  CMPInstrHandler(IndYAddrModeHandler_Data());
	  break;
	case 0xd2:
	  if (MachineType==1)
	    CMPInstrHandler(ZPIndAddrModeHandler_Data());
	  break;
	case 0xd5:
	  CMPInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0xd6:
	  DECInstrHandler(ZeroPgXAddrModeHandler_Address());
	  break;
	case 0xd8:
	  PSR&=255-FlagD; /* CLD */
	  break;
	case 0xd9:
	  CMPInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0xda:
	  if (MachineType==1)
	    Push(XReg); /* PHX */
	  break;
	case 0xdd:
	  CMPInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0xde:
	  DECInstrHandler(AbsXAddrModeHandler_Address());
	  break;
	case 0xe0:
	  CPXInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0xe1:
	  SBCInstrHandler(IndXAddrModeHandler_Data());
	  break;
	case 0xe4:
	  CPXInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0xe5:
	  SBCInstrHandler(WholeRam[ReadPaged(ProgramCounter++)]/*zp */);
	  break;
	case 0xe6:
	  INCInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
	case 0xe8:
	  INXInstrHandler();
	  break;
	case 0xe9:
	  SBCInstrHandler(ReadPaged(ProgramCounter++)); /* immediate */
	  break;
	case 0xea:
	  /* NOP */
	  break;
	case 0xec:
	  CPXInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0xed:
	  SBCInstrHandler(AbsAddrModeHandler_Data());
	  break;
	case 0xee:
	  INCInstrHandler(AbsAddrModeHandler_Address());
	  break;
	case 0xf1:
	  SBCInstrHandler(IndYAddrModeHandler_Data());
	  break;
	case 0xf2:
	  if (MachineType==1)
	    SBCInstrHandler(ZPIndAddrModeHandler_Data());
	  break;
	case 0xf5:
	  SBCInstrHandler(ZeroPgXAddrModeHandler_Data());
	  break;
	case 0xf6:
	  INCInstrHandler(ZeroPgXAddrModeHandler_Address());
	  break;
	case 0xf8:
	  PSR|=FlagD; /* SED */
	  break;
	case 0xf9:
	  SBCInstrHandler(AbsYAddrModeHandler_Data());
	  break;
	case 0xfa:
	  if (MachineType==1)
	  {
	    XReg=Pop(); /* PLX */
	    PSR&=~(FlagZ | FlagN);
	    PSR|=((XReg==0)<<1) | (XReg & 128);
	  }
	  break;
	case 0xfd:
	  SBCInstrHandler(AbsXAddrModeHandler_Data());
	  break;
	case 0xfe:
	  INCInstrHandler(AbsXAddrModeHandler_Address());
	  break;
	default:
	  BadCount++;
      }
    }
    if (OpCodes==3)
    {
      switch (CurrentInstruction)
      {
	case 0x07: /* Undocumented Instruction: ASL zp and ORA zp */
	  {
	    int16 zpaddr = ZeroPgAddrModeHandler_Address();
	    ASLInstrHandler(zpaddr);
	    ORAInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x03: /* Undocumented Instruction: ASL-ORA (zp,X) */
	  {
	    int16 zpaddr = IndXAddrModeHandler_Address();
	    ASLInstrHandler(zpaddr);
	    ORAInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x13: /* Undocumented Instruction: ASL-ORA (zp),Y */
	  {
	    int16 zpaddr = IndYAddrModeHandler_Address();
	    ASLInstrHandler(zpaddr);
	    ORAInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x0f: /* Undocumented Instruction: ASL-ORA abs */
	  {
	    int16 zpaddr = AbsAddrModeHandler_Address();
	    ASLInstrHandler(zpaddr);
	    ORAInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x17: /* Undocumented Instruction: ASL-ORA zp,X */
	  {
	    int16 zpaddr = ZeroPgXAddrModeHandler_Address();
	    ASLInstrHandler(zpaddr);
	    ORAInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x1b: /* Undocumented Instruction: ASL-ORA abs,Y */
	  {
	    int16 zpaddr = AbsYAddrModeHandler_Address();
	    ASLInstrHandler(zpaddr);
	    ORAInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x1f: /* Undocumented Instruction: ASL-ORA abs,X */
	  {
	    int16 zpaddr = AbsXAddrModeHandler_Address();
	    ASLInstrHandler(zpaddr);
	    ORAInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x23: /* Undocumented Instruction: ROL-AND (zp,X) */
	  {
	    int16 zpaddr=IndXAddrModeHandler_Address();
	    ROLInstrHandler(zpaddr);
	    ANDInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x27: /* Undocumented Instruction: ROL-AND zp */
	  {
	    int16 zpaddr=ZeroPgAddrModeHandler_Address();
	    ROLInstrHandler(zpaddr);
	    ANDInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x2f: /* Undocumented Instruction: ROL-AND abs */
	  {
	    int16 zpaddr=AbsAddrModeHandler_Address();
	    ROLInstrHandler(zpaddr);
	    ANDInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x33: /* Undocumented Instruction: ROL-AND (zp),Y */
	  {
	    int16 zpaddr=IndYAddrModeHandler_Address();
	    ROLInstrHandler(zpaddr);
	    ANDInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x37: /* Undocumented Instruction: ROL-AND zp,X */
	  {
	    int16 zpaddr=ZeroPgXAddrModeHandler_Address();
	    ROLInstrHandler(zpaddr);
	    ANDInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x3b: /* Undocumented Instruction: ROL-AND abs.Y */
	  {
	    int16 zpaddr=AbsYAddrModeHandler_Address();
	    ROLInstrHandler(zpaddr);
	    ANDInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x3f: /* Undocumented Instruction: ROL-AND abs.X */
	  {
	    int16 zpaddr=AbsXAddrModeHandler_Address();
	    ROLInstrHandler(zpaddr);
	    ANDInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x43: /* Undocumented Instruction: LSR-EOR (zp,X) */
	  {
	    int16 zpaddr=IndXAddrModeHandler_Address();
	    LSRInstrHandler(zpaddr);
	    EORInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x47: /* Undocumented Instruction: LSR-EOR zp */
	  {
	    int16 zpaddr=ZeroPgAddrModeHandler_Address();
	    LSRInstrHandler(zpaddr);
	    EORInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x4f: /* Undocumented Instruction: LSR-EOR abs */
	  {
	    int16 zpaddr=AbsAddrModeHandler_Address();
	    LSRInstrHandler(zpaddr);
	    EORInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x53: /* Undocumented Instruction: LSR-EOR (zp),Y */
	  {
	    int16 zpaddr=IndYAddrModeHandler_Address();
	    LSRInstrHandler(zpaddr);
	    EORInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x57: /* Undocumented Instruction: LSR-EOR zp,X */
	  {
	    int16 zpaddr=ZeroPgXAddrModeHandler_Address();
	    LSRInstrHandler(zpaddr);
	    EORInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x5b: /* Undocumented Instruction: LSR-EOR abs,Y */
	  {
	    int16 zpaddr=AbsYAddrModeHandler_Address();
	    LSRInstrHandler(zpaddr);
	    EORInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x5f: /* Undocumented Instruction: LSR-EOR abs,X */
	  {
	    int16 zpaddr=AbsXAddrModeHandler_Address();
	    LSRInstrHandler(zpaddr);
	    EORInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x44:
	case 0x54:
	  ProgramCounter+=1;
	  break;
	case 0x5c:
	  ProgramCounter+=2;
	  break;
	case 0x63: /* Undocumented Instruction: ROR-ADC (zp,X) */
	  {
	    int16 zpaddr=IndXAddrModeHandler_Address();
	    RORInstrHandler(zpaddr);
	    ADCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x67: /* Undocumented Instruction: ROR-ADC zp */
	  {
	    int16 zpaddr=ZeroPgAddrModeHandler_Address();
	    RORInstrHandler(zpaddr);
	    ADCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x6f: /* Undocumented Instruction: ROR-ADC abs */
	  {
	    int16 zpaddr=AbsAddrModeHandler_Address();
	    RORInstrHandler(zpaddr);
	    ADCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x73: /* Undocumented Instruction: ROR-ADC (zp),Y */
	  {
	    int16 zpaddr=IndYAddrModeHandler_Address();
	    RORInstrHandler(zpaddr);
	    ADCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x77: /* Undocumented Instruction: ROR-ADC zp,X */
	  {
	    int16 zpaddr=ZeroPgXAddrModeHandler_Address();
	    RORInstrHandler(zpaddr);
	    ADCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x7b: /* Undocumented Instruction: ROR-ADC abs,Y */
	  {
	    int16 zpaddr=AbsYAddrModeHandler_Address();
	    RORInstrHandler(zpaddr);
	    ADCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0x7f: /* Undocumented Instruction: ROR-ADC abs,X */
	  {
	    int16 zpaddr=AbsXAddrModeHandler_Address();
	    RORInstrHandler(zpaddr);
	    ADCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	// Undocumented DEC-CMP and INC-SBC Instructions
	case 0xc3: // DEC-CMP (zp,X)
	  {
	    int16 zpaddr=IndXAddrModeHandler_Address();
	    DECInstrHandler(zpaddr);
	    CMPInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xc7: // DEC-CMP zp
	  {
	    int16 zpaddr=ZeroPgAddrModeHandler_Address();
	    DECInstrHandler(zpaddr);
	    CMPInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xcf: // DEC-CMP abs
	  {
	    int16 zpaddr=AbsAddrModeHandler_Address();
	    DECInstrHandler(zpaddr);
	    CMPInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xd3: // DEC-CMP (zp),Y
	  {
	    int16 zpaddr=IndYAddrModeHandler_Address();
	    DECInstrHandler(zpaddr);
	    CMPInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xd7: // DEC-CMP zp,X
	  {
	    int16 zpaddr=ZeroPgXAddrModeHandler_Address();
	    DECInstrHandler(zpaddr);
	    CMPInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xdb: // DEC-CMP abs,Y
	  {
	    int16 zpaddr=AbsYAddrModeHandler_Address();
	    DECInstrHandler(zpaddr);
	    CMPInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xdf: // DEC-CMP abs,X
	  {
	    int16 zpaddr=AbsXAddrModeHandler_Address();
	    DECInstrHandler(zpaddr);
	    CMPInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xd4:
	case 0xf4:
	  ProgramCounter+=1;
	  break;
	case 0xdc:
	case 0xfc:
	  ProgramCounter+=2;
	  break;
	case 0xe3: // INC-SBC (zp,X)
	  {
	    int16 zpaddr=IndXAddrModeHandler_Address();
	    INCInstrHandler(zpaddr);
	    SBCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xe7: // INC-SBC zp
	  {
	    int16 zpaddr=ZeroPgAddrModeHandler_Address();
	    INCInstrHandler(zpaddr);
	    SBCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xef: // INC-SBC abs
	  {
	    int16 zpaddr=AbsAddrModeHandler_Address();
	    INCInstrHandler(zpaddr);
	    SBCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xf3: // INC-SBC (zp).Y
	  {
	    int16 zpaddr=IndYAddrModeHandler_Address();
	    INCInstrHandler(zpaddr);
	    SBCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xf7: // INC-SBC zp,X
	  {
	    int16 zpaddr=ZeroPgXAddrModeHandler_Address();
	    INCInstrHandler(zpaddr);
	    SBCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xfb: // INC-SBC abs,Y
	  {
	    int16 zpaddr=AbsYAddrModeHandler_Address();
	    INCInstrHandler(zpaddr);
	    SBCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	case 0xff: // INC-SBC abs,X
	  {
	    int16 zpaddr=AbsXAddrModeHandler_Address();
	    INCInstrHandler(zpaddr);
	    SBCInstrHandler(WholeRam[zpaddr]);
	  }
	  break;
	    // REALLY Undocumented instructions 6B, 8B and CB
	case 0x6b:
	  ANDInstrHandler(WholeRam[ProgramCounter++]);
	  RORInstrHandler_Acc();
	  break;
	case 0x8b:
	  Accumulator=XReg; /* TXA */
	  PSR&=~(FlagZ | FlagN);
	  PSR|=((Accumulator==0)<<1) | (Accumulator & 128);
	  ANDInstrHandler(WholeRam[ProgramCounter++]);
	  break;
	case 0xcb:
	  // SBX #n - I dont know if this uses the carry or not, i'm assuming its
	  // Subtract #n from X with carry.
	  {
	    unsigned char TmpAcc=Accumulator;
	    Accumulator=XReg;
	    SBCInstrHandler(WholeRam[ProgramCounter++]);
	    XReg=Accumulator;
	    Accumulator=TmpAcc; // Fudge so that I dont have to do the whole SBC code again
	  }
	  break;
	default:
	  BadCount++;
	  break;
      }
    }
    if (OpCodes>=2)
    {
      switch (CurrentInstruction)
      {
	case 0x0b:
	case 0x2b:
	  ANDInstrHandler(WholeRam[ProgramCounter++]); /* AND-MVC #n,b7 */
	  PSR|=((Accumulator & 128)>>7);
	  break;
	case 0x4b: /* Undocumented Instruction: AND imm and LSR A */
	  ANDInstrHandler(WholeRam[ProgramCounter++]);
	  LSRInstrHandler_Acc();
	  break;
	case 0x87: /* Undocumented Instruction: SAX zp (i.e. (zp) = A & X) */
	  /* This one does not seem to change the processor flags */
	  WholeRam[ZeroPgAddrModeHandler_Address()] = Accumulator & XReg;
	  break;
	case 0x83: /* Undocumented Instruction: SAX (zp,X) */
	  WholeRam[IndXAddrModeHandler_Address()] = Accumulator & XReg;
	  break;
	case 0x8f: /* Undocumented Instruction: SAX abs */
	  WholeRam[AbsAddrModeHandler_Address()] = Accumulator & XReg;
	  break;
	case 0x93: /* Undocumented Instruction: SAX (zp),Y */
	  WholeRam[IndYAddrModeHandler_Address()] = Accumulator & XReg;
	  break;
	case 0x97: /* Undocumented Instruction: SAX zp,Y */
	  WholeRam[ZeroPgYAddrModeHandler_Address()] = Accumulator & XReg;
	  break;
	case 0x9b: /* Undocumented Instruction: SAX abs,Y */
	  WholeRam[AbsYAddrModeHandler_Address()] = Accumulator & XReg;
	  break;
	case 0x9f: /* Undocumented Instruction: SAX abs,X */
	  WholeRam[AbsXAddrModeHandler_Address()] = Accumulator & XReg;
	  break;
	case 0xab: /* Undocumented Instruction: LAX #n */
	  LDAInstrHandler(WholeRam[ProgramCounter++]);
	  XReg = Accumulator;
	  break;
	case 0xa3: /* Undocumented Instruction: LAX (zp,X) */
	  LDAInstrHandler(IndXAddrModeHandler_Data());
	  XReg = Accumulator;
	  break;
	case 0xa7: /* Undocumented Instruction: LAX zp */
	  LDAInstrHandler(WholeRam[WholeRam[ProgramCounter++]]);
	  XReg = Accumulator;
	  break;
	case 0xaf: /* Undocumented Instruction: LAX abs */
	  LDAInstrHandler(AbsAddrModeHandler_Data());
	  XReg = Accumulator;
	  break;
	case 0xb3: /* Undocumented Instruction: LAX (zp),Y */
	  LDAInstrHandler(IndYAddrModeHandler_Data());
	  XReg = Accumulator;
	  break;
	case 0xb7: /* Undocumented Instruction: LAX zp,Y */
	  LDXInstrHandler(ZeroPgYAddrModeHandler_Data());
	  Accumulator = XReg;
	  break;
	case 0xbb:
	case 0xbf: /* Undocumented Instruction: LAX abs,Y */
	  LDAInstrHandler(AbsYAddrModeHandler_Data());
	  XReg = Accumulator;
	  break;
	default:
	  BadCount++;
      }
    }
    if (BadCount==OpCodes)
      BadInstrHandler(CurrentInstruction);

    // Although this is a null statement, I have left it in for the
    // time being - D.M.Sergeant [17/10/2002]
    ; /* OpCode switch */

    // This block corrects the cycle count for the branch instructions
    if ((CurrentInstruction==0x10) || (CurrentInstruction==0x30) ||
	(CurrentInstruction==0x50) || (CurrentInstruction==0x70) ||
	(CurrentInstruction==0x80) || (CurrentInstruction==0x90) ||
	(CurrentInstruction==0xb0) || (CurrentInstruction==0xd0) ||
	(CurrentInstruction==0xf0))
    {
      if (((ProgramCounter & 0xff00)!=(OldPC & 0xff00)) && (Branched==1)) 
	Cycles+=2;
      else Cycles++;
    }
    if (((CurrentInstruction & 0xf)==1) || ((CurrentInstruction & 0xf)==9) ||
	((CurrentInstruction & 0xf)==0xD))
    {
      if (((CurrentInstruction &0x10)==0) &&
          ((CurrentInstruction &0xf0)!=0x90) && (Carried==1))
	Cycles++;
    }
    if (((CurrentInstruction==0xBC) || (CurrentInstruction==0xBE)) &&
        (Carried==1))
      Cycles++;
    Cycles+=IRQCycles;
    IRQCycles=0; // IRQ Timing
    // End of cycle correction

    OldNMIStatus=NMIStatus;
    /* NOTE: Check IRQ status before polling hardware - this is essential
     * for Rocket Raid to work since it polls the IFR in the sys via for
     * start of frame - but with interrupts enabled.  If you do the interrupt
     * check later then the interrupt handler will always be entered and
     * rocket raid will never see it */
    if (TubeEnabled)
      SyncTubeProcessor();
    if ((intStatus) && (!GETIFLAG))
      DoInterrupt();
    TotalCycles+=Cycles;
    SecCycles+=Cycles;

    SysVIA_poll(Cycles);
    Sound_Trigger(Cycles);
    UserVIA_poll(Cycles);
    VideoPoll(Cycles);
    Serial_Poll();
    SCSI_Poll();
    LVROM_Poll(SecCycles);
    if (TotalCycles > VFrameTrigger)
    { 
      NextVideoPoll();
      VFrameTrigger=TotalCycles+80000;
    }
    // Sound_Trigger(Cycles);
    if (DisplayCycles > 0)
      DisplayCycles-=Cycles; // Countdown time till end of display of info.
    if ((MachineType==1) || (!NativeFDC))
      Poll1770(Cycles); // Do 1770 Background stuff
    if ((NMIStatus) && (!OldNMIStatus))
      DoNMI();
  }
} /* Exec6502Instruction */

/*-------------------------------------------------------------------------*/
void Save6502State(unsigned char *CPUState)
{
  CPUState[0] = ProgramCounter & 255;
  CPUState[1] = (ProgramCounter >> 8) & 255;
  CPUState[2] = Accumulator;
  CPUState[3] = XReg;
  CPUState[4] = YReg;
  CPUState[5] = StackReg;
  CPUState[6] = PSR;
}

void Save6502UEF(FILE *SUEF)
{
  fput16(0x0460,SUEF);
  fput32(16,SUEF);
  fput16(ProgramCounter,SUEF);
  fputc(Accumulator,SUEF);
  fputc(XReg,SUEF);
  fputc(YReg,SUEF);
  fputc(StackReg,SUEF);
  fputc(PSR,SUEF);
  fput32(TotalCycles,SUEF);
  fputc(intStatus,SUEF);
  fputc(NMIStatus,SUEF);
  fputc(NMILock,SUEF);
  fput16(0,SUEF);
}

void Load6502UEF(FILE *SUEF)
{
  int Dlong;
  ProgramCounter=fget16(SUEF);
  Accumulator=fgetc(SUEF);
  XReg=fgetc(SUEF);
  YReg=fgetc(SUEF);
  StackReg=fgetc(SUEF);
  PSR=fgetc(SUEF);
  //TotalCycles=fget32(SUEF);
  Dlong=fget32(SUEF);
  intStatus=fgetc(SUEF);
  NMIStatus=fgetc(SUEF);
  NMILock=fgetc(SUEF);
  //AtoDTrigger=Disc8271Trigger=AMXTrigger=PrinterTrigger=VideoTriggerCount=TotalCycles+100;
  //if (UseHostClock) SoundTrigger=TotalCycles+100;
  // Make sure emulator doesn't lock up waiting for triggers.

}

/*-------------------------------------------------------------------------*/
void Restore6502State(unsigned char *CPUState)
{
  ProgramCounter = CPUState[0] + (CPUState[1] << 8);
  Accumulator = CPUState[2];
  XReg = CPUState[3];
  YReg = CPUState[4];
  StackReg = CPUState[5];
  PSR = CPUState[6];

  /* Reset the other globals as well */
  TotalCycles = 0;
  intStatus = 0;
  NMIStatus = 0;
  NMILock = 0;
}

/*-------------------------------------------------------------------------*/
/* Dump state                                                              */
void core_dumpstate(void)
{
  cerr << "core:\n";
  DumpRegs();
} /* core_dumpstate */

