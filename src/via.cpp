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
/* Support file for 6522 via - 30/10/94 - David Alan Gilbert */

#ifndef VIA_HEADER
#define VIA_HEADER
#include <iostream.h>
#include "via.h"
#include <stdio.h>
#include "sysvia.h"
#include "uservia.h"
#include "uefstate.h"
#include "viastate.h"

void VIAReset(VIAState *ToReset) {
  ToReset->ora=ToReset->orb=0xff; /* ??? */
  ToReset->ira=ToReset->irb=0xff; /* ??? */
  ToReset->ddra=ToReset->ddrb=0; /* All inputs */
  ToReset->acr=0; /* Timed ints on t1, t2, no pb7 hacking, no latching, no shifting */
  ToReset->pcr=0; /* Neg edge inputs for cb2,ca2 and CA1 and CB1 */
  ToReset->ifr=0; /* No interrupts presently interrupting */
  ToReset->ier=0x80; /* No interrupts enabled */
  ToReset->timer1l=ToReset->timer2l=0xffff;/*0xffff; */
  ToReset->timer1c=ToReset->timer2c=0xffff;/*0x1ffff; */
  ToReset->timer1hasshot=0;
  ToReset->timer2hasshot=0;
  ToReset->timer2adjust=0;
} /* VIAReset */

/*-------------------------------------------------------------------------*/
void SaveVIAState(VIAState *VIAData, unsigned char *StateData) {
	StateData[0] = VIAData->orb;
	StateData[1] = VIAData->irb;
	StateData[2] = VIAData->ora;
	StateData[3] = VIAData->ira;
	StateData[4] = VIAData->ddrb;
	StateData[5] = VIAData->ddra;
	StateData[6] = (VIAData->timer1c / 2) & 255; /* Timers stored in 2MHz */
	StateData[7] = (VIAData->timer1c / 2) >> 8;  /* units for speed */
	StateData[8] = VIAData->timer1l & 255;
	StateData[9] = VIAData->timer1l >> 8;
	StateData[10] = (VIAData->timer2c / 2) & 255;
	StateData[11] = (VIAData->timer2c / 2) >> 8;
	StateData[12] = VIAData->timer2l & 255;
	StateData[13] = VIAData->timer2l >> 8;
	StateData[14] = 0;  /* Shift register - not currently used */
	StateData[15] = VIAData->acr;
	StateData[16] = VIAData->pcr;
	StateData[17] = VIAData->ifr;
	StateData[18] = VIAData->ier;
}

/*-------------------------------------------------------------------------*/
void RestoreVIAState(VIAState *VIAData, unsigned char *StateData) {
	VIAData->orb = StateData[0];
	VIAData->irb = StateData[1];
	VIAData->ora = StateData[2];
	VIAData->ira = StateData[3];
	VIAData->ddrb = StateData[4];
	VIAData->ddra = StateData[5];
	VIAData->timer1c = (StateData[6] + (StateData[7] << 8)) * 2;
	VIAData->timer1l = StateData[8] + (StateData[9] << 8);
	VIAData->timer2c = (StateData[10] + (StateData[11] << 8)) * 2;
	VIAData->timer2l = StateData[12] + (StateData[13] << 8);
	VIAData->acr = StateData[15];
	VIAData->pcr = StateData[16];
	VIAData->ifr = StateData[17];
	VIAData->ier = StateData[18];
	VIAData->timer1hasshot = 0;
	VIAData->timer2hasshot = 0;
}

/*-------------------------------------------------------------------------*/
void via_dumpstate(VIAState *ToDump) {
  cerr << "  ora=" << int(ToDump->ora) << "\n";
  cerr << "  orb="  << int(ToDump->orb) << "\n";
  cerr << "  ira="  << int(ToDump->ira) << "\n";
  cerr << "  irb="  << int(ToDump->irb) << "\n";
  cerr << "  ddra="  << int(ToDump->ddra) << "\n";
  cerr << "  ddrb="  << int(ToDump->ddrb) << "\n";
  cerr << "  acr="  << int(ToDump->acr) << "\n";
  cerr << "  pcr="  << int(ToDump->pcr) << "\n";
  cerr << "  ifr="  << int(ToDump->ifr) << "\n";
  cerr << "  ier="  << int(ToDump->ier) << "\n";
  cerr << "  timer1c="  << ToDump->timer1c << "\n";
  cerr << "  timer2c="  << ToDump->timer2c << "\n";
  cerr << "  timer1l="  << ToDump->timer1l << "\n";
  cerr << "  timer2l="  << ToDump->timer2l << "\n";
  cerr << "  timer1hasshot="  << ToDump->timer1hasshot << "\n";
  cerr << "  timer2hasshot="  << ToDump->timer2hasshot << "\n";
}; /* via_dumpstate */

void SaveVIAUEF(FILE *SUEF) {
	VIAState *VIAPtr;
	for (int n=0;n<2;n++) {
		if (!n) VIAPtr=&SysVIAState; else VIAPtr=&UserVIAState;
		fput16(0x0467,SUEF);
		fput32(22,SUEF);
		fputc(n,SUEF);
		fputc(VIAPtr->orb,SUEF);
		fputc(VIAPtr->irb,SUEF);
		fputc(VIAPtr->ora,SUEF);
		fputc(VIAPtr->ira,SUEF);
		fputc(VIAPtr->ddrb,SUEF);
		fputc(VIAPtr->ddra,SUEF);
		fput16(VIAPtr->timer1c,SUEF);
		fput16(VIAPtr->timer1l,SUEF);
		fput16(VIAPtr->timer2c,SUEF);
		fput16(VIAPtr->timer2l,SUEF);
		fputc(VIAPtr->acr,SUEF);
		fputc(VIAPtr->pcr,SUEF);
		fputc(VIAPtr->ifr,SUEF);
		fputc(VIAPtr->ier,SUEF);
		fputc(VIAPtr->timer1hasshot,SUEF);
		fputc(VIAPtr->timer2hasshot,SUEF);
		if (!n) fputc(IC32State,SUEF); else fputc(0,SUEF);
	}
}

void LoadViaUEF(FILE *SUEF) {
	VIAState *VIAPtr;
	int VIAType;
	VIAType=fgetc(SUEF); if (VIAType) VIAPtr=&UserVIAState; else VIAPtr=&SysVIAState;
	VIAPtr->orb=fgetc(SUEF);
	VIAPtr->irb=fgetc(SUEF);
	VIAPtr->ora=fgetc(SUEF);
	VIAPtr->ira=fgetc(SUEF);
	VIAPtr->ddrb=fgetc(SUEF);
	VIAPtr->ddra=fgetc(SUEF);
	VIAPtr->timer1c=fget16(SUEF);
	VIAPtr->timer1l=fget16(SUEF);
	VIAPtr->timer2c=fget16(SUEF);
	VIAPtr->timer2l=fget16(SUEF);
	VIAPtr->acr=fgetc(SUEF);
	VIAPtr->pcr=fgetc(SUEF);
	VIAPtr->ifr=fgetc(SUEF);
	VIAPtr->ier=fgetc(SUEF);
	VIAPtr->timer1hasshot=fgetc(SUEF);
	VIAPtr->timer2hasshot=fgetc(SUEF);
	if (!VIAType) IC32State=fgetc(SUEF);
}
#endif