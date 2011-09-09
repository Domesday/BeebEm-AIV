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

/***************
 * debugLogs.cpp : Introduced by D.M.Sergeant (17/10/2002)
 * Implements simple functions to open and close the log files
 ***************/

#include <stdio.h>

int logInstr = 0;
int dms_win_flag = 0;

void openLog(FILE * &logf, char log_fn[])
{
   if (logf != NULL)
      return;
   else
      logf = fopen(log_fn, "wb");
   if (logf == NULL)
      printf("Could not open Log file \"%s\"\n", log_fn);
   else
   {
      fprintf(logf, "Log File Opened\n");
      fflush(logf);
   }
}

void closeLog(FILE * &logf)
{
   if (logf != NULL)
      fclose(logf);
}
