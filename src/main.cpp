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
/* Mike Wyatt and NRM's port to win32 - 7/6/97 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <iostream.h>
#include <windows.h>

#include "6502core.h"
#include "beebmem.h"
#include "beebsound.h"
#include "sysvia.h"
#include "uservia.h"
#include "beebwin.h"
#include "video.h"
#include "via.h"
#include "disc1770.h"
#include "serial.h"
#include "tube.h"
#include "debugLogs.h"

#ifdef MULTITHREAD
#undef MULTITHREAD
#endif

#ifdef USE_MAIN_LOG
FILE *mainlog=NULL;
#endif

extern VIAState SysVIAState;
int DumpAfterEach=0;

unsigned char MachineType;
BeebWin *mainWin = NULL;
HINSTANCE hInst;
DWORD iSerialThread,iStatThread; // Thread IDs

int CALLBACK WinMain(HINSTANCE hInstance, 
			HINSTANCE hPrevInstance,
			LPSTR lpszCmdLine,
			int nCmdShow)
{
  MSG msg;

#ifdef USE_MAIN_LOG
  openLog(mainlog, MAIN_LOG_FN);
#endif

  hInst = hInstance;
  mainWin=new BeebWin();

  mainWin->Initialise();
  SoundReset();

  SoundDefault = 0;	// [DMS] disable sound - whatever any other part thinks

  if (SoundDefault)
    SoundInit();
  mainWin->ResetBeebSystem(MachineType,TubeEnabled,1); 
  mainWin->SetRomMenu();
  mainWin->SetSoundMenu();
  mainWin->m_frozen=FALSE;
  do
  {
    if(PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) || mainWin->IsFrozen())
    {
      if (!GetMessage(&msg, NULL, 0, 0))
      {
	break;              // Quit the app on WM_QUIT
      }

      TranslateMessage(&msg);// Translates virtual key codes
      DispatchMessage(&msg); // Dispatches message to window
    }

    if (!mainWin->IsFrozen())
    {
      Exec6502Instruction();
    }
  } while(1);
  
  mainWin->KillDLLs();

  delete mainWin;
  Kill_Serial();
  return(0);  
} /* main */

