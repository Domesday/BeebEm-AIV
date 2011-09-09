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

#ifndef BEEBWIN_HEADER
#define BEEBWIN_HEADER

#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <ddraw.h>
#include "port.h"
#include "video.h"

// Include the different video mixing modes that the LV-Rom player can handle
// (these define how the BBC video output is combined with the LV-Rom video
// output.) [Richard Gellman]
#define MIXMODE_LV  1	/* Video overlay VP1, LaserVision only */
#define MIXMODE_BBC 2	/* VP2, External (computer) RGB only */
#define MIXMODE_HK  3	/* VP3, Hard Keyed. Ext RGB overlayed */
#define MIXMODE_MIX 4	/* VP4, Mixed, transparent overlay */
#define MIXMODE_ENH 5	/* VP5, Enhanced, LV highlighted by Ext RGB */

/* Used in message boxes */
#define GETHWND (mainWin == NULL ? NULL : mainWin->GethWnd())

typedef union
{
  unsigned char data[8];
  EightByteType eightbyte;
} EightUChars;

typedef union
{
  unsigned char data[16];
  EightByteType eightbytes[2];
} SixteenUChars;
 
typedef struct
{
  BITMAPINFOHEADER	bmiHeader;
  RGBQUAD		bmiColors[256];
} bmiData;

struct LEDType
{
  bool ShiftLock;
  bool CapsLock;
  bool Motor;
  bool Disc0;
  bool Disc1;
  bool ShowDisc;
  bool ShowKB;
};
extern struct LEDType LEDs;

extern bmiData lvBmi;
extern unsigned char MixMode;
extern HDC hdcMem;
extern HBITMAP hbmMem;

class BeebWin
{
  public:
    enum PaletteType { RGB, BW, AMBER, GREEN } palette_type;
    unsigned char cols[9];
    HMENU  m_hMenu;
    BOOL   m_frozen;
    double m_RelativeSpeed;

    BeebWin();
    ~BeebWin();

    void Initialise();

    void UpdateModelType();
    void SetSoundMenu(void);
    void SetPBuff(void);
    void SetImageName(char *DiscName,char Drive,char DType);
    void SetTapeSpeedMenu(void);
    void SelectFDC(void);
    void LoadFDC(char *DLLName);
    void KillDLLs(void);
    void UpdateLEDMenu(HMENU hMenu);
    void SetDriveControl(unsigned char value);
    unsigned char GetDriveControl(void);
    void doLED(int sx,bool on);

    void updateLines(HDC hDC, int starty, int nlines);
    void updateLines(int starty, int nlines)
    {
      updateLines(m_hDC, starty, nlines);
    };

    void doHorizLine(unsigned long Col, int y, int sx, int width)
    {
      if (y>500)
        return;
      if (TeletextEnabled)
        y/=TeletextStyle;
      memset((char *)beebscreen+ (y* 640) + sx+36, Col , width);
    };

    void doInvHorizLine(unsigned long Col, int y, int sx, int width)
    {
      char *vaddr;
      if (TeletextEnabled)
        y/=TeletextStyle;
      vaddr=(char *)beebscreen+ (y* 640) + sx+((TeletextEnabled)?36:0);
      for (int n=0;n<width;n++)
      {
        *(vaddr+n)^=Col;
      }
    };

    void doUHorizLine(unsigned long Col, int y, int sx, int width)
    {
      if (y>500)
        return;
      if (TeletextEnabled)
        y/=TeletextStyle;
      memset((char *)beebscreen+ (y* 640) + sx+36, Col , width);
    };

//	void doHorizLine(unsigned long Col, int offset, int width) {
//		unsigned int tsx;
//		if ((offset+width)<640*256) return;
//		tsx=((offset/640)*640)+((offset % 640)/2);
//		memset(m_screen+tsx,Col,width);
//	};

    void SetRomMenu(void);	// LRW  Added for individual ROM/Ram

    EightUChars *GetLinePtr(int y)
    {
      if(y > 255)
	y=255;
      return((EightUChars *)((char *)beebscreen + ( y * 640 )));
    }

    SixteenUChars *GetLinePtr16(int y)
    {
      if(y > 255)
        y=255;
      return((SixteenUChars *)((char *)beebscreen + ( y * 640 )));
    }

    char *imageData(void)
    {
      return (char *)beebscreen;
    }

    HWND GethWnd() { return m_hWnd; };
	
    void RealizePalette(HDC) {};
    void ResetBeebSystem(unsigned char NewModelType, unsigned char TubeStatus,
			unsigned char LoadRoms);

    int StartOfFrame(void);
    BOOL UpdateTiming(void);
    void DisplayTiming(void);
    void ScaleJoystick(unsigned int x, unsigned int y);
    void SetMousestickButton(int button);
    void ScaleMousestick(unsigned int x, unsigned int y);
    void HandleCommand(int MenuId);
    void SetAMXPosition(unsigned int x, unsigned int y);
    void Focus(BOOL);
    BOOL IsFrozen(void);

    void ShowMenu(bool on);
    void TrackPopupMenu(int x, int y);
    bool IsFullScreen() { return m_isFullScreen; }
    char *m_screen;
    double m_RealTimeTarget;

    HDC 	m_hDC;
    HWND	m_hWnd;
  private:
    int		m_MenuIdWinSize;
    int		m_XWinSize, m_YWinSize;
    int		m_XWinPos, m_YWinPos;
    BOOL	m_ShowSpeedAndFPS;
    int		m_MenuIdSampleRate;
    int		m_MenuIdVolume;
    int		m_DiscTypeSelection;
    int		m_MenuIdTiming;
    int		m_FPSTarget;
    JOYCAPS	m_JoystickCaps;
    int		m_MenuIdSticks;
    BOOL	m_HideCursor;
    BOOL	m_FreezeWhenInactive;
    int		m_MenuIdKeyMapping;
    char	m_AppPath[_MAX_PATH];
    BOOL	m_WriteProtectDisc[2];
    int		m_MenuIdAMXSize;
    int		m_MenuIdAMXAdjust;
    int		m_AMXXSize;
    int		m_AMXYSize;
    int		m_AMXAdjust;
    BOOL	m_DirectDrawEnabled;
    int		m_DDFullScreenMode;
    bool	m_isFullScreen;
    bool	m_isDD32;

    HGDIOBJ 	m_hOldObj;
    HDC 	m_hDCBitmap;
    HGDIOBJ 	m_hBitmap;
    bmiData 	m_bmi;
    char	m_szTitle[100];

    int		m_ScreenRefreshCount;
    double	m_FramesPerSecond;

    int		m_MenuIdPrinterPort;
    char	m_PrinterFileName[_MAX_PATH];
    char	m_PrinterDevice[_MAX_PATH];

    // DirectX stuff
    BOOL			m_DXInit;
    LPDIRECTDRAW		m_DD;			// DirectDraw object
    LPDIRECTDRAW2		m_DD2;			// DirectDraw object
    LPDIRECTDRAWSURFACE		m_DDSPrimary;		// DD primary surface
    LPDIRECTDRAWSURFACE2	m_DDS2Primary;		// DD primary surface
    LPDIRECTDRAWSURFACE		m_DDSOne;		// Offscreen surface 1
    LPDIRECTDRAWSURFACE2	m_DDS2One;		// Offscreen surface 1
    LPDIRECTDRAWSURFACE		m_BackBuffer;	// Full Screen Back Buffer
    LPDIRECTDRAWSURFACE2	m_BackBuffer2;		// DD2 of the above
    BOOL			m_DDS2InVideoRAM;
    LPDIRECTDRAWCLIPPER		m_Clipper;		// clipper for primary

    BOOL InitClass(void);
    void UpdateOptiMenu(void);
    void CreateBeebWindow(void);
    void CreateBitmap(void);
    void InitMenu(void);
    void UpdateMonitorMenu();
    void UpdateSerialMenu(HMENU hMenu);
    void UpdateSFXMenu();

    void InitDirectX(void);
    HRESULT InitSurfaces(void);
    void ResetSurfaces(void);
    void GetRomMenu(void);		// LRW  Added for individual ROM/Ram
    void GreyRomMenu(BOOL SetToGrey);	// LRW	Added for individual ROM/Ram
    void TranslateWindowSize(void);
    void TranslateSampleRate(void);
    void TranslateVolume(void);
    void TranslateTiming(void);
    void TranslateKeyMapping(void);
    void ReadDisc(int Drive,HMENU dmenu);
    void LoadTape(void);
    void InitJoystick(void);
    void ResetJoystick(void);
    void RestoreState(void);
    void SaveState(void);
    void NewDiscImage(int Drive);
    void ToggleWriteProtect(int Drive);
    void SavePreferences(void);
    void SetWindowAttributes(bool wasFullScreen);
    void TranslateAMX(void);
    BOOL PrinterFile(void);
    void TogglePrinter(void);
    void TranslatePrinterPort(void);

}; /* BeebWin */

void SaveEmuUEF(FILE *SUEF);
void LoadEmuUEF(FILE *SUEF);
#endif

