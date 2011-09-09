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
/* Beebemulator - memory subsystem - David Alan Gilbert 16/10/94 */
#ifndef BEEBMEM_HEADER
#define BEEBMEM_HEADER

#include "stdio.h"

#define STD_OS_LDADDR		0xC000
#define STD_PAGE_ROM_LDADDR	0x8000
#define STD_ROM_SIZE		0x4000
extern int WritableRoms;  /* Is writing to a ROM allowed */

extern int RomWritable[16]; /* Allow writing to ROMs on an individual basis */

extern unsigned char WholeRam[65536];
extern unsigned char Roms[16][16384];
extern unsigned char ROMSEL;
extern int PagedRomReg;
/* Master 128 Specific Stuff */
extern unsigned char FSRam[12228]; // 12K Filing System RAM (yes ok its only 8, i misread ;P)
extern unsigned char PrivateRAM[4096]; // 4K Private RAM (VDU Use mainly)
extern int CMOSRAM[64]; // 50 Bytes CMOS RAM
extern unsigned char ShadowRAM[32768]; // 20K Shadow RAM
extern unsigned char MOSROM[16384]; // 12K MOS Store for swapping FS ram in and out
extern unsigned char ACCCON; // ACCess CONtrol register
extern unsigned char UseShadow; // 1 to use shadow ram, 0 to use main ram
extern unsigned char MainRAM[32768]; // temporary store for main ram when using shadow ram
struct CMOSType {
	unsigned char Enabled;
	unsigned char ChipSelect;
	unsigned char Address;
    unsigned char StrobedData;
	unsigned char DataStrobe;
	unsigned char Op;
};
extern struct CMOSType CMOS;
extern unsigned char Sh_Display,Sh_CPUX,Sh_CPUE,PRAM,FRAM;
extern char RomPath[512];
/* End of Master 128 Specific Stuff, note initilised anyway regardless of Model Type in use */
/* NOTE: Only to be used if 'a' doesn't change the address */
#define BEEBREADMEM_FAST(a) ((a<0xfc00)?WholeRam[a]:BeebReadMem(a))
/* as BEEBREADMEM_FAST but then increments a */
#define BEEBREADMEM_FASTINC(a) ((a<0xfc00)?WholeRam[a++]:BeebReadMem(a++))

int BeebReadMem(int Address);
void BeebWriteMem(int Address, int Value);
#define BEEBWRITEMEM_FAST(Address, Value) if (Address<0x8000) WholeRam[Address]=Value; else BeebWriteMem(Address,Value);
#define BEEBWRITEMEM_DIRECT(Address, Value) WholeRam[Address]=Value;
char *BeebMemPtrWithWrap(int a, int n);
char *BeebMemPtrWithWrapMo7(int a, int n);
void BeebReadRoms(void);
void BeebMemInit(unsigned char LoadRoms);

void SaveMemState(unsigned char *RomState,unsigned char *MemState,unsigned char *SWRamState);
void RestoreMemState(unsigned char *RomState,unsigned char *MemState,unsigned char *SWRamState);

/* Used to show the Rom titles from the options menu */
char *ReadRomTitle( int bank, char *Title, int BufSize );

void beebmem_dumpstate(void);
void SaveMemUEF(FILE *SUEF);
extern int EFDCAddr; // 1770 FDC location
extern int EDCAddr; // Drive control location
extern bool NativeFDC; // see beebmem.cpp for description
void LoadRomRegsUEF(FILE *SUEF);
void LoadMainMemUEF(FILE *SUEF);
void LoadShadMemUEF(FILE *SUEF);
void LoadPrivMemUEF(FILE *SUEF);
void LoadFileMemUEF(FILE *SUEF);
void LoadSWRMMemUEF(FILE *SUEF);
#endif
