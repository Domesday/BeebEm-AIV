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
/**************
 * debugLogs.h : Introduced by D.M.Sergeant (17/10/2002)
 * This defines the filenames for the various debug logs, and also
 * provides a function call to open and close the logfile.
 **************/
#include "debugConf.h"

#ifdef USE_CORE_LOG
#define CORE_LOG_FN	"logs/oscli.log"
#endif
#ifdef USE_INSTR_LOG
#define INSTR_LOG_FN	"logs/instr.log"
extern int logInstr;
#endif
#ifdef USE_FDC2_LOG
#define FDC2_LOG_FN	"logs/fdc2.log"
#endif
#ifdef USE_FD_LOG
#define FD_LOG_FN	"logs/fd.log"
#endif
#ifdef USE_LVROM_LOG
#define LVROM_LOG_FN	"logs/lvrom.log"
#endif
#ifdef USE_AIVSCSI_LOG
#define SCSI_LOG_FN	"logs/aivscsi.log"
#endif
#ifdef USE_SCSICMD_LOG
#define SCSICMD_LOG_FN	"logs/scsicom.log"
#endif
#ifdef USE_VIA_LOG
#define VIA_LOG_FN	"logs/via.log"
#endif
#ifdef USE_TUBE_LOG
#define TUBE_LOG_FN	"logs/tubeula.log"
#endif
#ifdef USE_TUBEINST_LOG
#define TUBEINST_LOG_FN	"logs/tube.log"
#endif
#ifdef USE_CRTC_LOG
#define CRTC_LOG_FN	"logs/crtc.log"
#endif
#ifdef USE_MAIN_LOG
#define MAIN_LOG_FN	"logs/main.log"
#endif
#ifdef USE_WIN_LOG
#define WIN_LOG_FN	"logs/win.log"
extern int dms_win_flag;
#endif

extern void openLog(FILE * &logf, char log_fn[]);
extern void closeLog(FILE * &logf);

