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
/* Beeb state save and restore funcitonality - Mike Wyatt 7/6/97 */

/* Format for Beeb State files:

  Offset      Content

  0000-000f   "BBC Micro State" - null terminated string identifying the
                                  file as a beeb state file.

  0010-001f   CPU State
  0010          Program Counter low byte
  0011          Program Counter high byte
  0012          Accumulator
  0013          X Register
  0014          Y Register
  0015          Stack Register
  0016          PSR - processor flags
  0017-001f     Not currently used

  0020-003f   System VIA State
  0020          output reg b
  0021          input reg b
  0022          output reg a
  0023          input reg a
  0024          data direction reg b
  0025          data direction reg a
  0026          timer 1 counter low byte
  0027          timer 1 counter high byte
  0028          timer 1 latch low byte
  0029          timer 1 latch high byte
  002a          timer 2 counter low byte
  002b          timer 2 counter high byte
  002c          timer 2 latch low byte
  002d          timer 2 latch high byte
  002e          shift register
  002f          aux control reg
  0030          peripheral control reg
  0031          interrupt flag reg
  0032          interrupt enable reg
  0033-0037     Not currently used
  0038          IC32 state - addressable latch programmed through bits 0-3
                             of port B of the System VIA
  0039-003f     Not currently used

  0040-005f   User VIA State
  0040-0052     same as system VIA
  0053-005f     Not currently used

  0060-009f   Video State (6845 and video ULA)
  0060          6845 control reg
  0061-0072     6845 registers R0 to R17
  0073-007f     Not currently used
  0080          ULA control reg
  0081-0090     ULA palette registers
  0091-009f     Not currently used  

  00a0-00af   ROM and RAM state
  00a0          Flag (0 or 1) indicating if sideways RAM is in use

  00b0-00ff   Not currently used

  0100-80ff   32KB of memory

  8100-c0ff   16KB of sideways RAM (not used if RAM flag is 0)

*/

/* Struture to hold all the state information */
typedef struct
{
	char Tag[16];
	unsigned char CPUState[16];
	unsigned char SysVIAState[32];
	unsigned char UserVIAState[32];
	unsigned char VideoState[64];
	unsigned char RomState[16];
	unsigned char UnusedState[80]; /* For future use */
	unsigned char MemState[32768];
	unsigned char SWRamState[16384];
} BeebState;

#define BEEB_STATE_FILE_TAG			"BBC Micro State"
#define BEEB_STATE_SIZE				sizeof(BeebState)
#define BEEB_STATE_SIZE_NO_SWRAM	(sizeof(BeebState) - 16384)

void BeebSaveState(char *FileName);
void BeebRestoreState(char *FileName);
