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

// SCSI module for the Domesday Version of BeebEm

#include <stdio.h>
#include <windows.h>
#include "6502core.h"
#include "beebmem.h"
#include "beebwin.h"
#include "main.h"
#include "lvrom.h"
#include "scsi.h"
#include "debugLogs.h"

typedef struct {
	unsigned char DataOut,DataIn;
	unsigned char Status;
	int IRQ_Enable;
	int Phase;
	unsigned char Buffer[256];
	int Pointer;
	int Limit;
	char GroupCode,CommandCode;
	long LBA;
	int LUN;
	long Sectors;
	int Command;
} SCSI_Type;

#define BUS_FREE 1
#define COMMAND 2
#define DATA_IN 3
#define DATA_OUT 4
#define STATUS 5
#define MESSAGE 6

#define READ_DATA 128
#define FCODE_IN 129
#define FCODE_OUT 130

#define SCSI_ID 0

// #define SCSI_DEBUG
#define COMMAND_DEBUG

typedef enum {
	SMSG,
	BSY,
	SEL,
	ACK,
	IRQ,
	REQ,
	IO,
	CD,
} SCSI_Stat;

SCSI_Type SCSI;

#ifdef USE_SCSI_LOG
FILE *scsilog = NULL;
#endif
#ifdef USE_SCSICMD_LOG
FILE *scsicmdlog = NULL;
#endif

#define SET_SCSI(bit) SCSI.Status|=(1<<bit)
#define RESET_SCSI(bit) SCSI.Status&=~(1<<bit)

/* Address selects are as follows:
0 Read Data (Read Only)
0 Write Data (Write Only)
1 Read Status
2 Write Select
3 Write IRQ Enable
*/

void REQ_ACK(char bit)
{
  if ((bit==REQ) && (!(SCSI.Status & (1<<REQ))))
  { 

#ifdef USE_SCSI_LOG
    fprintf(scsilog, "Requesting byte\n");
    fflush(scsilog);
#endif
    SET_SCSI(REQ); 
    RESET_SCSI(ACK); 
    if (SCSI.IRQ_Enable)
    {
      intStatus |= 1<<scsi;
      SET_SCSI(IRQ);
    }
  }
  if ((bit==ACK)  && (!(SCSI.Status & (1<<ACK))))
  {
    SET_SCSI(ACK);
    RESET_SCSI(REQ); 
#ifdef USE_SCSI_LOG
    fprintf(scsilog, "Received byte: Out %02X In %02X\n",
    			SCSI.DataOut, SCSI.DataIn);
    fflush(scsilog);
#endif
  }
}

void SEL_BSY(char bit)
{
  if (bit==SEL)
  {
    SET_SCSI(SEL);
    RESET_SCSI(BSY); 
#ifdef USE_SCSI_LOG
    fprintf(scsilog, "SEL enabled\n");
    fflush(scsilog);
#endif
  }
  if (bit==BSY)
  {
    SET_SCSI(BSY);
    RESET_SCSI(SEL); 
#ifdef USE_SCSI_LOG
    fprintf(scsilog, "BSY asserted\n");
    fflush(scsilog);
#endif
  }
}

void SCSI_Write(unsigned char addr, unsigned char val)
{
#ifdef USE_SCSI_LOG
  fprintf(scsilog, "SCSI Write of %d to %d\n",val,addr);
  fflush(scsilog);
#endif
  if (addr==0)
  {
    // Write Data
    SCSI.DataOut=val ^ 255;
    REQ_ACK(ACK);
  }
  if (addr==2)
  {
    // Write Select
    SEL_BSY(SEL);
  }
  if (addr==3)
  {
    // Write IRQ Enable
    SCSI.IRQ_Enable=val & 1;
    if ((val &1 )==0)
    {
      intStatus&=~(1<<scsi);
      RESET_SCSI(IRQ);
    }
  } 
}

unsigned char SCSI_Read(unsigned char addr)
{
#ifdef USE_SCSI_LOG
  fprintf(scsilog, "SCSI Read of %d\n",addr);
  fflush(scsilog);
#endif
  if (addr==0)
  {
    REQ_ACK(ACK);
    return(SCSI.DataIn);
  }
  if (addr==1)
  {
    return(SCSI.Status  & 243);
  }
  return 0;
}

void SCSI_Reset(void)
{
  SCSI.Status=0;
  SCSI.DataIn=0;
  SCSI.DataOut=0;
  SCSI.IRQ_Enable=0;
  SCSI.Phase=BUS_FREE;
#ifdef USE_SCSI_LOG
  openLog(scsilog, SCSI_LOG_FN);
#endif
#ifdef USE_SCSICMD_LOG
  openLog(scsicmdlog, SCSICMD_LOG_FN);
#endif
}

void PrepareNextReadBlock(void)
{
  // Prepare next block for shunting out the SCSI BUS.
  FILE *F_Read;
  F_Read=fopen(lvDiscLdiFile[currLvDisc], "rb");
  if (F_Read)
  {
    fseek(F_Read,SCSI.LBA*256,SEEK_SET);
    fread(SCSI.Buffer,1,256,F_Read);
    fclose(F_Read);
  }
  else
  {
    memset(SCSI.Buffer, 0, 256);
  }
  SCSI.Limit=256;
  SCSI.Pointer=0;
  REQ_ACK(REQ);
  SCSI.DataIn=SCSI.Buffer[0];
}

void DoEndCommand(void);

void DoSCSICommand(void)
{ 
	// SCSI Command interpreter
	if (SCSI.GroupCode==0) {
		// Group 0 commands
		SCSI.LUN=(SCSI.Buffer[1] & 224) >> 5;
		SCSI.LBA=SCSI.Buffer[3];
		SCSI.LBA+=SCSI.Buffer[2] *256;
		SCSI.LBA+=(SCSI.Buffer[1] & 31) *65536;
		SCSI.Sectors=SCSI.Buffer[4];
		if (SCSI.CommandCode==0x08) {
			// Read Sector
			SCSI.Phase=DATA_IN;
			RESET_SCSI(CD);
			SET_SCSI(IO);
			RESET_SCSI(SMSG);
			SCSI.Sectors--;
			PrepareNextReadBlock();
			SCSI.Command=READ_DATA;
		}
		if (SCSI.CommandCode==0x1b) {
			MixMode=MIXMODE_BBC;
			DoEndCommand();
		}
	}
	if (SCSI.GroupCode==6) {
		if (SCSI.CommandCode==0x0a) {
			SCSI.LUN=(SCSI.Buffer[1] & 224) >> 5;
			SCSI.Phase=DATA_OUT;
			RESET_SCSI(CD);
			RESET_SCSI(IO);
			RESET_SCSI(SMSG);
			SCSI.Command=FCODE_IN;
			REQ_ACK(REQ);
			SCSI.Pointer=0;
			SCSI.Limit=256;
		}
		if (SCSI.CommandCode==0x08) {
			SCSI.LUN=(SCSI.Buffer[1] & 224) >> 5;
			SCSI.Phase=DATA_IN;
			RESET_SCSI(CD);
			SET_SCSI(IO);
			RESET_SCSI(SMSG);
			SCSI.Command=FCODE_OUT;
			REQ_ACK(REQ);
			SCSI.Pointer=0;
			SCSI.Limit=256;
			memcpy(SCSI.Buffer,FCode_Reply_Buf,256);
			AckLVReply();
			SCSI.DataIn=SCSI.Buffer[0];
			REQ_ACK(REQ);
		}

	}

#ifdef USE_SCSICMD_LOG
  fprintf(scsicmdlog, "Group %02X Command %02X\n",
  			SCSI.GroupCode, SCSI.CommandCode);
  fprintf(scsicmdlog, "LBA: &%08X\n", SCSI.LBA);
  fprintf(scsicmdlog, "Length: &%02X\n", SCSI.Sectors+1);
  fflush(scsicmdlog);
#endif

}

void DoEndCommand(void) {
	if (SCSI.GroupCode==0) {
		if ((SCSI.CommandCode==0x08) ||
			(SCSI.CommandCode==0x1b)) {
			// Finish off read sector command
			SCSI.Phase=STATUS;
			SET_SCSI(CD);
			SET_SCSI(IO);
			RESET_SCSI(SMSG);
			SCSI.Buffer[0]=0;
			SCSI.Pointer=0;
			SCSI.Limit=1;
			REQ_ACK(REQ);
		}
	}
	if (SCSI.GroupCode==6) {
			SCSI.Phase=STATUS;
			SET_SCSI(CD);
			SET_SCSI(IO);
			RESET_SCSI(SMSG);
			SCSI.Buffer[0]=0;
			SCSI.Pointer=0;
			SCSI.Limit=1;
			REQ_ACK(REQ);
	}
}

void PrepareMessageBlock(void) {
	SCSI.Pointer=1;
	SCSI.Limit=1;
	SCSI.Buffer[0]=0;
	SCSI.DataIn=0; // COMMAND COMPLETE
	REQ_ACK(REQ);
}

void SCSI_Poll(void) {
	int RequestMore;
	if (!(SCSI.Status & (1<<IO))) SCSI.DataIn=SCSI.DataOut;
	if (SCSI.Phase==BUS_FREE) {
		if ((SCSI.Status & (1<<SEL)) && (SCSI.DataOut==1<<SCSI_ID)) {
			SEL_BSY(BSY);
			SCSI.Phase=COMMAND;
			SET_SCSI(CD); RESET_SCSI(IO); RESET_SCSI(SMSG);
			REQ_ACK(REQ);
			SCSI.Pointer=0;
			SCSI.Limit=256;
		}
	}
	if (SCSI.Phase==COMMAND) {
		RequestMore=1;
		if ((SCSI.Status & (1<<ACK)) && (SCSI.Pointer<SCSI.Limit)) {
			SCSI.Buffer[SCSI.Pointer++]=SCSI.DataOut;
			if (SCSI.Pointer==1) {
				// Detect SCSI command type
				SCSI.GroupCode=(SCSI.Buffer[0] & 224) >> 5;
				SCSI.CommandCode=SCSI.Buffer[0] & 31;
				if (SCSI.GroupCode==0) SCSI.Limit=6;
				if (SCSI.GroupCode==1) SCSI.Limit=10;
				if (SCSI.GroupCode==5) SCSI.Limit=12;
				if (SCSI.GroupCode==6) SCSI.Limit=6;
			}
			if (SCSI.Pointer>=SCSI.Limit) {
				// Command buffer full
				RequestMore=0;
				DoSCSICommand();
			}
		if (RequestMore) REQ_ACK(REQ);
		}
	}
	if (SCSI.Phase==DATA_OUT) {
		if ((SCSI.Status & (1<<ACK)) && (SCSI.Pointer<SCSI.Limit)) {
			SCSI.Buffer[SCSI.Pointer++]=SCSI.DataOut;
			if (SCSI.Pointer>=SCSI.Limit) {
				for (int n=0;n<256;n++) {
					if (SCSI.Buffer[n]==13) SCSI.Buffer[n]=0;
				}
				memcpy(FCode_Buf,SCSI.Buffer,256);
				Do_FCode_Command();
				DoEndCommand();
			} else {
				REQ_ACK(REQ);
			}
		}
	}
	if (SCSI.Phase==DATA_IN) {
		if ((SCSI.Status & (1<<ACK)) && (SCSI.Pointer<SCSI.Limit)) {
			SCSI.DataIn=SCSI.Buffer[++SCSI.Pointer];
			if (SCSI.Pointer<SCSI.Limit) REQ_ACK(REQ);
			if (SCSI.Pointer>=SCSI.Limit) {
				if (SCSI.Command==READ_DATA) {
#ifdef USE_SCSI_LOG
  fprintf(scsilog, "SCSI Block transferred\n");
  fflush(scsilog);
#endif
					if (SCSI.Sectors>0) {
						SCSI.Sectors--;
						SCSI.LBA++;
						PrepareNextReadBlock(); 
					} else {
						DoEndCommand();
					}
				}
				if (SCSI.Command==FCODE_OUT) {
					DoEndCommand();
				}
			}
		}
	}
	if (SCSI.Phase==STATUS) {
		if (SCSI.Status & (1<<ACK)) {
			// Status byte taken, Move to message phase
			SCSI.Phase=BUS_FREE;
			SET_SCSI(CD);
			SET_SCSI(IO);
			SET_SCSI(SMSG);
			SCSI.Phase=MESSAGE;
			PrepareMessageBlock();
		}
	}
	if (SCSI.Phase==MESSAGE) {
		if ((SCSI.Status & (1<<ACK)) && (SCSI.Pointer<SCSI.Limit)) {
			SCSI.DataIn=SCSI.Buffer[++SCSI.Pointer];
			if (SCSI.Pointer<SCSI.Limit) REQ_ACK(REQ);
		}
		if ((SCSI.Status & (1<<ACK)) && (SCSI.Pointer>=SCSI.Limit)) {
			RESET_SCSI(CD);
			RESET_SCSI(IO);
			RESET_SCSI(SMSG);
			RESET_SCSI(BSY);
			// Bus Released
			SCSI.Phase=BUS_FREE;
		}
	}
}
