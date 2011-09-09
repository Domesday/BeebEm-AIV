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

#include <iostream.h>
#include <stdio.h>

#include "6502core.h"
#include "uservia.h"
#include "via.h"
#include "viastate.h"

#ifdef WIN32
#include <windows.h>
#include "main.h"
#endif

/* AMX mouse (see uservia.h) */
int AMXMouseEnabled = 0;
int AMXLRForMiddle = 0;
__int64 AMXTrigger = 0;
int AMXButtons = 0;
int AMXTargetX = 0;
int AMXTargetY = 0;
int AMXCurrentX = 0;
int AMXCurrentY = 0;

/* Printer port */
int PrinterEnabled = 0;
__int64 PrinterTrigger = 0;
static char PrinterFileName[256];
static FILE *PrinterFileHandle = NULL;

extern int DumpAfterEach;
/* My raw VIA state */
VIAState UserVIAState;

unsigned char WTDelay1,WTDelay2; // Timer write delay - one cycle

/*--------------------------------------------------------------------------*/
static void UpdateIFRTopBit(void) {
  /* Update top bit of IFR */
  if (UserVIAState.ifr&(UserVIAState.ier&0x7f))
    UserVIAState.ifr|=0x80;
  else
    UserVIAState.ifr&=0x7f;
  intStatus&=~(1<<userVia);
  intStatus|=((UserVIAState.ifr & 128)?(1<<userVia):0);
}; /* UpdateIFRTopBit */

/*--------------------------------------------------------------------------*/
/* Address is in the range 0-f - with the fe60 stripped out */
void UserVIAWrite(int Address, int Value) {
  /* cerr << "UserVIAWrite: Address=0x" << hex << Address << " Value=0x" << Value << dec << " at " << TotalCycles << "\n";
  DumpRegs(); */

  switch (Address) {
    case 0:
      UserVIAState.orb=Value & 0xff;
      if ((UserVIAState.ifr & 1) && ((UserVIAState.pcr & 2)==0)) {
        UserVIAState.ifr&=0xfe;
        UpdateIFRTopBit();
      };
      break;

    case 1:
      UserVIAState.ora=Value & 0xff;
      UserVIAState.ifr&=0xfc;
      UpdateIFRTopBit();
      if (PrinterEnabled) {
        if (fputc(UserVIAState.ora, PrinterFileHandle) == EOF) {
#ifdef WIN32
          char errstr[200];
          sprintf(errstr, "Failed to write to printer file:\n  %s", PrinterFileName);
          MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
#else
          cerr << "Failed to write to printer file " << PrinterFileName << "\n";
#endif
        }
        else {
          fflush(PrinterFileHandle);
          SetTrigger(PRINTER_TRIGGER, PrinterTrigger);
        }
      }
      break;

    case 2:
      UserVIAState.ddrb=Value & 0xff;
      break;

    case 3:
      UserVIAState.ddra=Value & 0xff;
      break;

    case 4:
    case 6:
      /*cerr << "UserVia Reg4/6 Timer1 lo Counter Write val=0x " << hex << Value << dec << " at " << TotalCycles << "\n"; */
      UserVIAState.timer1l&=0xff00;
      UserVIAState.timer1l|=(Value & 0xff);
      break;

    case 5:
      /*cerr << "UserVia Reg5 Timer1 hi Counter Write val=0x" << hex << Value << dec  << " at " << TotalCycles << "\n"; */
      UserVIAState.timer1l&=0xff;
      UserVIAState.timer1l|=(Value & 0xff)<<8;
      UserVIAState.timer1c=UserVIAState.timer1l * 2;
      UserVIAState.ifr &=0xbf; /* clear timer 1 ifr */
      UserVIAState.timer1hasshot=0;
      /* If PB7 toggling enabled, then lower PB7 now */
      if (UserVIAState.acr & 128) {
        UserVIAState.orb&=0x7f;
        UserVIAState.irb&=0x7f;
      };
      UpdateIFRTopBit();
	  WTDelay1=4;
      break;

    case 7:
      /*cerr << "UserVia Reg7 Timer1 hi latch Write val=0x" << hex << Value << dec  << " at " << TotalCycles << "\n"; */
      UserVIAState.timer1l&=0xff;
      UserVIAState.timer1l|=(Value & 0xff)<<8;
      break;

    case 8:
      /* cerr << "UserVia Reg8 Timer2 lo Counter Write val=0x" << hex << Value << dec << "\n"; */
      UserVIAState.timer2l&=0xff00;
      UserVIAState.timer2l|=(Value & 0xff);
      break;

    case 9:
      /* cerr << "UserVia Reg9 Timer2 hi Counter Write val=0x" << hex << Value << dec << "\n";
      core_dumpstate(); */
      UserVIAState.timer2l&=0xff;
      UserVIAState.timer2l|=(Value & 0xff)<<8;
      UserVIAState.timer2c=UserVIAState.timer2l * 2;
      UserVIAState.ifr &=0xdf; /* clear timer 2 ifr */
      UpdateIFRTopBit();
	  WTDelay2=4;
      break;

    case 10:
      break;

    case 11:
      UserVIAState.acr=Value & 0xff;
      break;

    case 12:
      UserVIAState.pcr=Value & 0xff;
      break;

    case 13:
      UserVIAState.ifr&=~(Value & 0xff);
      UpdateIFRTopBit();
      break;

    case 14:
      /* cerr << "User VIA Write ier Value=" << Value << "\n"; */
      if (Value & 0x80)
        UserVIAState.ier|=Value & 0xff;
      else
        UserVIAState.ier&=~(Value & 0xff);
      UserVIAState.ier&=0x7f;
      UpdateIFRTopBit();
      break;

    case 15:
      UserVIAState.ora=Value & 0xff;
      break;
  } /* Address switch */
} /* UserVIAWrite */

/*--------------------------------------------------------------------------*/
/* Address is in the range 0-f - with the fe60 stripped out */
int UserVIARead(int Address) {
  int tmp;
  /* cerr << "UserVIARead: Address=0x" << hex << Address << dec << " at " << TotalCycles << "\n";
  DumpRegs(); */
  switch (Address) {
    case 0: /* IRB read */
      tmp=(UserVIAState.orb & UserVIAState.ddrb) | (UserVIAState.irb & (~UserVIAState.ddrb));

      if (AMXMouseEnabled) {
        if (AMXLRForMiddle) {
          if ((AMXButtons & AMX_LEFT_BUTTON) && (AMXButtons & AMX_RIGHT_BUTTON))
            AMXButtons = AMX_MIDDLE_BUTTON;
          else
            AMXButtons &= ~AMX_MIDDLE_BUTTON;
        }

        tmp &= 0x1f;
        tmp |= (AMXButtons ^ 7) << 5;
        UserVIAState.ifr&=0xe7;
        UpdateIFRTopBit();

        /* Set up another interrupt if not at target */
        if (AMXTargetX != AMXCurrentX || AMXTargetY != AMXCurrentY) {
          SetTrigger(AMX_TRIGGER, AMXTrigger);
        }
        else {
          ClearTrigger(AMXTrigger);
        }
      }
      return(tmp);

    case 2:
      return(UserVIAState.ddrb);

    case 3:
      return(UserVIAState.ddra);

    case 4: /* Timer 1 lo counter */
      tmp=UserVIAState.timer1c / 2;
      UserVIAState.ifr&=0xbf; /* Clear bit 6 - timer 1 */
      UpdateIFRTopBit();
	  if (UserVIAState.timer1c<=(UserVIAState.timer1l*2)) 
      return(tmp & 0xff);
	  else
	  return(0xff);

    case 5: /* Timer 1 ho counter */
      tmp=UserVIAState.timer1c /512;
	  if (UserVIAState.timer1c<=(UserVIAState.timer1l*2)) 
      return(tmp & 0xff);
	  else
	  return(0xff);

    case 6: /* Timer 1 lo latch */
      return(UserVIAState.timer1l & 0xff);

    case 7: /* Timer 1 ho latch */
      return((UserVIAState.timer1l / 256) & 0xff);

    case 8: /* Timer 2 lo counter */
      tmp=UserVIAState.timer2c / 2;
      UserVIAState.ifr&=0xdf; /* Clear bit 5 - timer 2 */
      UpdateIFRTopBit();
	  if (UserVIAState.timer2c<=(UserVIAState.timer2l*2))
	      return(tmp & 0xff);
	  else
		  return(0xff);

    case 9: /* Timer 2 ho counter */
	  if (UserVIAState.timer2c<=(UserVIAState.timer2l*2)) 
		return((UserVIAState.timer2c / 512) & 0xff);
	  else
		  return(0xff);

    case 12:
      return(UserVIAState.pcr);

    case 13:
      UpdateIFRTopBit();
      return(UserVIAState.ifr);
      break;

    case 14:
      return(UserVIAState.ier | 0x80);

    case 1:
      UserVIAState.ifr&=0xfc;
      UpdateIFRTopBit();
    case 15:
      return(255);
      break;
  } /* Address switch */
  return(0xff);
} /* UserVIARead */

/*--------------------------------------------------------------------------*/
void UserVIATriggerCA1Int(void) {
  /* We should be concerned with active edges etc. */
  UserVIAState.ifr|=2; /* CA1 */
  UpdateIFRTopBit();
}; /* UserVIATriggerCA1Int */

/*--------------------------------------------------------------------------*/
void UserVIA_poll_real(void) {
  int tCycles;
  if (UserVIAState.timer1c<0) {
	tCycles=abs(UserVIAState.timer1c);
    /*cerr << "UserVIA timer1c\n"; */
    UserVIAState.timer1c=(UserVIAState.timer1l * 2)-(tCycles-4);
    if ((UserVIAState.timer1hasshot==0) || (UserVIAState.acr & 0x40)) {
      /*cerr << "UserVIA timer1c - int at " << TotalCycles << "\n"; */
      UserVIAState.ifr|=0x40; /* Timer 1 interrupt */
      UpdateIFRTopBit();
      if (UserVIAState.acr & 0x80) {
        UserVIAState.orb^=0x80; /* Toggle PB7 */
        UserVIAState.irb^=0x80; /* Toggle PB7 */
      };
    };
    UserVIAState.timer1hasshot=1;
  } /* timer1c underflow */

  if (UserVIAState.timer2c<0) {
	tCycles=abs(UserVIAState.timer2c);
    /* cerr << "UserVIA timer2c\n"; */
    UserVIAState.timer2c=(UserVIAState.timer2l * 2)+(tCycles-(3+UserVIAState.timer2adjust));
	UserVIAState.timer2adjust=1-UserVIAState.timer2adjust;	
    if (!(UserVIAState.ifr&=0x20)) {
     /* cerr << "UserVIA timer2c - int\n"; */
      UserVIAState.ifr|=0x20; /* Timer 2 interrupt */
      UpdateIFRTopBit();
    };
  } /* timer2c underflow */
} /* UserVIA_poll */

/*--------------------------------------------------------------------------*/
void UserVIAReset(void) {
  VIAReset(&UserVIAState);
  ClearTrigger(AMXTrigger);
  ClearTrigger(PrinterTrigger);
} /* UserVIAReset */

/*-------------------------------------------------------------------------*/
void SaveUserVIAState(unsigned char *StateData) {
	SaveVIAState(&UserVIAState, StateData);
}

/*-------------------------------------------------------------------------*/
void RestoreUserVIAState(unsigned char *StateData) {
	RestoreVIAState(&UserVIAState, StateData);
}

/*-------------------------------------------------------------------------*/
void AMXMouseMovement() {
	ClearTrigger(AMXTrigger);

	/* Check if there is a outstanding interrupt */
	if (AMXMouseEnabled && (UserVIAState.ifr & 0x18) == 0)
	{
		if (AMXTargetX != AMXCurrentX || AMXTargetY != AMXCurrentY)
		{
			if (AMXTargetX != AMXCurrentX)
			{
				UserVIAState.ifr |= 0x10;
				if (AMXTargetX < AMXCurrentX)
				{
					UserVIAState.irb &= ~0x01;
					AMXCurrentX--;
				}
				else
				{
					UserVIAState.irb |= 0x01;
					AMXCurrentX++;
				}
			}
			if (AMXTargetY != AMXCurrentY)
			{
				UserVIAState.ifr |= 0x08;
				if (AMXTargetY > AMXCurrentY)
				{
					UserVIAState.irb &= ~0x04;
					AMXCurrentY++;
				}
				else
				{
					UserVIAState.irb |= 0x04;
					AMXCurrentY--;
				}
			}
			UpdateIFRTopBit();
		}
	}
}

/*-------------------------------------------------------------------------*/
void PrinterEnable(char *FileName) {
	/* Close file if already open */
	if (PrinterFileHandle != NULL)
	{
		fclose(PrinterFileHandle);
		PrinterFileHandle = NULL;
	}

	strcpy(PrinterFileName, FileName);
	PrinterFileHandle = fopen(FileName, "wb");
	if (PrinterFileHandle == NULL)
	{
#ifdef WIN32
		char errstr[200];
		sprintf(errstr, "Failed to open printer:\n  %s", PrinterFileName);
		MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
#else
		cerr << "Failed to open printer " << PrinterFileName << "\n";
#endif
	}
	else
	{
		PrinterEnabled = 1;
		SetTrigger(PRINTER_TRIGGER, PrinterTrigger);
	}
}

/*-------------------------------------------------------------------------*/
void PrinterDisable() {
	if (PrinterFileHandle != NULL)
	{
		fclose(PrinterFileHandle);
		PrinterFileHandle = NULL;
	}

	PrinterEnabled = 0;
	ClearTrigger(PrinterTrigger);
}
/*-------------------------------------------------------------------------*/
void PrinterPoll() {
	ClearTrigger(PrinterTrigger);
	UserVIATriggerCA1Int();

	/* The CA1 interrupt is not always picked up,
		set up a trigger just in case. */
	SetTrigger(100000, PrinterTrigger);
}

/*--------------------------------------------------------------------------*/
void uservia_dumpstate(void) {
  cerr << "Uservia:\n";
  via_dumpstate(&UserVIAState);
}; /* uservia_dumpstate */
