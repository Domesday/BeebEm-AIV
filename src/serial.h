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

// Serial/Cassette Support for BeebEm
// Written by Richard Gellman

#ifndef SERIAL_HEADER
#define SERIAL_HEADER
void Write_ACIA_Control(unsigned char CReg);
void Write_ACIA_Tx_Data(unsigned char Data);
void Write_SERPROC(unsigned char Data);
unsigned char Read_ACIA_Status(void);
unsigned char Read_ACIA_Rx_Data(void);
unsigned char Read_SERPROC(void);
void Serial_Poll(void);
void Kill_Serial(void);
void LoadUEF(char *UEFName);
void RewindTape(void);
extern int TapeClockSpeed;
#endif