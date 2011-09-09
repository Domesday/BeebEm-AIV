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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "6502core.h"
#include "sysvia.h"
#include "uservia.h"
#include "video.h"
#include "beebsound.h"
#include "beebmem.h"
#include "beebstate.h"
#include "main.h"

#ifdef WIN32
#include <windows.h>
#endif

/*--------------------------------------------------------------------------*/
void BeebSaveState(char *FileName)
{
	FILE *StateFile;
	BeebState StateData;
	size_t StateSize;

	/* Get all the state data */
	memset(&StateData, 0, BEEB_STATE_SIZE);
	strcpy(StateData.Tag, BEEB_STATE_FILE_TAG);
	Save6502State(StateData.CPUState);
	SaveSysVIAState(StateData.SysVIAState);
	SaveUserVIAState(StateData.UserVIAState);
	SaveVideoState(StateData.VideoState);
	SaveMemState(StateData.RomState,StateData.MemState,StateData.SWRamState);

	if (StateData.RomState[0] == 0)
		StateSize = BEEB_STATE_SIZE_NO_SWRAM;
	else
		StateSize = BEEB_STATE_SIZE;

	/* Write the data to the file */
	StateFile = fopen(FileName,"wb");
	if (StateFile != NULL)
	{
		if (fwrite(&StateData,1,StateSize,StateFile) != StateSize)
		{
#ifdef WIN32
			char errstr[200];
			sprintf(errstr, "Failed to write to BeebState file:\n  %s", FileName);
			MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
#else
			fprintf(stderr,"Failed to write to BeebState file: %s\n",FileName);
#endif
		}
		fclose(StateFile);
	}
	else
	{
#ifdef WIN32
	char errstr[200];
	sprintf(errstr, "Cannot open BeebState file:\n  %s", FileName);
	MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
#else
	fprintf(stderr,"Cannot open BeebState file: %s\n",FileName);
#endif
	}
}

/*--------------------------------------------------------------------------*/
void BeebRestoreState(char *FileName)
{
	FILE *StateFile;
	BeebState StateData;
	size_t StateSize;

	memset(&StateData, 0, BEEB_STATE_SIZE);

	/* Read the data from the file */
	StateFile = fopen(FileName,"rb");
	if (StateFile != NULL)
	{
		StateSize = fread(&StateData,1,BEEB_STATE_SIZE,StateFile);
		if ((StateSize == BEEB_STATE_SIZE && StateData.RomState[0] != 0) ||
			(StateSize == BEEB_STATE_SIZE_NO_SWRAM && StateData.RomState[0] == 0))
		{
			if (strcmp(StateData.Tag, BEEB_STATE_FILE_TAG) == 0)
			{
				/* Restore all the state data */
				Restore6502State(StateData.CPUState);
				RestoreSysVIAState(StateData.SysVIAState);
				RestoreUserVIAState(StateData.UserVIAState);
				RestoreVideoState(StateData.VideoState);
				RestoreMemState(StateData.RomState,StateData.MemState,StateData.SWRamState);

				/* Now reset parts of the emulator that are not restored */
				if (SoundEnabled)
				{
					SoundReset();
					SoundInit();
				}
			}
			else
			{
#ifdef WIN32
				char errstr[200];
				sprintf(errstr, "Not a BeebState file:\n  %s", FileName);
				MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
#else
				fprintf(stderr,"Not a BeebState file: %s\n",FileName);
#endif
			}
		}
		else
		{
#ifdef WIN32
			char errstr[200];
			sprintf(errstr, "BeebState file is wrong size:\n  %s", FileName);
			MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
#else
			fprintf(stderr,"BeebState file is wrong size: %s\n",FileName);
#endif
		}
		fclose(StateFile);
	}
	else
	{
#ifdef WIN32
	char errstr[200];
	sprintf(errstr, "Cannot open BeebState file:\n  %s", FileName);
	MessageBox(GETHWND,errstr,"BBC Emulator",MB_OK|MB_ICONERROR);
#else
	fprintf(stderr,"Cannot open BeebState file: %s\n",FileName);
#endif
	}
}

