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
/* User VIA support file for the beeb emulator - David Alan Gilbert 11/12/94 */
/* Modified from the system via */

#ifndef USERVIA_HEADER
#define USERVIA_HEADER

#include "viastate.h"
#include "via.h"

extern VIAState UserVIAState;
extern unsigned char WTDelay1,WTDelay2;

void UserVIAWrite(int Address, int Value);
int UserVIARead(int Address);
void UserVIAReset(void);

void UserVIA_poll_real(void);

#define UserVIA_poll(ncycles) \
  UserVIAState.timer1c-=(ncycles-WTDelay1); \
  UserVIAState.timer2c-=(ncycles-WTDelay2); \
  WTDelay1=WTDelay2=0; \
  if ((UserVIAState.timer1c<0) || (UserVIAState.timer2c<0)) UserVIA_poll_real(); \
  if (AMXMouseEnabled && AMXTrigger<=TotalCycles) AMXMouseMovement(); \
  if (PrinterEnabled && PrinterTrigger<=TotalCycles) PrinterPoll();

void SaveUserVIAState(unsigned char *StateData);
void RestoreUserVIAState(unsigned char *StateData);

void uservia_dumpstate(void);

/* AMX mouse enabled */
extern int AMXMouseEnabled;

/* Map Left+Right button presses to a middle button press */
extern int AMXLRForMiddle;

/* Number of cycles between each mouse interrupt */
#define AMX_TRIGGER 1000
extern __int64 AMXTrigger;

/* Checks if a movement interrupt should be generated */
void AMXMouseMovement();

/* Target and current positions for mouse */
extern int AMXTargetX;
extern int AMXTargetY;
extern int AMXCurrentX;
extern int AMXCurrentY;

/* Button states */
#define AMX_LEFT_BUTTON 1
#define AMX_MIDDLE_BUTTON 2
#define AMX_RIGHT_BUTTON 4
extern int AMXButtons;

/* Printer enabled */
extern int PrinterEnabled;
void PrinterEnable(char *FileName);
void PrinterDisable();

/* Trigger for checking for printer ready. */
#define PRINTER_TRIGGER 25
extern __int64 PrinterTrigger;
void PrinterPoll();

#endif
