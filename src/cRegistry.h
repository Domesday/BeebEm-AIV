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

// This class is an easier way of accessing the registry from a c++ application.
// By David Overton (david@overton.org.uk (NO SPAM)).
// See cRegistry.cpp for more comments and stuff.

#if !defined(_cRegistry_H_)
#define _cRegistry_H_

#include <windows.h>
#define MAX_BUFF_LENGTH 1024 // maximum length of data (in bytes) that you may read in.

class cRegistry {
public:
	bool CreateKey(HKEY hKeyRoot, LPSTR lpSubKey);
	bool DeleteKey(HKEY hKeyRoot, LPSTR lpSubKey);

	bool DeleteValue(HKEY hKeyRoot, LPSTR lpSubKey, LPSTR lpValueName);

	bool GetBinaryValue(HKEY hKeyRoot, LPSTR lpSubKey, LPSTR lpValue, PVOID pData, int* pnSize);
	bool GetDWORDValue(HKEY hKeyRoot, LPSTR lpSubKey, LPSTR lpValue, DWORD &dwBuffer);
	bool GetStringValue(HKEY hKeyRoot, LPSTR lpSubKey, LPSTR lpValue, LPSTR lpBuffer);

	bool SetBinaryValue(HKEY hKeyRoot, LPSTR lpSubKey, LPSTR lpValue, PVOID pData, int* pnSize);
	bool SetDWORDValue(HKEY hKeyRoot, LPSTR lpSubKey, LPSTR lpValue, DWORD dwValue);
	bool SetStringValue(HKEY hKeyRoot, LPSTR lpSubKey, LPSTR lpValue, LPSTR lpData);
};


#endif