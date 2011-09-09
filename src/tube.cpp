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
/* Copied for 65C02 Tube core - 13/04/01 */

#include <iostream.h>
#include <stdio.h>
#include <stdlib.h>

#include "6502core.h"
#include "beebmem.h"
#include "main.h"
#include "tube.h"
#include "debugLogs.h"

#define INLINE static

#define TubeCPUDebug

#ifdef USE_TUBEINST_LOG
FILE *tinstrlog=NULL;
#endif
#ifdef USE_TUBE_LOG
FILE *tubelog=NULL;
#endif

class TubeRegister {
public:
	int Limit;
	int Type;

unsigned char TubeRegister::GetStatusByte() {
	if ((!Type) || ((Type) && (Limit==1))) return(((Pointer>0)?128:0)|((Pointer==Limit)?0:64)|63);
	else return(R3A | R3F | 63);
}

void TubeRegister::WriteData(unsigned char IOData) {
	// if the pointer is not equal to limit, add it on the end
	if (Pointer<Limit) {
		Register[Pointer]=IOData;
		Pointer++;
		if (Pointer==Limit) { R3A=128; R3F=0; }
	}
}

unsigned char TubeRegister::ReadData() {
	unsigned char TmpData;
	unsigned char TmpReg[25];
	if (Pointer>0) {
		TmpData=Register[0];
		memcpy(TmpReg,Register+1,25);
		memcpy(Register,TmpReg,25);
		Pointer--;
		if (Pointer==0) { R3A=0; R3F=64; }
	} else TmpData=0;
	return(TmpData);
}

TubeRegister::TubeRegister() {
	Limit=1;
	Pointer=0;
	memset(Register,0,25);
}

void Reset() {
	Limit=1;
	Pointer=0;
	memset(Register,0,25);
}

private:
	int Pointer;
	unsigned char Register[25];
	int R3A,R3F;
};

unsigned char TubeRam[65536];
unsigned char ROMSpace[65536];
unsigned char TubeEnabled,EnableTube;

__int64 TotalTubeCycles=0;  
int Paccum;

int TubeProgramCounter;
int TubeAccumulator,TubeXReg,TubeYReg;
unsigned char TubeStackReg;
unsigned char TubePSR;
int IRQCycles;

unsigned char TubeintStatus=0; /* bit set (nums in IRQ_Nums) if interrupt being caused */
unsigned char TubeNMIStatus=0; /* bit set (nums in NMI_Nums) if NMI being caused */
unsigned int TubeNMILock=0; /* Well I think NMI's are maskable - to stop repeated NMI's - the lock is released when an RTI is done */

int IDump=0;
int route;


int UseROM;
unsigned char NMI_Accumulator,NMI_X,NMI_Y,NMI_P;
unsigned int NMI_SP;

typedef int int16;
/* Stats */

enum PSRFlags {
  FlagC=1,
  FlagZ=2,
  FlagI=4,
  FlagD=8,
  FlagB=16,
  FlagV=64,
  FlagN=128
};

/* Note how GETCTFLAG is special since being bit 0 we don't need to test it to get a clean 0/1 */
#define GETCTFLAG ((TubePSR & FlagC))
#define GETZTFLAG ((TubePSR & FlagZ)>0)
#define GETITFLAG ((TubePSR & FlagI)>0)
#define GETDTFLAG ((TubePSR & FlagD)>0)
#define GETBTFLAG ((TubePSR & FlagB)>0)
#define GETVTFLAG ((TubePSR & FlagV)>0)
#define GETNTFLAG ((TubePSR & FlagN)>0)

/* Types for internal function arrays */
typedef void (*InstrHandlerFuncType)(int16 Operand);
typedef int16 (*AddrModeHandlerFuncType)(int WantsAddr);

int TubeCyclesTable[]={
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
}; /* TubeCyclesTable */

/* The number of TubeCycles to be used by the current instruction - exported to
   allow fernangling by memory subsystem */
unsigned int TubeCycles;

unsigned char TubeBranched,TubeCarried;
// Branched - 1 if the instruction branched
// TubeCarried - 1 if the instruction TubeCarried over to high byte in index calculation
// 1 if first cycle happened

/* A macro to speed up writes - uses a local variable called 'tmpaddr' */
#define TUBEREADMEM_FAST(a) TubeReadMem(a)
#define TUBEREADMEM_FASTINC(a) TubeReadMem(a++)
#define TUBEWRITEMEM_FAST(Address, Value) TubeWriteMem(Address,Value);
#define TUBEWRITEMEM_DIRECT(Address, Value) TubeWriteMem(Address,Value);
#define TUBEFASTWRITE(addr,val) tmpaddr=addr; TubeWriteMem(tmpaddr,val);

// Tube memory/io handling functions

TubeRegister R1_HP,R1_PH,R2_HP,R2_PH,R3_HP,R3_PH,R4_HP,R4_PH;
static int T,P,V,M,J,I,Q,K;
void TubeReset();
unsigned char TubeStatus;

/*void logroute(char *routetype) {
	return;
	for (int n=0;n<(route+10);n++) fprintf(TubeLog," ");
	fprintf(TubeLog,"%s at %04x\n",routetype,TubeProgramCounter-1);
}*/

unsigned char TubeN;

void R3Update() {
	TubeNMIStatus=0;
	TubeN=0;
	if (R3_HP.GetStatusByte() & 128) TubeN|=128;
	if (!(R3_PH.GetStatusByte() & 128)) TubeN|=128;
	if (M) {
		TubeNMIStatus=TubeN;
	}
}

// this is a macro to combine status bits
// it merges bit 7 of the first with bit 6 of the second
#define MKSTAT(A,B) ((A.GetStatusByte()&128)|(B.GetStatusByte()&64)|63)

unsigned char R2_H_S;

unsigned char ReadTubeFromHostSide(unsigned char IOAddr) {
	unsigned char TmpData;
	if (IOAddr==0) { 
		TmpData=MKSTAT(R1_PH,R1_HP);
		TmpData&=192;
		TmpData|=(TubeStatus & 63);
		return(TmpData); 
	}
	if (IOAddr==1) return(R1_PH.ReadData());
	if (IOAddr==2) return(MKSTAT(R2_PH,R2_HP));
	if (IOAddr==3) { TmpData=R2_PH.ReadData(); R2_H_S=MKSTAT(R2_PH,R2_HP); return(TmpData); }
	if (IOAddr==4) {
		TmpData=MKSTAT(R3_PH,R3_HP);
		return(TmpData);
	}
	if (IOAddr==5) {
		TmpData=R3_PH.ReadData();
		R3Update();
		return(TmpData);
	}
	if (IOAddr==6) return(MKSTAT(R4_PH,R4_HP));
	if (IOAddr==7) {
		if (Q) intStatus&=~(1<<tube);
		return(R4_PH.ReadData());
	}
	return(0);
}


void WriteTubeFromHostSide(unsigned char IOAddr,unsigned char IOData) {
	static unsigned char TmpData,n;
	if (IOAddr==0) {
		// Why cant ppl just store the bits and be done with it?
		for (n=1;n<128;n*=2) {
			if (IOData & n) {
				if (IOData & 128) TubeStatus|=n; else TubeStatus&=~n;
			}
		} 
		T=TubeStatus & 64;
		P=TubeStatus & 32;
		V=(TubeStatus & 16)>>5;
		M=TubeStatus & 8;
		J=TubeStatus & 4;
		I=TubeStatus & 2;
		Q=TubeStatus & 1;
		if ((T) || (P)) TmpData=TubeStatus;
		if (T) { TubeReset(); }
		if (P) { Init65C02core(); }
		if ((T) || (P)) TubeStatus=TmpData & 63;
		if (!I) TubeintStatus&=2;
		if (!J) TubeintStatus&=1;
		if (!Q) intStatus&=~(1<<tube);
		R3_PH.Limit=R3_HP.Limit=1+V;
	}
	if (IOAddr==1) {
		R1_HP.WriteData(IOData);
		if (I) TubeintStatus|=1;
	}
	if (IOAddr==3) {
		R2_HP.WriteData(IOData);
		R2_H_S=MKSTAT(R2_PH,R2_HP);
	}
	if (IOAddr==5) {
		R3_HP.WriteData(IOData);
		R3Update();
	}
	if (IOAddr==7) {
		if (J) TubeintStatus|=2;
		R4_HP.WriteData(IOData);
	}
}

unsigned char ReadTubeFromParasiteSide(unsigned char IOAddr) {
	unsigned char TmpData;
	UseROM=0;
	if (IOAddr==0) { 
		TmpData=MKSTAT(R1_HP,R1_PH);
		TmpData&=192;
		TmpData|=(TubeStatus & 63);
		return(TmpData); 
	}
	if (IOAddr==1) {
		TmpData=(R1_HP.ReadData());
		if (!(R1_HP.GetStatusByte()&128) && I) TubeintStatus&=2;
		return(TmpData);
	}
	if (IOAddr==2) return(MKSTAT(R2_HP,R2_PH));
	if (IOAddr==3) { TmpData=R2_HP.ReadData(); R2_H_S=MKSTAT(R2_PH,R2_HP); return(TmpData); }
	if (IOAddr==4) { // R3STAT
		TmpData=MKSTAT(R3_HP,R3_PH);
		return((TmpData&127)|TubeN);
	}
	if (IOAddr==5) {
		TmpData=R3_HP.ReadData();
		R3Update();
		return(TmpData);
	}
	if (IOAddr==6) return(MKSTAT(R4_HP,R4_PH));
	if (IOAddr==7) {
		TmpData=(R4_HP.ReadData());
		if (J) TubeintStatus&=1;
		return(TmpData);
	}
	return(0);
}

void WriteTubeFromParasiteSide(unsigned char IOAddr,unsigned char IOData) {
	UseROM=0;
	if (IOAddr==1) R1_PH.WriteData(IOData); 
	if (IOAddr==3) { R2_PH.WriteData(IOData); R2_H_S=MKSTAT(R2_PH,R2_HP); }
	if (IOAddr==5) {
		R3_PH.WriteData(IOData);
		R3Update();
	}
	if (IOAddr==7) {
		if (Q) intStatus|=1<<tube;
		R4_PH.WriteData(IOData);
	}
}

void TubeWriteMem(unsigned int IOAddr,unsigned char IOData) {
	if (IOAddr<0xfef0) { TubeRam[IOAddr]=IOData; return; } // write to memory anyways
	if (IOAddr>0xfeff) { TubeRam[IOAddr]=IOData; return; } // same again
	WriteTubeFromParasiteSide(IOAddr&7,IOData); // write to Tube ULA
}

unsigned char TubeReadMem(unsigned int IOAddr) {
	// This is complicated
	if (IOAddr<0x8000) return TubeRam[IOAddr]; // direct read, comes from ram only
	if (IOAddr>=0x8000) {
		if ((IOAddr>=0xfef0) && (IOAddr<=0xfeff)) return ReadTubeFromParasiteSide(IOAddr&7); // Tube ULA
		if (UseROM) {
			return ROMSpace[IOAddr&0xfff];
		} else {
			return TubeRam[IOAddr];
		}
	}
	return 0;
}

/* Get a two byte address from the program counter, and then post inc the program counter */
#define GETTWOBYTEFROMTUBEPC(var) \
  var=TubeReadMem(TubeProgramCounter); \
  var|=TubeReadMem((TubeProgramCounter+1)&0xffff)<<8; \
  var&=0xffff; \
  TubeProgramCounter+=2;

/*----------------------------------------------------------------------------*/
INLINE  int SignExtendByte(signed char in) {
  /*if (in & 0x80) return(in | 0xffffff00); else return(in); */
  /* I think this should sign extend by virtue of the casts - gcc does anyway - the code
  above will definitly do the trick */
  return((int)in);
} /* SignExtendByte */

/*----------------------------------------------------------------------------*/
/* Set the Z flag if 'in' is 0, and N if bit 7 is set - leave all other bits  */
/* untouched.                                                                 */
INLINE  void SetPSRZN(const unsigned char in) {
  TubePSR&=~(FlagZ | FlagN);
  TubePSR|=((in==0)<<1) | (in & 128);
}; /* SetTubePSRZN */

/*----------------------------------------------------------------------------*/
/* Note: n is 128 for true - not 1                                            */
INLINE  void SetPSR(int mask,int c,int z,int i,int d,int b, int v, int n) {
  TubePSR&=~mask;
  TubePSR|=c | (z<<1) | (i<<2) | (d<<3) | (b<<4) | (v<<6) | n;
} /* SetPSR */

/*----------------------------------------------------------------------------*/
/* NOTE!!!!! n is 128 or 0 - not 1 or 0                                       */
INLINE  void SetPSRCZN(int c,int z, int n) {
  TubePSR&=~(FlagC | FlagZ | FlagN);
  TubePSR|=c | (z<<1) | n;
} /* SetPSRCZN */

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
INLINE  void Push(unsigned char ToPush) {
  TubeWriteMem(0x100+TubeStackReg,ToPush);
  TubeStackReg--;
} /* Push */

/*----------------------------------------------------------------------------*/
INLINE  unsigned char Pop(void) {
  TubeStackReg++;
  return(TubeReadMem(0x100+TubeStackReg));
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
  EffectiveAddress=SignExtendByte((signed char)TubeReadMem(TubeProgramCounter++));
  EffectiveAddress+=TubeProgramCounter;
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* RelAddrModeHandler */

/*----------------------------------------------------------------------------*/
INLINE void TubeADCInstrHandler(int16 operand) {
  /* NOTE! Not sure about C and V flags */
  int TmpResultV,TmpResultC;
  if (!GETDTFLAG) {
    TmpResultC=TubeAccumulator+operand+GETCTFLAG;
    TmpResultV=(signed char)TubeAccumulator+(signed char)operand+GETCTFLAG;
    TubeAccumulator=TmpResultC & 255;
    SetPSR(FlagC | FlagZ | FlagV | FlagN, (TmpResultC & 256)>0,TubeAccumulator==0,0,0,0,((TubeAccumulator & 128)>0) ^ (TmpResultV<0),(TubeAccumulator & 128));
  } else {
    int ZFlag=0,NFlag=0,CFlag=0,VFlag=0;
    int TmpResult,TmpCarry=0;
    int ln,hn;

    /* Z flag determined from 2's compl result, not BCD result! */
    TmpResult=TubeAccumulator+operand+GETCTFLAG;
    ZFlag=((TmpResult & 0xff)==0);

    ln=(TubeAccumulator & 0xf)+(operand & 0xf)+GETCTFLAG;
    if (ln>9) {
      ln += 6;
      ln &= 0xf;
      TmpCarry=0x10;
    }
    hn=(TubeAccumulator & 0xf0)+(operand & 0xf0)+TmpCarry;
    /* N and V flags are determined before high nibble is adjusted.
       NOTE: V is not always correct */
    NFlag=hn & 128;
    VFlag=(hn ^ TubeAccumulator) & 128 && !((TubeAccumulator ^ operand) & 128);
    if (hn>0x90) {
      hn += 0x60;
      hn &= 0xf0;
      CFlag=1;
    }
    TubeAccumulator=hn|ln;
    ZFlag=(TubeAccumulator==0);
	NFlag=(TubeAccumulator&128);
	SetPSR(FlagC | FlagZ | FlagV | FlagN,CFlag,ZFlag,0,0,0,VFlag,NFlag);
  }
} /* ADCInstrHandler */

/*----------------------------------------------------------------------------*/
INLINE  void TubeANDInstrHandler(int16 operand) {
  TubeAccumulator=TubeAccumulator & operand;
  TubePSR&=~(FlagZ | FlagN);
  TubePSR|=((TubeAccumulator==0)<<1) | (TubeAccumulator & 128);
} /* ANDInstrHandler */

INLINE  void TubeASLInstrHandler(int16 address) {
  unsigned char oldVal,newVal;
  oldVal=TUBEREADMEM_FAST(address);
  newVal=(((unsigned int)oldVal)<<1)&255;
  TUBEWRITEMEM_FAST(address,newVal);
  SetPSRCZN((oldVal & 128)>0, newVal==0,newVal & 128);
} /* ASLInstrHandler */

INLINE  void TubeTRBInstrHandler(int16 address) {
	unsigned int oldVal,newVal;
	oldVal=TUBEREADMEM_FAST(address);
	newVal=(TubeAccumulator ^ 255) & oldVal;
    TUBEWRITEMEM_FAST(address,newVal);
    TubePSR&=253;
	TubePSR|=((TubeAccumulator & oldVal)==0) ? 2 : 0;
} // TRBInstrHandler

INLINE  void TubeTSBInstrHandler(int16 address) {
	unsigned int oldVal,newVal;
	oldVal=TUBEREADMEM_FAST(address);
	newVal=TubeAccumulator | oldVal;
    TUBEWRITEMEM_FAST(address,newVal);
    TubePSR&=253;
	TubePSR|=((TubeAccumulator & oldVal)==0) ? 2 : 0;
} // TSBInstrHandler

INLINE  void TubeASLInstrHandler_Acc(void) {
  unsigned int oldVal,newVal;
  /* TubeAccumulator */
  oldVal=TubeAccumulator;
  TubeAccumulator=newVal=(((unsigned int)TubeAccumulator)<<1)&255;
  SetPSRCZN((oldVal & 128)>0, newVal==0,newVal & 128);
} /* ASLInstrHandler_Acc */

INLINE  void TubeBCCInstrHandler(void) {
  if (!GETCTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
} /* BCCInstrHandler */

INLINE  void TubeBCSInstrHandler(void) {
  if (GETCTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
} /* BCSInstrHandler */

INLINE  void TubeBEQInstrHandler(void) {
  if (GETZTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
} /* BEQInstrHandler */

INLINE  void TubeBITInstrHandler(int16 operand) {
  TubePSR&=~(FlagZ | FlagN | FlagV);
  /* z if result 0, and NV to top bits of operand */
  TubePSR|=(((TubeAccumulator & operand)==0)<<1) | (operand & 192);
} /* BITInstrHandler */

INLINE  void TubeBMIInstrHandler(void) {
  if (GETNTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
} /* BMIInstrHandler */

INLINE  void TubeBNEInstrHandler(void) {
  if (!GETZTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
} /* BNEInstrHandler */

INLINE  void TubeBPLInstrHandler(void) {
  if (!GETNTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
}; /* BPLInstrHandler */

INLINE  void TubeBRKInstrHandler(void) {
//	char errmsg[200]; int TPC;
//  TPC=TubeProgramCounter-1;
  PushWord(TubeProgramCounter+1);
  SetPSR(FlagB,0,0,0,0,1,0,0); /* Set B before pushing */
  Push(TubePSR);
  SetPSR(FlagI,0,0,1,0,0,0,0); /* Set I after pushing - see Birnbaum */
  TubeProgramCounter=TubeReadMem(0xfffe) | (TubeReadMem(0xffff)<<8);
//  sprintf(errmsg,"Parasite BRK encountered at %04x",TPC);
//  MessageBox(GETHWND,errmsg,"BeebEm",MB_OK);
} /* BRKInstrHandler */

INLINE  void TubeBVCInstrHandler(void) {
  if (!GETVTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
} /* BVCInstrHandler */

INLINE  void TubeBVSInstrHandler(void) {
  if (GETVTFLAG) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
  } else TubeProgramCounter++;
} /* BVSInstrHandler */

INLINE  void TubeBRAInstrHandler(void) {
    TubeProgramCounter=RelAddrModeHandler_Data();
    TubeBranched=1;
} /* BRAnstrHandler */

INLINE void TubeSBCInstrHandler(int16 operand);

INLINE  void TubeCMPInstrHandler(int16 operand) {
  /* NOTE! Should we consult D flag ? */
/*  unsigned char TmpAcc;
  TubePSR|=1;
  TmpAcc=TubeAccumulator;
  TubeSBCInstrHandler(operand);
  TubeAccumulator=TmpAcc;  */
  unsigned char result=TubeAccumulator-operand;
  unsigned char CFlag;
  CFlag=0; if (TubeAccumulator>=operand) CFlag=FlagC;
  SetPSRCZN(CFlag,TubeAccumulator==operand,result & 128);  
} /* CMPInstrHandler */

INLINE  void TubeCPXInstrHandler(int16 operand) {
/*  unsigned char TmpAcc;
  TubePSR|=1;
  TmpAcc=TubeAccumulator;
  TubeAccumulator=TubeXReg;
  TubeSBCInstrHandler(operand);
  TubeAccumulator=TmpAcc; */
  unsigned char result=(TubeXReg-operand);
  SetPSRCZN(TubeXReg>=operand,TubeXReg==operand,result & 128); 
} /* CPXInstrHandler */

INLINE  void TubeCPYInstrHandler(int16 operand) {
/*  unsigned char TmpAcc;
  TubePSR|=1;
  TmpAcc=TubeAccumulator;
  TubeAccumulator=TubeYReg;
  TubeSBCInstrHandler(operand);
  TubeAccumulator=TmpAcc; */
  unsigned char result=(TubeYReg-operand);
  SetPSRCZN(TubeYReg>=operand,TubeYReg==operand,result & 128); 
} /* CPYInstrHandler */

INLINE  void TubeDECInstrHandler(int16 address) {
  int val;

  val=TUBEREADMEM_FAST(address);

  val=(val-1);
  if (val<0) val=256+val;

  TUBEWRITEMEM_FAST(address,val);
  SetPSRZN(val);
} /* DECInstrHandler */

INLINE  void TubeDEXInstrHandler(void) {
  int val;
  val=(TubeXReg-1);
  if (val<0) val=256+val;
  TubeXReg=val;
  SetPSRZN(TubeXReg);
} /* DEXInstrHandler */

INLINE  void TubeDEAInstrHandler(void) {
  int val;
  val=(TubeAccumulator-1);
  if (val<0) val=256+val;
  TubeAccumulator=val;
  SetPSRZN(TubeAccumulator);
} /* DEAInstrHandler */

INLINE  void TubeEORInstrHandler(int16 operand) {
  TubeAccumulator^=operand;
  SetPSRZN(TubeAccumulator);
} /* EORInstrHandler */

INLINE  void TubeINCInstrHandler(int16 address) {
  unsigned char val;

  val=TUBEREADMEM_FAST(address);

  val=(val+1) & 255;

  TUBEWRITEMEM_FAST(address,val);
  SetPSRZN(val);
} /* INCInstrHandler */

INLINE  void TubeINXInstrHandler(void) {
  TubeXReg+=1;
  TubeXReg&=255;
  SetPSRZN(TubeXReg);
} /* INXInstrHandler */

INLINE  void TubeINAInstrHandler(void) {
  TubeAccumulator+=1;
  TubeAccumulator&=255;
  SetPSRZN(TubeAccumulator);
} /* INAInstrHandler */

INLINE  void TubeJSRInstrHandler(int16 address) {
  PushWord(TubeProgramCounter-1);
  TubeProgramCounter=address;
  //if (address==0xfff1) IDump=1;
} /* JSRInstrHandler */

INLINE  void TubeLDAInstrHandler(int16 operand) {
  TubeAccumulator=operand;
  SetPSRZN(TubeAccumulator);
} /* LDAInstrHandler */

INLINE  void TubeLDXInstrHandler(int16 operand) {
  TubeXReg=operand;
  SetPSRZN(TubeXReg);
} /* LDXInstrHandler */

INLINE  void TubeLDYInstrHandler(int16 operand) {
  TubeYReg=operand;
  SetPSRZN(TubeYReg);
} /* LDYInstrHandler */

INLINE  void TubeLSRInstrHandler(int16 address) {
  unsigned char oldVal,newVal;
  oldVal=TUBEREADMEM_FAST(address);
  newVal=(((unsigned int)oldVal)>>1)&255;
  TUBEWRITEMEM_FAST(address,newVal);
  SetPSRCZN((oldVal & 1)>0, newVal==0,0);
} /* LSRInstrHandler */

INLINE  void TubeLSRInstrHandler_Acc(void) {
  unsigned char oldVal,newVal;
  /* TubeAccumulator */
  oldVal=TubeAccumulator;
  TubeAccumulator=newVal=(((unsigned int)TubeAccumulator)>>1) & 255;
  SetPSRCZN((oldVal & 1)>0, newVal==0,0);
} /* LSRInstrHandler_Acc */

INLINE  void TubeORAInstrHandler(int16 operand) {
  TubeAccumulator=TubeAccumulator | operand;
  SetPSRZN(TubeAccumulator);
} /* ORAInstrHandler */

INLINE  void TubeROLInstrHandler(int16 address) {
  unsigned char oldVal,newVal;

  oldVal=TUBEREADMEM_FAST(address);
  newVal=((unsigned int)oldVal<<1) & 254;
  newVal+=GETCTFLAG;
  TUBEWRITEMEM_FAST(address,newVal);
  SetPSRCZN((oldVal & 128)>0,newVal==0,newVal & 128);
} /* ROLInstrHandler */

INLINE  void TubeROLInstrHandler_Acc(void) {
  unsigned char oldVal,newVal;

  oldVal=TubeAccumulator;
  newVal=((unsigned int)oldVal<<1) & 254;
  newVal+=GETCTFLAG;
  TubeAccumulator=newVal;
  SetPSRCZN((oldVal & 128)>0,newVal==0,newVal & 128);
} /* ROLInstrHandler_Acc */

INLINE  void TubeRORInstrHandler(int16 address) {
  unsigned char oldVal,newVal;

  oldVal=TUBEREADMEM_FAST(address);
  newVal=((unsigned int)oldVal>>1) & 127;
  newVal+=GETCTFLAG*128;
  TUBEWRITEMEM_FAST(address,newVal);
  SetPSRCZN(oldVal & 1,newVal==0,newVal & 128);
} /* RORInstrHandler */

INLINE  void TubeRORInstrHandler_Acc(void) {
  unsigned char oldVal,newVal;

  oldVal=TubeAccumulator;
  newVal=((unsigned int)oldVal>>1) & 127;
  newVal+=GETCTFLAG*128;
  TubeAccumulator=newVal;
  SetPSRCZN(oldVal & 1,newVal==0,newVal & 128);
} /* RORInstrHandler_Acc */

INLINE void TubeSBCInstrHandler(int16 operand) {
  /* NOTE! Not sure about C and V flags */
  int TmpResultV,TmpResultC;
  unsigned char nhn,nln;
  if (!GETDTFLAG) {
    TmpResultV=(signed char)TubeAccumulator-(signed char)operand-(1-GETCTFLAG);
    TmpResultC=TubeAccumulator-operand-(1-GETCTFLAG);
    TubeAccumulator=TmpResultC & 255;
    SetPSR(FlagC | FlagZ | FlagV | FlagN, TmpResultC>=0,TubeAccumulator==0,0,0,0,
      ((TubeAccumulator & 128)>0) ^ ((TmpResultV & 256)!=0),(TubeAccumulator & 128));
  } else {
    int ZFlag=0,NFlag=0,CFlag=1,VFlag=0;
    int TmpResult,TmpCarry=0;
    int ln,hn,oln,ohn;
	nhn=(TubeAccumulator>>4)&15; nln=TubeAccumulator & 15;

    /* Z flag determined from 2's compl result, not BCD result! */
    TmpResult=TubeAccumulator-operand-(1-GETCTFLAG);
    ZFlag=((TmpResult & 0xff)==0);

    ohn=operand & 0xf0; oln = operand & 0xf;
	if ((oln>9) && ((TubeAccumulator&15)<10)) { oln-=10; ohn+=0x10; } 
	// promote the lower nibble to the next ten, and increase the higher nibble
    ln=(TubeAccumulator & 0xf)-oln-(1-GETCTFLAG);
    if (ln<0) {
	  if ((TubeAccumulator & 15)<10) ln-=6;
      ln&=0xf;
      TmpCarry=0x10;
    }
    hn=(TubeAccumulator & 0xf0)-ohn-TmpCarry;
    /* N and V flags are determined before high nibble is adjusted.
       NOTE: V is not always correct */
    NFlag=hn & 128;
    TmpResultV=(signed char)TubeAccumulator-(signed char)operand-(1-GETCTFLAG);
	if ((TmpResultV<-128)||(TmpResultV>127)) VFlag=1; else VFlag=0;
    if (hn<0) {
      hn-=0x60;
      hn&=0xf0;
      CFlag=0;
    }
    TubeAccumulator=hn|ln;
    if (TubeAccumulator==0) ZFlag=1;
	NFlag=(hn &128);
	CFlag=(TmpResult&256)==0;
    SetPSR(FlagC | FlagZ | FlagV | FlagN,CFlag,ZFlag,0,0,0,VFlag,NFlag);
  }
} /* SBCInstrHandler */


INLINE  void TubeSTXInstrHandler(int16 address) {
  TUBEWRITEMEM_FAST(address,TubeXReg);
} /* STXInstrHandler */

INLINE  void TubeSTYInstrHandler(int16 address) {
  TUBEWRITEMEM_FAST(address,TubeYReg);
} /* STYInstrHandler */

INLINE  void BadInstrHandler(int opcode) {
	if (!IgnoreIllegalInstructions)
	{
#ifdef WIN32
		char errstr[250];
		sprintf(errstr,"Unsupported 65C02 instruction 0x%02X at 0x%04X\n"
			"  OK - instruction will be skipped\n"
			"  Cancel - dump memory and exit",opcode,TubeProgramCounter-1);
		if (MessageBox(GETHWND,errstr,"BBC Emulator",MB_OKCANCEL|MB_ICONERROR) == IDCANCEL)
		{
			exit(0);
		}
#else
		fprintf(stderr,"Bad instruction handler called:\n");
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
		TubeProgramCounter++;
		break;

	/* Three byte instructions */
	case 0xc:
	case 0xe:
	case 0xf:
		TubeProgramCounter+=2;
		break;
	}
} /* BadInstrHandler */

/*-------------------------------------------------------------------------*/
/* Absolute  addressing mode handler                                       */
INLINE  int16 AbsAddrModeHandler_Data(void) {
  int FullAddress;

  /* Get the address from after the instruction */
  
  GETTWOBYTEFROMTUBEPC(FullAddress)

  /* And then read it */
  return(TUBEREADMEM_FAST(FullAddress));
} /* AbsAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute  addressing mode handler                                       */
INLINE  int16 AbsAddrModeHandler_Address(void) {
  int FullAddress;

  /* Get the address from after the instruction */
  GETTWOBYTEFROMTUBEPC(FullAddress)

  /* And then read it */
  return(FullAddress);
} /* AbsAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page addressing mode handler                                       */
INLINE  int16 ZeroPgAddrModeHandler_Address(void) {
  return(TubeReadMem(TubeProgramCounter++));
} /* ZeroPgAddrModeHandler_Address */

/*-------------------------------------------------------------------------*/
/* Indexed with X preinc addressing mode handler                           */
INLINE  int16 IndXAddrModeHandler_Data(void) {
  unsigned char ZeroPageAddress;
  int EffectiveAddress;

  ZeroPageAddress=(TubeReadMem(TubeProgramCounter++)+TubeXReg) & 255;

  EffectiveAddress=TubeReadMem(ZeroPageAddress) | (TubeReadMem((ZeroPageAddress+1)&0xff)<<8);
  return(TUBEREADMEM_FAST(EffectiveAddress));
} /* IndXAddrModeHandler_Data */

/*-------------------------------------------------------------------------*/
/* Indexed with X preinc addressing mode handler                           */
INLINE  int16 IndXAddrModeHandler_Address(void) {
  unsigned char ZeroPageAddress;
  int EffectiveAddress;

  ZeroPageAddress=(TubeReadMem(TubeProgramCounter++)+TubeXReg) & 255;

  EffectiveAddress=TubeReadMem(ZeroPageAddress) | (TubeReadMem((ZeroPageAddress+1)&0xff)<<8);
  return(EffectiveAddress);
} /* IndXAddrModeHandler_Address */

/*-------------------------------------------------------------------------*/
/* Indexed with Y postinc addressing mode handler                          */
INLINE  int16 IndYAddrModeHandler_Data(void) {
  int EffectiveAddress;
  unsigned char ZPAddr;
  ZPAddr=TubeReadMem(TubeProgramCounter++);
  EffectiveAddress=TubeReadMem(ZPAddr)+TubeYReg;
  if (EffectiveAddress>0xff) TubeCarried=1;
  EffectiveAddress+=(TubeReadMem(ZPAddr+1)<<8);
  EffectiveAddress&=0xffff;

  return(TUBEREADMEM_FAST(EffectiveAddress));
} /* IndYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Indexed with Y postinc addressing mode handler                          */
INLINE  int16 IndYAddrModeHandler_Address(void) {
  int EffectiveAddress;
  unsigned char ZPAddr;
  ZPAddr=TubeReadMem(TubeProgramCounter++);
  EffectiveAddress=TubeReadMem(ZPAddr)+TubeYReg;
  if (EffectiveAddress>0xff) TubeCarried=1;
  EffectiveAddress+=(TubeReadMem(ZPAddr+1)<<8);
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* IndYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page wih X offset addressing mode handler                          */
INLINE  int16 ZeroPgXAddrModeHandler_Data(void) {
  int EffectiveAddress;
  EffectiveAddress=(TubeReadMem(TubeProgramCounter++)+TubeXReg) & 255;
  return(TubeReadMem(EffectiveAddress));
} /* ZeroPgXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page wih X offset addressing mode handler                          */
INLINE  int16 ZeroPgXAddrModeHandler_Address(void) {
  int EffectiveAddress;
  EffectiveAddress=(TubeReadMem(TubeProgramCounter++)+TubeXReg) & 255;
  return(EffectiveAddress);
} /* ZeroPgXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with X offset addressing mode handler                          */
INLINE  int16 AbsXAddrModeHandler_Data(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMTUBEPC(EffectiveAddress);
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+TubeXReg) & 0xff00)) TubeCarried=1;
  EffectiveAddress+=TubeXReg;
  EffectiveAddress&=0xffff;

  return(TUBEREADMEM_FAST(EffectiveAddress));
} /* AbsXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with X offset addressing mode handler                          */
INLINE  int16 AbsXAddrModeHandler_Address(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMTUBEPC(EffectiveAddress)
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+TubeXReg) & 0xff00)) TubeCarried=1;
  EffectiveAddress+=TubeXReg;
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* AbsXAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with Y offset addressing mode handler                          */
INLINE  int16 AbsYAddrModeHandler_Data(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMTUBEPC(EffectiveAddress);
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+TubeYReg) & 0xff00)) TubeCarried=1;
  EffectiveAddress+=TubeYReg;
  EffectiveAddress&=0xffff;

  return(TUBEREADMEM_FAST(EffectiveAddress));
} /* AbsYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Absolute with Y offset addressing mode handler                          */
INLINE  int16 AbsYAddrModeHandler_Address(void) {
  int EffectiveAddress;
  GETTWOBYTEFROMTUBEPC(EffectiveAddress)
  if ((EffectiveAddress & 0xff00)!=((EffectiveAddress+TubeYReg) & 0xff00)) TubeCarried=1;
  EffectiveAddress+=TubeYReg;
  EffectiveAddress&=0xffff;

  return(EffectiveAddress);
} /* AbsYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Indirect addressing mode handler                                        */
INLINE  int16 IndAddrModeHandler_Address(void) {
  /* For jump indirect only */
  int VectorLocation;
  int EffectiveAddress;

  GETTWOBYTEFROMTUBEPC(VectorLocation)

  /* Ok kiddies, deliberate bug time.
  According to my BBC Master Reference Manual Part 2
  the 6502 has a bug concerning this addressing mode and VectorLocation==xxFF
  so, we're going to emulate that bug -- Richard Gellman */
   EffectiveAddress=TUBEREADMEM_FAST(VectorLocation);
   EffectiveAddress|=TUBEREADMEM_FAST((VectorLocation+1)&0xffff) << 8; 
  return(EffectiveAddress);
} /* IndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page Indirect addressing mode handler                                        */
INLINE  int16 ZPIndAddrModeHandler_Address(void) {
  int VectorLocation;
  int EffectiveAddress;

  VectorLocation=TubeReadMem(TubeProgramCounter++);
  EffectiveAddress=TubeReadMem(VectorLocation)+(TubeReadMem(VectorLocation+1)<<8);

   // EffectiveAddress|=TUBEREADMEM_FAST(VectorLocation+1) << 8; }
  return(EffectiveAddress);
} /* ZPIndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page Indirect addressing mode handler                                        */
INLINE  int16 ZPIndAddrModeHandler_Data(void) {
  int VectorLocation;
  int EffectiveAddress;

  VectorLocation=TubeReadMem(TubeProgramCounter++);
  EffectiveAddress=TubeReadMem(VectorLocation)+(TubeReadMem(VectorLocation+1)<<8);

   // EffectiveAddress|=TUBEREADMEM_FAST(VectorLocation+1) << 8; }
  return(TubeReadMem(EffectiveAddress));
} /* ZPIndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Pre-indexed absolute Indirect addressing mode handler                                        */
INLINE  int16 IndAddrXModeHandler_Address(void) {
  /* For jump indirect only */
  int VectorLocation;
  int EffectiveAddress;

  GETTWOBYTEFROMTUBEPC(VectorLocation)
  EffectiveAddress=TUBEREADMEM_FAST((VectorLocation+TubeXReg)&0xffff);
  EffectiveAddress|=TUBEREADMEM_FAST((VectorLocation+1+TubeXReg)&0xffff) << 8; 
  EffectiveAddress&=0xffff;

   // EffectiveAddress|=TUBEREADMEM_FAST(VectorLocation+1) << 8; }
  return(EffectiveAddress);
} /* ZPIndAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page with Y offset addressing mode handler                         */
INLINE  int16 ZeroPgYAddrModeHandler_Data(void) {
  int EffectiveAddress;
  EffectiveAddress=(TubeReadMem(TubeProgramCounter++)+TubeYReg) & 255;
  return(TubeReadMem(EffectiveAddress));
} /* ZeroPgYAddrModeHandler */

/*-------------------------------------------------------------------------*/
/* Zero page with Y offset addressing mode handler                         */
INLINE  int16 ZeroPgYAddrModeHandler_Address(void) {
  int EffectiveAddress;
  EffectiveAddress=(TubeReadMem(TubeProgramCounter++)+TubeYReg) & 255;
  return(EffectiveAddress);
} /* ZeroPgYAddrModeHandler */


void TubeReset() {

  R1_HP.Reset(); R2_HP.Reset(); R3_HP.Reset(); R4_HP.Reset();
  R1_PH.Reset(); R2_PH.Reset(); R3_PH.Reset(); R4_PH.Reset();

  R1_HP.Limit=1;		R1_PH.Limit=24;
  R2_HP.Limit=1;		R2_PH.Limit=1;
  R3_HP.Limit=1;		R3_PH.Limit=1;
  R4_HP.Limit=1;		R4_PH.Limit=1;

  R1_HP.Type=0; R1_PH.Type=0; R2_HP.Type=0; R2_PH.Type=0;
  R3_HP.Type=1; R3_PH.Type=1; R4_HP.Type=0; R4_PH.Type=0;

  T=V=M=J=I=Q=K=P=0;

  //R3_PH.WriteData(0xff);

//  if (TubeLog==NULL) TubeLog=fopen("/ula.log","wb");
} /* Init6502core */

/*-------------------------------------------------------------------------*/
/* Initialise 6502core                                                     */
void Init65C02core(void) {
  FILE *TubeRom;
  char TRN[256];
  char *TubeRomName=TRN;
  //The fun part, the tube OS is copied from ROM to tube RAM before the processor starts processing
  //This makes the OS "ROM" writable in effect, but must be restored on each reset.
  //strcpy(TubeRomName,RomPath); strcat(TubeRomName,"/beebfile/m128/65c102.rom");
  //strcpy(TubeRomName,RomPath); strcat(TubeRomName,"/beebfile/65c102.rom");
  strcpy(TubeRomName,RomPath); strcat(TubeRomName,"beebfile/m128/newTube65c102.rom");
  memset(ROMSpace,0xff,65536);
  UseROM=1; // This goes to 0 when a tube read/write occurs
  TubeRom=fopen(TubeRomName,"rb");
  if (TubeRom!=NULL) {
	  fread(ROMSpace+0x800,1,2048,TubeRom);
//	  fread(ROMSpace+0x000,1,2048,TubeRom);
	  fclose(TubeRom);
  } else MessageBox(GETHWND,"Unable to load Tube ROM","BeebEm",MB_OK);
  TubeProgramCounter=TubeReadMem(0xfffc) | (TubeReadMem(0xfffd)<<8);
  TubeAccumulator=TubeXReg=TubeYReg=0x00; /* For consistancy of execution */
  TubeStackReg=0xff; /* Initial value ? */
  TubePSR=FlagI; /* Interrupts off for starters */

  TubeintStatus=0;
  TubeNMIStatus=0;
  TubeNMILock=0;
  TubeReset();
  route=0;
  IRQCycles=6;
//  TInstrLog=fopen("/tube.log","wb");
}

/*-------------------------------------------------------------------------*/
void DoTubeInterrupt(void) {
  PushWord(TubeProgramCounter);
  Push(TubePSR & ~FlagB); // IRQ in mid BRK becomes a BRK
  TubeProgramCounter=TubeReadMem(0xfffe) | (TubeReadMem(0xffff)<<8);
  SetPSR(FlagI,0,0,1,0,0,0,0);
  IRQCycles=7;
} /* DoInterrupt */

/*-------------------------------------------------------------------------*/
void DoTubeNMI(void) {
  /*cerr << "Doing NMI\n"; */
  TubeNMILock=1;
  NMI_Accumulator=TubeAccumulator;
  NMI_X=TubeXReg;
  NMI_Y=TubeYReg;
  NMI_P=TubePSR;
  NMI_SP=TubeStackReg; 
  PushWord(TubeProgramCounter);
  Push(TubePSR);
  TubeProgramCounter=TubeReadMem(0xfffa) | (TubeReadMem(0xfffb)<<8);
  SetPSR(FlagI,0,0,1,0,0,0,0); /* Normal interrupts should be disabled during NMI ? */
  IRQCycles=7;
} /* DoNMI */

/*-------------------------------------------------------------------------*/
/* Execute one 6502 instruction, move program counter on                   */
void Exec65C02Instruction(void) {
  static int TubeCurrentInstruction;
  static int tmpaddr;
  static int OldTubeNMIStatus;
  static int OldPC;
  int memptr;
  int tempPSR;
  unsigned char mdata;
  OldPC=TubeProgramCounter;
  //if (TubeProgramCounter==0xe00) BeginDump=1;
  TubeCurrentInstruction=TubeReadMem(TubeProgramCounter++);
  TubeCycles=TubeCyclesTable[TubeCurrentInstruction]; 
  TubeCarried=0; TubeBranched=0;
  switch (TubeCurrentInstruction) {
    case 0x00:
      TubeBRKInstrHandler();
      break;
    case 0x01:
      TubeORAInstrHandler(IndXAddrModeHandler_Data());
      break;
	case 0x04:
	  TubeTSBInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
    case 0x05:
      TubeORAInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0x06:
      TubeASLInstrHandler(ZeroPgAddrModeHandler_Address());
      break;
    case 0x08:
      Push(TubePSR); /* PHP */
      break;
    case 0x09:
      TubeORAInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0x0a:
      TubeASLInstrHandler_Acc();
      break;
	case 0x0c:
	  TubeTSBInstrHandler(AbsAddrModeHandler_Address());
	  break;
    case 0x0d:
      TubeORAInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0x0e:
      TubeASLInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0x10:
      TubeBPLInstrHandler();
      break;
    case 0x30:
      TubeBMIInstrHandler();
      break;
    case 0x50:
      TubeBVCInstrHandler();
      break;
    case 0x70:
      TubeBVSInstrHandler();
      break;
    case 0x80:
      TubeBRAInstrHandler();
      break;
    case 0x90:
      TubeBCCInstrHandler();
      break;
    case 0xb0:
      TubeBCSInstrHandler();
      break;
    case 0xd0:
      TubeBNEInstrHandler();
      break;
    case 0xf0:
      TubeBEQInstrHandler();
      break;
    case 0x11:
      TubeORAInstrHandler(IndYAddrModeHandler_Data());
      break;
    case 0x12:
      TubeORAInstrHandler(ZPIndAddrModeHandler_Data());
      break;
	case 0x14:
	  TubeTRBInstrHandler(ZeroPgAddrModeHandler_Address());
	  break;
    case 0x15:
      TubeORAInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0x16:
      TubeASLInstrHandler(ZeroPgXAddrModeHandler_Address());
      break;
    case 0x18:
      TubePSR&=255-FlagC; /* CLC */
      break;
    case 0x19:
      TubeORAInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0x1a:
      TubeINAInstrHandler();
      break;
	case 0x1c:
	  TubeTRBInstrHandler(AbsAddrModeHandler_Address());
	  break;
    case 0x1d:
      TubeORAInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0x1e:
      TubeASLInstrHandler(AbsXAddrModeHandler_Address());
      break;
    case 0x20:
      TubeJSRInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0x21:
      TubeANDInstrHandler(IndXAddrModeHandler_Data());
      break;
    case 0x24:
      TubeBITInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0x25:
      TubeANDInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0x26:
      TubeROLInstrHandler(ZeroPgAddrModeHandler_Address());
      break;
    case 0x28:
      TubePSR=Pop(); /* PLP */
      break;
    case 0x29:
      TubeANDInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0x2a:
      TubeROLInstrHandler_Acc();
      break;
    case 0x2c:
      TubeBITInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0x2d:
      TubeANDInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0x2e:
      TubeROLInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0x31:
      TubeANDInstrHandler(IndYAddrModeHandler_Data());
      break;
    case 0x32:
      TubeANDInstrHandler(ZPIndAddrModeHandler_Data());
      break;
    case 0x34: /* BIT Absolute,X */
      TubeBITInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0x35:
      TubeANDInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0x36:
      TubeROLInstrHandler(ZeroPgXAddrModeHandler_Address());
      break;
    case 0x38:
      TubePSR|=FlagC; /* SEC */
      break;
    case 0x39:
      TubeANDInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0x3a:
      TubeDEAInstrHandler();
      break;
    case 0x3c: /* BIT Absolute,X */
      TubeBITInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0x3d:
      TubeANDInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0x3e:
      TubeROLInstrHandler(AbsXAddrModeHandler_Address());
      break;
    case 0x40:
      TubePSR=Pop(); /* rti*/
      TubeProgramCounter=PopWord();
//      if (TubeNMILock) { 
		  TubeNMILock=0;
/*		  TubeAccumulator=NMI_Accumulator;
		  TubeXReg=NMI_X;
		  TubeYReg=NMI_Y;
		  TubePSR=NMI_P;
		  TubeStackReg=NMI_SP;  */
//	  } 
//	  if (GETBTFLAG) TubePSR&=~FlagB;
      break;
    case 0x41:
      TubeEORInstrHandler(IndXAddrModeHandler_Data());
      break;
    case 0x45:
      TubeEORInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0x46:
      TubeLSRInstrHandler(ZeroPgAddrModeHandler_Address());
      break;
    case 0x48:
      Push(TubeAccumulator); /* pha */
      break;
    case 0x49:
      TubeEORInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0x4a:
      TubeLSRInstrHandler_Acc();
      break;
    case 0x4c:
      TubeProgramCounter=AbsAddrModeHandler_Address(); /* JMP */
      break;
    case 0x4d:
      TubeEORInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0x4e:
      TubeLSRInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0x51:
      TubeEORInstrHandler(IndYAddrModeHandler_Data());
      break;
    case 0x52:
      TubeEORInstrHandler(ZPIndAddrModeHandler_Data());
      break;
    case 0x55:
      TubeEORInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0x56:
      TubeLSRInstrHandler(ZeroPgXAddrModeHandler_Address());
      break;
    case 0x58:
      TubePSR&=255-FlagI; /* CLI */
      break;
    case 0x59:
      TubeEORInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0x5a:
      Push(TubeYReg); /* PHY */
      break;
    case 0x5d:
      TubeEORInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0x5e:
      TubeLSRInstrHandler(AbsXAddrModeHandler_Address());
      break;
    case 0x60:
      TubeProgramCounter=PopWord()+1; /* RTS */
      break;
    case 0x61:
      TubeADCInstrHandler(IndXAddrModeHandler_Data());
      break;
    case 0x64:
      TUBEWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),0); /* STZ Zero Page */
      break;
    case 0x65:
      TubeADCInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0x66:
      TubeRORInstrHandler(ZeroPgAddrModeHandler_Address());
      break;
    case 0x68:
      TubeAccumulator=Pop(); /* PLA */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeAccumulator==0)<<1) | (TubeAccumulator & 128);
      break;
    case 0x69:
      TubeADCInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0x6a:
      TubeRORInstrHandler_Acc();
      break;
    case 0x6c:
      TubeProgramCounter=IndAddrModeHandler_Address(); /* JMP */
      break;
    case 0x6d:
      TubeADCInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0x6e:
      TubeRORInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0x71:
      TubeADCInstrHandler(IndYAddrModeHandler_Data());
      break;
    case 0x72:
      TubeADCInstrHandler(ZPIndAddrModeHandler_Data());
      break;
    case 0x74:
	  TUBEFASTWRITE(ZeroPgXAddrModeHandler_Address(),0); /* STZ Zpg,X */
      break;
    case 0x75:
      TubeADCInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0x76:
      TubeRORInstrHandler(ZeroPgXAddrModeHandler_Address());
      break;
    case 0x78:
      TubePSR|=FlagI; /* SEI */
      break;
    case 0x79:
      TubeADCInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0x7a:
		TubeYReg=Pop(); /* PLY */
		TubePSR&=~(FlagZ | FlagN);
		TubePSR|=((TubeYReg==0)<<1) | (TubeYReg & 128);
	  break;
    case 0x7c:
      TubeProgramCounter=IndAddrXModeHandler_Address(); /* JMP abs,X*/
      break;
    case 0x7d:
      TubeADCInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0x7e:
      TubeRORInstrHandler(AbsXAddrModeHandler_Address());
      break;
    case 0x81:
      TUBEFASTWRITE(IndXAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x84:
      TUBEWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),TubeYReg);
      break;
    case 0x85:
      TUBEWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x86:
      TUBEWRITEMEM_DIRECT(ZeroPgAddrModeHandler_Address(),TubeXReg);
      break;
    case 0x88:
      TubeYReg=(TubeYReg-1) & 255; /* DEY */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeYReg==0)<<1) | (TubeYReg & 128);
      break;
    case 0x89: /* BIT Immediate */
	  tempPSR=TubePSR&192;
      TubeBITInstrHandler(TubeReadMem(TubeProgramCounter++));
	  TubePSR=((TubePSR&63)|tempPSR);
      break;
    case 0x8a:
      TubeAccumulator=TubeXReg; /* TXA */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeAccumulator==0)<<1) | (TubeAccumulator & 128);
      break;
    case 0x8c:
      TubeSTYInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0x8d:
      TUBEFASTWRITE(AbsAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x8e:
      TubeSTXInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0x91:
      TUBEFASTWRITE(IndYAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x92:
      TUBEFASTWRITE(ZPIndAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x94:
      TubeSTYInstrHandler(ZeroPgXAddrModeHandler_Address());
      break;
    case 0x95:
      TUBEFASTWRITE(ZeroPgXAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x96:
      TubeSTXInstrHandler(ZeroPgYAddrModeHandler_Address());
      break;
    case 0x98:
      TubeAccumulator=TubeYReg; /* TYA */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeAccumulator==0)<<1) | (TubeAccumulator & 128);
      break;
    case 0x99:
      TUBEFASTWRITE(AbsYAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x9a:
      TubeStackReg=TubeXReg; /* TXS */
      break;
    case 0x9c:
      TUBEFASTWRITE(AbsAddrModeHandler_Address(),0); /* STZ Absolute */
	  /* here's a curiosity, STZ Absolute IS on the 6502 UNOFFICIALLY
	  and on the 65C12 OFFICIALLY. Something we should know? - Richard Gellman */
      break;
    case 0x9d:
      TUBEFASTWRITE(AbsXAddrModeHandler_Address(),TubeAccumulator); /* STA */
      break;
    case 0x9e:
		TUBEFASTWRITE(AbsXAddrModeHandler_Address(),0);  /* STZ Abs,X */ 
      break;
    case 0xa0:
      TubeLDYInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0xa1:
      TubeLDAInstrHandler(IndXAddrModeHandler_Data());
      break;
    case 0xa2:
      TubeLDXInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0xa4:
      TubeLDYInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0xa5:
      TubeLDAInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0xa6:
      TubeLDXInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0xa8:
      TubeYReg=TubeAccumulator; /* TAY */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeAccumulator==0)<<1) | (TubeAccumulator & 128);
      break;
    case 0xa9:
      TubeLDAInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0xaa:
      TubeXReg=TubeAccumulator; /* TAX */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeAccumulator==0)<<1) | (TubeAccumulator & 128);
      break;
    case 0xac:
      TubeLDYInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0xad:
      TubeLDAInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0xae:
      TubeLDXInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0xb1:
      TubeLDAInstrHandler(IndYAddrModeHandler_Data());
      break;
    case 0xb2:
      TubeLDAInstrHandler(ZPIndAddrModeHandler_Data());
      break;
    case 0xb4:
      TubeLDYInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0xb5:
      TubeLDAInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0xb6:
      TubeLDXInstrHandler(ZeroPgYAddrModeHandler_Data());
      break;
    case 0xb8:
      TubePSR&=255-FlagV; /* CLV */
      break;
    case 0xb9:
      TubeLDAInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0xba:
      TubeXReg=TubeStackReg; /* TSX */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeXReg==0)<<1) | (TubeXReg & 128);
      break;
    case 0xbc:
      TubeLDYInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0xbd:
      TubeLDAInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0xbe:
      TubeLDXInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0xc0:
      TubeCPYInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0xc1:
      TubeCMPInstrHandler(IndXAddrModeHandler_Data());
      break;
    case 0xc4:
      TubeCPYInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0xc5:
      TubeCMPInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0xc6:
      TubeDECInstrHandler(ZeroPgAddrModeHandler_Address());
      break;
    case 0xc8:
      TubeYReg+=1; /* INY */
      TubeYReg&=255;
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeYReg==0)<<1) | (TubeYReg & 128);
      break;
    case 0xc9:
      TubeCMPInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0xca:
      TubeDEXInstrHandler();
      break;
    case 0xcc:
      TubeCPYInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0xcd:
      TubeCMPInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0xce:
      TubeDECInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0xd1:
      TubeCMPInstrHandler(IndYAddrModeHandler_Data());
      break;
    case 0xd2:
      TubeCMPInstrHandler(ZPIndAddrModeHandler_Data());
      break;
    case 0xd5:
      TubeCMPInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0xd6:
      TubeDECInstrHandler(ZeroPgXAddrModeHandler_Address());
      break;
    case 0xd8:
      TubePSR&=255-FlagD; /* CLD */
      break;
    case 0xd9:
      TubeCMPInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0xda:
      Push(TubeXReg); /* PHX */
      break;
    case 0xdd:
      TubeCMPInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0xde:
      TubeDECInstrHandler(AbsXAddrModeHandler_Address());
      break;
    case 0xe0:
      TubeCPXInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0xe1:
      TubeSBCInstrHandler(IndXAddrModeHandler_Data());
      break;
    case 0xe4:
      TubeCPXInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0xe5:
      TubeSBCInstrHandler(TubeReadMem(TubeReadMem(TubeProgramCounter++))/*zp */);
      break;
    case 0xe6:
      TubeINCInstrHandler(ZeroPgAddrModeHandler_Address());
      break;
    case 0xe8:
      TubeINXInstrHandler();
      break;
    case 0xe9:
      TubeSBCInstrHandler(TubeReadMem(TubeProgramCounter++)); /* immediate */
      break;
    case 0xea:
      /* NOP */
      break;
    case 0xec:
      TubeCPXInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0xed:
      TubeSBCInstrHandler(AbsAddrModeHandler_Data());
      break;
    case 0xee:
      TubeINCInstrHandler(AbsAddrModeHandler_Address());
      break;
    case 0xf1:
      TubeSBCInstrHandler(IndYAddrModeHandler_Data());
      break;
    case 0xf2:
      TubeSBCInstrHandler(ZPIndAddrModeHandler_Data());
      break;
    case 0xf5:
      TubeSBCInstrHandler(ZeroPgXAddrModeHandler_Data());
      break;
    case 0xf6:
      TubeINCInstrHandler(ZeroPgXAddrModeHandler_Address());
      break;
    case 0xf8:
      TubePSR|=FlagD; /* SED */
      break;
    case 0xf9:
      TubeSBCInstrHandler(AbsYAddrModeHandler_Data());
      break;
    case 0xfa:
	  TubeXReg=Pop(); /* PLX */
      TubePSR&=~(FlagZ | FlagN);
      TubePSR|=((TubeXReg==0)<<1) | (TubeXReg & 128);
		break;
	case 0xfc:
		TubePSR|=FlagD;
		TubeProgramCounter+=2;
		break;
    case 0xfd:
      TubeSBCInstrHandler(AbsXAddrModeHandler_Data());
      break;
    case 0xfe:
      TubeINCInstrHandler(AbsXAddrModeHandler_Address());
      break;
	// 65C02 Bit instructions
	case 0x07:
	case 0x17:
	case 0x27:
	case 0x37:
	case 0x47:
	case 0x57:
	case 0x67:
	case 0x77:
		// RMBn zp
		memptr=TubeReadMem(TubeProgramCounter++);
		mdata=TubeReadMem(memptr);
		mdata&=~(1<<((TubeCurrentInstruction>>4)&7));
		TubeWriteMem(memptr,mdata);
		break;
	case 0x87:
	case 0x97:
	case 0xa7:
	case 0xb7:
	case 0xc7:
	case 0xd7:
	case 0xe7:
	case 0xf7:
		// SMBn zp
		memptr=TubeReadMem(TubeProgramCounter++);
		mdata=TubeReadMem(memptr);
		mdata|=1<<((TubeCurrentInstruction>>4)&7);
		TubeWriteMem(memptr,mdata);
		break; 
	case 0x0f:
	case 0x1f:
	case 0x2f:
	case 0x3f:
	case 0x4f:
	case 0x5f:
	case 0x6f:
	case 0x7f:
		// BBRn zp,rel
		memptr=TubeReadMem(TubeProgramCounter++);
		mdata=TubeReadMem(memptr);
		if (mdata & (1<<((TubeCurrentInstruction>>4)&7))) {
			TubeProgramCounter++;
		} else {
			TubeProgramCounter=RelAddrModeHandler_Data();
			TubeBranched=1;
		}
		break;
	case 0x8f:
	case 0x9f:
	case 0xaf:
	case 0xbf:
	case 0xcf:
	case 0xdf:
	case 0xef:
	case 0xff:
		// BBSn zp.rel
		memptr=TubeReadMem(TubeProgramCounter++);
		mdata=TubeReadMem(memptr);
		if (mdata & (1<<((TubeCurrentInstruction>>4)&7))) {
			TubeProgramCounter=RelAddrModeHandler_Data();
			TubeBranched=1;
		} else {
			TubeProgramCounter++;
		}
		break; 
    default:
      BadInstrHandler(TubeCurrentInstruction);
      break;
	break;

  }; /* OpCode switch */
	// This block corrects the cycle count for the branch instructions
	if ((TubeCurrentInstruction==0x10) ||
		(TubeCurrentInstruction==0x30) ||
		(TubeCurrentInstruction==0x50) ||
		(TubeCurrentInstruction==0x70) ||
		(TubeCurrentInstruction==0x80) ||
		(TubeCurrentInstruction==0x90) ||
		(TubeCurrentInstruction==0xb0) ||
		(TubeCurrentInstruction==0xd0) ||
		(TubeCurrentInstruction==0xf0)) {
			if (((TubeProgramCounter & 0xff00)!=(OldPC & 0xff00)) && (TubeBranched==1)) 
				TubeCycles+=2; else TubeCycles++;
	}
	if (((TubeCurrentInstruction & 0xf)==1) ||
		((TubeCurrentInstruction & 0xf)==9) ||
		((TubeCurrentInstruction & 0xf)==0xD)) {
		if (((TubeCurrentInstruction &0x10)==0) &&
			((TubeCurrentInstruction &0xf0)!=0x90) &&
			(TubeCarried==1)) TubeCycles++;
	}
	if (((TubeCurrentInstruction==0xBC) || (TubeCurrentInstruction==0xBE)) && (TubeCarried==1)) TubeCycles++;
	// End of cycle correction
	TubeProgramCounter&=0xffff;
  TotalTubeCycles+=TubeCycles+IRQCycles;
  IRQCycles=0;
  OldPC=TubeProgramCounter;
} /* Exec6502Instruction */

void SyncTubeProcessor (void) {
	// This proc syncronises the two processors on a cycle based timing.
	// i.e. if parasitecycles<hostcycles then execute parasite instructions until
	// parasitecycles>=hostcycles.
	if (!P) {
		 while (TotalTubeCycles<(__int64)((double)TotalCycles*1.5)) {	
			Exec65C02Instruction();
			if ((TubeNMIStatus) && (!TubeNMILock)) DoTubeNMI();
		    if ((TubeintStatus>0) && (!GETITFLAG) && (!TubeNMILock)) DoTubeInterrupt();
		 }
	}
}

void DumpTubeLanguageRom(void) {
	FILE *tlrom;
	tlrom=fopen("/TubeLang.rom","wb");
	if (tlrom!=NULL) fwrite(TubeRam+0x8000,0x4000,1,tlrom);
	fclose(tlrom);
}
