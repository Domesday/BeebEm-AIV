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

/*
Serial/Cassette Support for BeebEm

Written by Richard Gellman - March 2001

You may not distribute this entire file separate from the whole BeebEm distribution.

You may use sections or all of this code for your own uses, provided that:

1) It is not a separate file in itself.
2) This copyright message is included
3) Acknowledgement is made to the author, and any aubsequent authors of additional code.

The code may be modified as required, and freely distributed as such authors see fit,
provided that:

1) Acknowledgement is made to the author, and any subsequent authors of additional code
2) Indication is made of modification.

Absolutely no warranties/guarantees are made by using this code. You use and/or run this code
at your own risk. The code is classed by the author as "unlikely to cause problems as far as
can be determined under normal use".

-- end of legal crap - Richard Gellman :) */

// P.S. If anybody knows how to emulate this, do tell me - 16/03/2001 - Richard Gellman

#include "serial.h"
#include "6502core.h"
#include "uef.h"
#include "main.h"
#include "beebsound.h"
#include "beebwin.h"
#include "lvrom.h"
#include <stdio.h>

#define CASSETTE 0  // Device in 
#define RS423 1		// use defines

unsigned char Cass_Relay=0; // Cassette Relay state
unsigned char SerialChannel=CASSETTE; // Device in use

unsigned char RDRE=0,TDRF=0; // RX/TX Data Reg in use specifiers
unsigned char RDR,TDR; // Receive and Transmit Data Registers
unsigned char RDSR,TDSR; // Receive and Transmit Data Shift Registers (buffers)
unsigned int Tx_Rate,Rx_Rate; // Recieve and Transmit baud rates.
unsigned char Clk_Divide; // Clock divide rate

unsigned char ACIA_Status,ACIA_Control; // 6850 ACIA Status.& Control
unsigned char SP_Control; // SERPROC Control;

unsigned char CTS,RTS,FirstReset=1;
unsigned char DCD=0,DCDI=1,ODCDI=1,DCDClear=0; // count to clear DCD bit

unsigned char Parity,Stop_Bits,Data_Bits,RIE,TIE; // Receive Intterrupt Enable
												  // and Transmit Interrupt Enable
unsigned char TxD,RxD; // Transmit and Receive destinations (data or shift register)

UEFFILE TapeFile; // UEF Tape file handle for Thomas Harte's UEF-DLL

unsigned int Baud_Rates[8]={19200,1200,4800,150,9600,300,2400,75};

unsigned char INTONE=0; // Dummy byte bug
unsigned char OldRelayState=0;

#define DCDLOWLIMIT 20
#define DCDCOUNTDOWN 200
unsigned int DCDClock=DCDCOUNTDOWN;
unsigned char UEFOpen=0;
unsigned int TapeCount=100;

int UEF_BUF=0,NEW_UEF_BUF=0;
int TapeClock=0,OldClock=0;

int TapeClockSpeed;

FILE *serlog;

void SetACIAStatus(unsigned char bit) {
	ACIA_Status|=1<<bit;
}

void ResetACIAStatus(unsigned char bit) {
	ACIA_Status&=~(1<<bit);
}

void Write_ACIA_Control(unsigned char CReg) {
	unsigned char bit;
	ACIA_Control=CReg; // This is done for safe keeping
	// Master reset
	if ((CReg & 3)==3) {
		ACIA_Status&=8; ResetACIAStatus(7); SetACIAStatus(2);
		intStatus&=~(1<<serial); // Master reset clears IRQ
		if (FirstReset==1) { 
			CTS=1; SetACIAStatus(3);
			FirstReset=0; RTS=1; 
		} // RTS High on first Master reset.
		if (DCDI==0) ResetACIAStatus(2); // clear DCD Bit if DCD input is low
		ResetACIAStatus(2); DCDI=0; DCDClear=0;
		SetACIAStatus(1); // Xmit data register empty
	}
	// Clock Divide
	if ((CReg & 3)==0) Clk_Divide=1;
	if ((CReg & 3)==1) Clk_Divide=16;
	if ((CReg & 3)==2) Clk_Divide=64;
	// Parity, Data, and Stop Bits.
	Parity=2-((CReg & 4)>>2);
	Stop_Bits=2-((CReg & 8)>>3);
	Data_Bits=7+((CReg & 16)>>4);
	if ((CReg & 28)==16) { Stop_Bits=2; Parity=0; }
	if ((CReg & 28)==20) { Stop_Bits=1; Parity=0; }
	// Transmission control
	TIE=(CReg & 32)>>5;
	RTS=(CReg & 64)>>6;
	RIE=(CReg & 128)>>7;
	bit=(CReg & 96)>>5;
	if (bit==3) { RTS=0; TIE=0; }
	// Change serial port settings
	if (SerialChannel==RS423) {
		// The code that was here is no longer needed, however the RTS line is emulated as a
		// "F-Code transmit enable", specifically, it is an interlock that stops a serial F-code
		// being interpreted in the middle of a SCSI F-Code
	}
}

void Write_ACIA_Tx_Data(unsigned char Data) {
	intStatus&=~(1<<serial);
	ResetACIAStatus(7);
	if (SerialChannel=RS423) {
//		if (ACIA_Status & 2) {
			LVROM_PutSerial(Data);
//			SetACIAStatus(1);
//		}
	}
}

void Write_SERPROC(unsigned char Data) {
	unsigned int HigherRate;
	SP_Control=Data;
	// Slightly easier this time.
	// just the Rx and Tx baud rates, and the selectors.
	Cass_Relay=(Data & 128)>>7;
	TapeAudio.Enabled=(Cass_Relay)?TRUE:FALSE;
	LEDs.Motor=(Cass_Relay==1);
	if (Cass_Relay!=OldRelayState) {
		OldRelayState=Cass_Relay;
		ClickRelay(Cass_Relay);
	}
	SerialChannel=(Data & 64)>>6;
	Tx_Rate=Baud_Rates[(Data & 7)];
	Rx_Rate=Baud_Rates[(Data & 56)>>3];
	// Note, the PC serial port (or at least win32) does not allow different transmit/receive rates
	// So we will use the higher of the two
	if (SerialChannel==RS423) {
		HigherRate=Tx_Rate;
		if (Rx_Rate>Tx_Rate) HigherRate=Rx_Rate;
		// The code that was here has no meaning, as the data going in and out of 
		// the serial port is emulated, and is therefore not electronically dependent
		// on a clock signal
	}
}

unsigned char Read_ACIA_Status(void) {
	DCDClear=2;
	return(ACIA_Status);
}

void HandleData(unsigned char AData) {
	//fprintf(serlog,"%d %02X\n",RxD,AData);
	// This proc has to dump data into the serial chip's registers
	if (RxD==0) { RDR=AData; SetACIAStatus(0); } // Rx Reg full
	if (RxD==1) { RDSR=AData; SetACIAStatus(0); }
	ResetACIAStatus(5);
	if (RxD==2) { RDR=RDSR; RDSR=AData; SetACIAStatus(5); } // overrun
	if (RIE) { intStatus|=1<<serial; SetACIAStatus(7); } // interrupt on receive/overun
	if (RxD<2) RxD++; 	
}


unsigned char Read_ACIA_Rx_Data(void) {
	unsigned char TData;
	intStatus&=~(1<<serial);
	ResetACIAStatus(7);
	if (DCDI==0) { DCDClear=2; }
	TData=RDR; RDR=RDSR; RDSR=0;
	if (RxD>0) RxD--; 
	if (RxD==0) ResetACIAStatus(0);
	if ((RxD>0) && (RIE)) { intStatus|=1<<serial; SetACIAStatus(7); }
	if (Data_Bits==7) TData&=127;
	return(TData);
}

unsigned char Read_SERPROC(void) {
return(SP_Control);
}

void Serial_Poll(void) {
	if (SerialChannel==RS423) {
		SetACIAStatus(3);
	} 
}


void Kill_Serial(void) {
}

void LoadUEF(char *UEFName) {
}

void RewindTape(void) {
}
