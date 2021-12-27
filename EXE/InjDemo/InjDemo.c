/*************************************************
 * InjDemo - Demo program for Injection library. *
 *                                               *
 * (c) A. Miguel Feijao, 10/8/2005 - 1/11/2011   *
 *************************************************/

#pragma comment(lib, "comctl32.lib")

/*********************************************************
 * VS2010 compilation notes:                             *
 * - Include library Comctl32.lib                        *
 * - For Unicode build set UNICODE and _UNICODE symbols  *
 * - Basic Runtime Checks = Default                      *
 * - Buffer Security Check = No (/GS-)                   *
 *********************************************************/

#define   WIN32_LEAN_AND_MEAN
#define   _CRT_SECURE_NO_WARNINGS
#define   _CRT_NON_CONFORMING_SWPRINTFS

#include  <windows.h>
#include  <tchar.h>
#include  <stdio.h>
#include  <commctrl.h>
#include  <commdlg.h>
#include  <stdlib.h>
#include  <Shellapi.h>

#include  "..\..\DLL\InjLib\Inject.h"
#include  "Enum.h"
#include  "List.h"
#include  "resource.h"

#define GET_X_LPARAM(lp)  ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)  ((int)(short)HIWORD(lp))

// Process list
typedef struct _PROCLIST{
    struct  _PROCLIST *pNext;
    DWORD   dwPID;
    TCHAR   ProcessName[MAX_PATH];
} PROCLIST, *PPROCLIST;

// Subclass data
typedef struct {
    // Private
    RDATA           pv;             // Data needed for subclassing
    // Public
    UINT            WM_Private;     // Message send by remote wnd proc
    HWND            hDisplayWnd;    // Window that will receive WM_PRIVATE messages
    POSTMESSAGE     PostMessage;    // PostMessage() addr
} MYRDATA, *PMYRDATA;

// Subclass list
typedef struct _SUBCLASSLIST {
    struct _SUBCLASSLIST *pNext;
    DWORD   dwPID;
    MYRDATA myRD;
} SUBCLASSLIST, *PSUBCLASSLIST;

// Sort data struct
typedef struct _SORTDATA {
    int     Sort;                   // 0=sort by PID, 1=sort by Process name
    DWORD   dwPID;
    TCHAR   ProcessName[MAX_PATH];
} SORTDATA, *PSORTDATA;

// Message send by remote window procedure
#define     WMPRIVATE    TEXT("WM_PRIVATE")
#define     WM_PRIVATE   WM_APP + 100
UINT        WM_Private;

PPROCLIST     pProcList;      // ProcList
PSUBCLASSLIST pSubclassList;  // SubclassList
HWND          ghWndList;      // Listview control handle
HWND          ghWndEdit;      // Edit window handle
int           Sort = 0;       // Sort by column no.

// Used by ProcList compares
BOOL ProcListCmpFunc(PPROCLIST pList, DWORD PID)
{
    return (pList->dwPID == PID);
}

// Used by ProcList sorts
BOOL ProcListSort(PPROCLIST pList, PSORTDATA pData)
{
    // Sort ascending by PID
    if (pData->Sort == 0)
        return (pData->dwPID < pList->dwPID);
    // Sort ascending by Process name
    else
        return (_tcsicmp(pData->ProcessName, pList->ProcessName) < 0);
}

// Used by SubclassList compares
BOOL SubclassListCmpFunc(PSUBCLASSLIST pList, DWORD PID)
{
    return (pList->dwPID == PID);
}

// Remove closed processes from SubclassList
void SubclassListUpdate(PSUBCLASSLIST *pSubclassList, PPROCLIST pProcessList)
{
    PSUBCLASSLIST pSCL = *pSubclassList;
    PSUBCLASSLIST pAux;

    while (pSCL)
    {
        pAux = pSCL->pNext;
        if (!ListFind((PLIST)pProcessList, pSCL->dwPID, (CMPFUNC)ProcListCmpFunc))
            ListDelete((PLIST *)pSubclassList, pSCL->dwPID, (CMPFUNC)SubclassListCmpFunc);
        pSCL = pAux;
    }
}

// Update Listview control data from ProcList
BOOL ListViewUpdate(HWND hWndList, PPROCLIST pProcList)
{
    int         nItem, row;
    PPROCLIST   p;
    LVITEM      item;
    TCHAR       msgbuf[MAX_PATH];

    // No. of items in Listview control
    nItem = ListView_GetItemCount(hWndList);

    // Copy data from ProcessList to the Listview control
    // (until one of the two lists ends)
    for (row=0, p=pProcList; row < nItem && p != NULL; row++, p = p->pNext)
    {
        // Set lParam item to dwPID
        item.mask       = LVIF_PARAM;
        item.iItem      = row;
        item.iSubItem   = 0;
        item.lParam     = p->dwPID;
        ListView_SetItem(hWndList, &item);

        // Column 1
        if (p->dwPID >= 0x80000000)
            _stprintf(msgbuf, TEXT("0x%08X"), p->dwPID);
        else
            _stprintf(msgbuf, TEXT("%d"), p->dwPID);
        ListView_SetItemText(hWndList, row, 0, msgbuf);

        // Column 2
        _stprintf(msgbuf, TEXT("%s"), p->ProcessName);
        ListView_SetItemText(hWndList, row, 1, msgbuf);
    }

    // Listview control > ProcessList => Delete remaining Listview control items
    while (row < nItem)
    {
        ListView_DeleteItem(hWndList, row);
        row++;
    }

    // Listview control < ProcessList => Expand Listview control
    while (p)
    {
        // It's a new request. Put at the end.
        row = 0x7FFFFFFF;

        // Column 1
        if (p->dwPID >= 0x80000000)
            _stprintf(msgbuf, TEXT("0x%08X"), p->dwPID);
        else
            _stprintf(msgbuf, TEXT("%d"), p->dwPID);
        item.mask       = LVIF_TEXT | LVIF_PARAM;
        item.iItem      = row;
        item.iSubItem   = 0;
        item.lParam     = p->dwPID;
        item.pszText    = msgbuf;
        item.cchTextMax = lstrlen(item.pszText) + sizeof(TCHAR);
        row = ListView_InsertItem(hWndList, &item);
        if (row == -1)
            return FALSE;

        // Column 2
        _stprintf(msgbuf, TEXT("%s"), p->ProcessName);
        ListView_SetItemText(hWndList, row, 1, msgbuf);

        p = p->pNext;
    }

    return TRUE;
}

// Called by EnumProcesses() for each process
BOOL CALLBACK EnumProcessCallback(DWORD dwProcessId, LPCTSTR pszProcessName, PDWORD pdwPID, LPCTSTR pszName)
{
    PPROCLIST pNew;
    SORTDATA  sd;

    /*** Insert new data into ProcessList ***/
    pNew = (PPROCLIST)malloc(sizeof(PROCLIST));
    if (!pNew)
        return TRUE;

    pNew->dwPID = dwProcessId;
    _tcscpy(pNew->ProcessName, pszProcessName);
    pNew->pNext = pProcList;

    sd.Sort = Sort;
    sd.dwPID = dwProcessId;
    _tcscpy(sd.ProcessName, pszProcessName);
    ListInsertAndSort((PLIST *)&pProcList, (PLIST)pNew, (DWORD)&sd, (CMPFUNC)ProcListSort);

    return TRUE;
}

// Append a string message to the edit control.
void AddLine(TCHAR *s, int nCRLF)
{
    TCHAR Str[255];
    int   i;

    // Set caret to the end of the edit control.
    SendMessage(ghWndEdit, EM_SETSEL, 65534, 65534);

    // Copy string to buffer.
    _tcscpy(Str, s);

    // Add CR/LF
    for (i=0; i < nCRLF; i++)
        _tcscat(Str, TEXT("\r\n"));

    // Append the string to the edit control.
    SendMessage(ghWndEdit, EM_REPLACESEL, TRUE, (LPARAM)Str);
}

/******************************
 * Enable/Disable privilege.  *
 * Called with SE_DEBUG_NAME. *
 ******************************/
BOOL EnablePrivilege(LPCTSTR lpszPrivilegeName, BOOL bEnable)
{
    HANDLE              hToken;
    TOKEN_PRIVILEGES    tp;
    LUID                luid;
    BOOL                ret;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY | TOKEN_READ,
                          &hToken))
        return FALSE;

    if (!LookupPrivilegeValue(NULL, lpszPrivilegeName, &luid))
        return FALSE;

    tp.PrivilegeCount           = 1;
    tp.Privileges[0].Luid       = luid;
    tp.Privileges[0].Attributes = bEnable ? SE_PRIVILEGE_ENABLED : 0;

    ret = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);

    CloseHandle(hToken);

    return ret;
}

/////////////////////////////////////////////////////////////////////////////

typedef struct {
    DWORD PlatformId;
    DWORD MajorVersion;
    DWORD MinorVersion;
    DWORD BuildNumberLow;
    DWORD BuildNumberHigh;
    TCHAR *VersionString;
} VERSION;

VERSION v[] = {{1, 4, 0,   0,    950, TEXT("Windows 95")},
               {1, 4, 40, 310,   310, TEXT("Microsoft Plus! for Windows 95")},
               {1, 4, 0,  951,  1080, TEXT("Windows 95 SP1")},
               {1, 4, 0,  1081, 1111, TEXT("Windows 95 OSR2")},
               {1, 4, 3,  1214, 1214, TEXT("Windows 95 OSR2.1")},   
               {1, 4, 3,  1216, 1216, TEXT("Windows 95 OSR2.5")},
               {1, 4, 10, 1998, 1998, TEXT("Windows 98")},
               {1, 4, 10, 1999, 2182, TEXT("Windows 98 SP1")},
               {1, 4, 10, 2222, 2222, TEXT("Windows 98 SE")},
               {1, 4, 90, 2476, 2476, TEXT("Windows Me Beta")},
               {1, 4, 90, 3000, 3000, TEXT("Windows Me")},

               {2, 3, 10,  528, 528,  TEXT("Windows NT 3.1")},
               {2, 3, 50,  807, 807,  TEXT("Windows NT 3.5")},
               {2, 3, 51, 1057, 1057, TEXT("Windows NT 3.51")},
               {2, 4, 0,  1381, 1381, TEXT("Windows NT 4")},
               {2, 5, 0,  1515, 1515, TEXT("Windows NT 5.00 (Beta 2)")},
               {2, 5, 0,  2031, 2031, TEXT("Windows 2000 (Beta 3)")},
               {2, 5, 0,  2128, 2128, TEXT("Windows 2000 (Beta 3 RC2)")},
               {2, 5, 0,  2183, 2183, TEXT("Windows 2000 (Beta 3)")},
               {2, 5, 0,  2195, 2195, TEXT("Windows 2000")},
               {2, 5, 1,  2505, 2505, TEXT("Windows XP (RC 1)")},
               {2, 5, 1,  2600, 2600, TEXT("Windows XP")},
               {2, 5, 2,  3541, 3541, TEXT("Windows .NET Server interim")},
               {2, 5, 2,  3590, 3590, TEXT("Windows .NET Server Beta 3")},
               {2, 5, 2,  3660, 3660, TEXT("Windows .NET Server Release Candidate 1 (RC1)")},
               {2, 5, 2,  3718, 3718, TEXT("Windows .NET Server 2003 RC2")},
               {2, 5, 2,  3763, 3763, TEXT("Windows Server 2003 (Beta?)")},
               {2, 5, 2,  3790, 3790, TEXT("Windows 2003")},

               {2, 6, 0,  5048, 5048, TEXT("Windows Longhorn")},
               {2, 6, 0,  5112, 5112, TEXT("Windows Vista Beta 1")},
               {2, 6, 0,  5219, 5219, TEXT("Windows Vista Community Technology Preview 1 (CTP)")},
               {2, 6, 0,  5231, 5231, TEXT("Windows Vista CTP2")},
               {2, 6, 0,  5259, 5259, TEXT("Windows Vista TAP Preview")},
               {2, 6, 0,  5270, 5270, TEXT("Windows Vista CTP (Dezember)")},
               {2, 6, 0,  5308, 5308, TEXT("Windows Vista CTP (Februar)")},
               {2, 6, 0,  5342, 5342, TEXT("Windows Vista CTP Refresh")},
               {2, 6, 0,  5365, 5365, TEXT("Windows Vista April EWD")},
               {2, 6, 0,  5381, 5381, TEXT("Windows Vista, Beta 2 Preview")},
               {2, 6, 0,  5384, 5384, TEXT("Windows Vista Beta 2")},
               {2, 6, 0,  5456, 5536, TEXT("Windows Vista Pre-RC1")},
               {2, 6, 0,  5600, 5600, TEXT("Windows Vista Release Candidate 1 (RC1)")},
               {2, 6, 0,  5700, 5728, TEXT("Windows Vista Pre-RC2")},
               {2, 6, 0,  5744, 5744, TEXT("Windows Vista RC2")},
               {2, 6, 0,  5808, 5840, TEXT("Windows Vista Pre-RTM")},
               {2, 6, 0,  6000, 6000, TEXT("Windows Vista")},
               {2, 6, 0,  6001, 6001, TEXT("Windows Vista Service Pack 1 (SP1)")},
               {2, 6, 0,  6002, 6002, TEXT("Windows Vista Service Pack 2 (SP2)")},

               {2, 6, 1,  6519, 6519, TEXT("Windows 7 Milestone 1 (M1)")},
               {2, 6, 1,  6589, 6589, TEXT("Windows 7 Milestone 2 (M2)")},
               {2, 6, 1,  7000, 7000, TEXT("Windows 7 Beta 1")},
               {2, 6, 1,  7100, 7100, TEXT("Windows 7 Release Candidate 1 (RC1)")},
               {2, 6, 1,  7600, 7600, TEXT("Windows 7")},
               {2, 6, 1,  7601, 7601, TEXT("Windows 7 Service Pack 1 (SP1)")},

               {3, 1, 0, 0000, 9999, TEXT("Windows CE 1.0")},
               {3, 2, 0, 0000, 9999, TEXT("Windows CE 2.0")},
               {3, 2, 1, 0000, 9999, TEXT("Windows CE 2.1")},
               {3, 3, 0, 0000, 9999, TEXT("Windows CE 3.0")},
};

#define NELEMENTS sizeof(v) / sizeof(VERSION)

BOOL OSWin9x;

#define MAXSUBSYSTEM 16

TCHAR *szSubsystem[MAXSUBSYSTEM+1] = {
	                    TEXT("Unknown"),	               // 0 = Unknown subsystem.
                        TEXT("Native"),                    // 1 = Image doesn't require a subsystem.
                        TEXT("Windows GUI"),               // 2 = Image runs in the Windows GUI subsystem.
                        TEXT("Windows Console"),           // 3 = Image runs in the Windows character subsystem.
					    TEXT(""),                          //
                        TEXT("OS/2 CUI"),                  // 5 = Image runs in the OS/2 character subsystem.
					    TEXT(""),                          //
                        TEXT("POSIX CUI"),                 // 7 = Image runs in the Posix character subsystem.
                        TEXT("VXD (Win9x)"),               // 8 = Image is a native Win9x driver.
                        TEXT("WIN CE"),                    // 9 = Image runs in the Windows CE subsystem.
                        TEXT("EFI Application"),           // 10 = 
                        TEXT("EFI Boot Service Driver"),   // 11 = 
			            TEXT("EFI Runtime Driver"),        // 12 = 
			            TEXT("EFI ROM"),                   // 13 =
		            	TEXT("Xbox"),                      // 14 =
			            TEXT(""),                          //
			            TEXT("Windows Boot Application")}; // 16 =

TCHAR *szError[] = {TEXT("OK"),
                    TEXT("Error in remote code"),
                    TEXT("Invalid OS"),
                    TEXT("Exception"),
                    TEXT("Invalid Process"),
                    TEXT("Timeout"),
                    TEXT("OpenProcess() failed"),
                    TEXT("ReadProcessMemory() failed"),
                    TEXT("WriteProcessMemory() failed"),
                    TEXT("VirtualAllocEx() failed"),
                    TEXT("NtQueryInformationProcess() failed"),
                    TEXT("CreateThread() failed"),
                    TEXT("RtlCreateUserThread() failed"),
                    TEXT("Invalid NT header"),
                    TEXT("IsCodeSafe() returned FALSE"),
                    TEXT("IsThreadId() returned FALSE"),
                    TEXT("GetPDB() failed"),
                    TEXT("Invalid ThreadList"),
                    TEXT("LoadLibrary() failed"),
                    TEXT("GetProcAddress() failed"),
                    TEXT("Invalid parameter"),
                    TEXT("Could not patch ASM code")};

// Display data returned by GetProcessInfo()
void DisplayProcessInfo(DWORD ProcessFlags, LPCTSTR pszProcessName, DWORD PID)
{
    TCHAR   szMsg[255];
    TCHAR   szPID[20];
    int     Subsystem;
    int     error;

    AddLine(TEXT(""), 1);
    if (OSWin9x)
        _stprintf(szPID, TEXT("%08X"), PID);
    else
        _stprintf(szPID, TEXT("%d"), PID);
    _stprintf(szMsg, TEXT("Process Info (Name=\"%s\", PID=%s): "), pszProcessName, szPID);
    AddLine(szMsg, 0);

    if (LOWORD(ProcessFlags) == (WORD)-1)
    {
        error = HIWORD(ProcessFlags);
        _stprintf(szMsg, TEXT("%d (%s)"), error, error <= ERROR_MAX ? szError[error] : TEXT("???"));
        AddLine(szMsg, 1);
    }
    else
    {
        AddLine(TEXT(""), 1);
        Subsystem = HIWORD(ProcessFlags);
        _stprintf(szMsg, TEXT("    Subsystem (%d) = %s"), Subsystem, Subsystem <= MAXSUBSYSTEM ? szSubsystem[Subsystem] : TEXT("???"));
        AddLine(szMsg, 1);
        _stprintf(szMsg, TEXT("    Win9x - %s"), ProcessFlags & fWIN9X ? TEXT("Yes") : TEXT("No"));
        AddLine(szMsg, 1);
        _stprintf(szMsg, TEXT("    WinNT - %s"), ProcessFlags & fWINNT ? TEXT("Yes") : TEXT("No"));
        AddLine(szMsg, 1);
        _stprintf(szMsg, TEXT("    Invalid - %s"), ProcessFlags & fINVALID ? TEXT("Yes") : TEXT("No"));
        AddLine(szMsg, 1);
        _stprintf(szMsg, TEXT("    Debugged - %s"), ProcessFlags & fDEBUGGED ? TEXT("Yes") : TEXT("No"));
        AddLine(szMsg, 1);
        _stprintf(szMsg, TEXT("    Initialized - %s"), ProcessFlags & fNOTINITIALIZED ? TEXT("No") : TEXT("Yes"));
        AddLine(szMsg, 1);
        _stprintf(szMsg, TEXT("    Protected - %s"), ProcessFlags & fPROTECTED ? TEXT("Yes") : TEXT("No"));
        AddLine(szMsg, 1);
    }
    AddLine(TEXT(""), 1);
}

// Display window version
BOOL DisplayOSVersion()
{
    OSVERSIONINFO   osvi;
    TCHAR           szMsg[255];
    TCHAR           szCSDVersion[128];
    DWORD           OSPlatformId, OSMajorVersion, OSMinorVersion, OSBuildVersion;
    BOOL            Found;
    int             i;

    // Get Windows version
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    if (!GetVersionEx(&osvi))
        return FALSE;

    // Save version data
    OSMajorVersion = osvi.dwMajorVersion;
    OSMinorVersion = osvi.dwMinorVersion;
    OSPlatformId = osvi.dwPlatformId;
    OSBuildVersion = LOWORD(osvi.dwBuildNumber);
    _tcscpy(szCSDVersion, osvi.szCSDVersion);
    OSWin9x = osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS;

    Found = FALSE;
    for (i=0; i < NELEMENTS; i++)
    {
        if (OSPlatformId == v[i].PlatformId &&
            OSMajorVersion == v[i].MajorVersion &&
            OSMinorVersion == v[i].MinorVersion &&
            OSBuildVersion >= v[i].BuildNumberLow &&
            OSBuildVersion <= v[i].BuildNumberHigh)
        {
            _stprintf(szMsg, TEXT("%s (%d.%d.%d %s)"), v[i].VersionString, OSMajorVersion, OSMinorVersion, OSBuildVersion, szCSDVersion);
            AddLine(szMsg, 1);
            Found = TRUE;
            break;
        }
    }

    if (!Found)
    {
        _stprintf(szMsg, TEXT("Unknown (%d.%d.%d %s)"), OSMajorVersion, OSMinorVersion, OSBuildVersion, szCSDVersion);
        AddLine(szMsg, 1);
    }

    AddLine(TEXT(""), 1);
    return TRUE;
}

///////////////////////////////////////////////////////////////////

// Used to pass/return data to/from EnumWindows function
typedef struct _ENUMPARAM {
   HWND     hWnd;
   DWORD    PID;
} ENUMPARAM, *PENUMPARAM;

/***********************************
 * Enum windows callback function. *
 ***********************************/
BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM ep)
{
    DWORD  dwPID;

    GetWindowThreadProcessId(hWnd, &dwPID);

    // Does PID match ?
    if (((PENUMPARAM)ep)->PID == dwPID)
    {
        ((PENUMPARAM)ep)->hWnd = hWnd;
        return FALSE;
    }

    return TRUE;
}

/******************************************
 * Return handle of main window from PID. *
 ******************************************/
HWND GetWinHandle(DWORD PID)
{
    ENUMPARAM ep;

    ep.hWnd = NULL;
    ep.PID = PID;
    EnumWindows(EnumWindowsProc, (LPARAM)&ep);

    return ep.hWnd;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

typedef struct {
    int     Result;     // Result from remote thread
} PARAMBLOCK, *PPARAMBLOCK;

/************************************
 * Test thread for RemoteExecute(). *
 ************************************/
#pragma check_stack(off)
static DWORD WINAPI RemoteThread(PPARAMBLOCK pData)
{
    __asm__(
        "mov $0, %eax\n\t"
    );
//    __asm mov [eax], eax

    // Return value
    pData->Result = 123;
    return 456;
}


/****************************
 * Remote window procedure. *
 ****************************/
#pragma check_stack(off)
static LRESULT WINAPI MyWndProcHandler(PMYRDATA pData, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
    case WM_MOUSEMOVE:
         pData->PostMessage(pData->hDisplayWnd, pData->WM_Private, wParam, lParam);
         break;
    }

    // Let original handler process the messages
    return FALSE;
}


/**************************************************
 * Callback function that handles the messages    *
 * for the Dialog (Main) window.                  *
 **************************************************/
BOOL CALLBACK DlgProc(HWND hDialog, UINT Message, WPARAM wParam, LPARAM lParam)
{
    #define     TIMER_ID        1
    #define     TIMER_REFRESH   1000    // 1 second

    #define     DLL             TEXT("Dll.Dll")
    #define     BROWSEFILTER    TEXT("Dll files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0\0")
    #define     TITLE           TEXT("Select DLL to inject")

    HWND          hWndList;
    LVCOLUMN      col;
    TCHAR         s[255];
    LPNMHDR       lpnm;
    int           currentItem;
    RECT          rec;
    int           width, height;
    int           ColWidth1, ColWidth2;
    static BOOL   fTimer = FALSE;

    LVITEM        item;
    DWORD         PID;
    DWORD         ProcessFlags;
    HANDLE        hProcess;
    PARAMBLOCK    MyData;
    DWORD         dwExitCode;
    int           rc;

    int           r;
    OPENFILENAME  ofn;
    static TCHAR  szFilename[MAX_PATH];
    TCHAR         szTitle[100];
    int           xPos, yPos;
    HWND          hRemoteWnd;
    PSUBCLASSLIST pSC;
    OSVERSIONINFO osvi;
    LPNMLISTVIEW  pnmv;

    static HMODULE  hInjLib;

    // Injection library exported functions
    static GETPROCESSINFO      GetProcessInfo ;
    static REMOTEEXECUTE       RemoteExecute ;
    static INJECTDLL           InjectDll;
    static EJECTDLL            EjectDll;
    static STARTREMOTESUBCLASS StartRemoteSubclass;
    static STOPREMOTESUBCLASS  StopRemoteSubclass;

    // Private message send by subclassed window proc.
    if (Message == WM_Private)
    {
         xPos = GET_X_LPARAM(lParam);
         yPos = GET_Y_LPARAM(lParam);
         _stprintf(s, TEXT("Mouse pos = (%d,%d)"), xPos, yPos);
         AddLine(s, 1);
    }

    switch (Message)
    {
    case WM_INITDIALOG:
         // Get Windows version
         osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
         if (!GetVersionEx(&osvi))
            return FALSE;

         OSWin9x = osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS;

         // Center dialog window on screen
         width = GetSystemMetrics(SM_CXSCREEN);
         height = GetSystemMetrics(SM_CYSCREEN);
         GetWindowRect(hDialog, &rec);
         MoveWindow(hDialog,
                    (width - (rec.right - rec.left))/2,
                    (height - (rec.bottom-rec.top))/2,
                     rec.right-rec.left,
                     rec.bottom-rec.top,
                     FALSE);

         // Force the common controls DLL to be loaded
         InitCommonControls();

         // ListView control handle
         hWndList = GetDlgItem(hDialog, IDC_LIST);
         ghWndList = hWndList;

         // Edit control handle
         ghWndEdit = GetDlgItem(hDialog, IDC_EDIT);

         // Get the size of List View Control
         GetClientRect(hWndList, &rec);

         // Width of columns
         ColWidth1 = (int)(rec.right / (OSWin9x ? 5 : 10));
         ColWidth2 = rec.right - ColWidth1;

         col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
         col.fmt  = LVCFMT_LEFT;

         // Create column 1
         col.iSubItem   = 1;
         col.cx         = ColWidth1;
         col.pszText    = TEXT("PID");
         if (ListView_InsertColumn(hWndList, 0, &col) == -1)
            return FALSE;

         // Create column 2
         col.iSubItem   = 2;
         col.cx         = ColWidth2;
         col.pszText    = TEXT("Process Name");
         if (ListView_InsertColumn(hWndList, 1, &col) == -1)
            return FALSE;

         // Set full-row selection
         ListView_SetExtendedListViewStyle(hWndList, LVS_EX_FULLROWSELECT | LVS_EX_ONECLICKACTIVATE);

         // Default radiobutton selection
         CheckRadioButton(hDialog, IDC_INJCODE, IDC_INJDLL, IDC_INJCODE);
         CheckRadioButton(hDialog, IDC_INJECT, IDC_EJECT, IDC_INJECT);
         CheckRadioButton(hDialog, IDC_START, IDC_STOP, IDC_START);
         EnableWindow(GetDlgItem(hDialog, IDC_DLLTYPE), FALSE);
         EnableWindow(GetDlgItem(hDialog, IDC_INJECT), FALSE);
         EnableWindow(GetDlgItem(hDialog, IDC_EJECT), FALSE);
         EnableWindow(GetDlgItem(hDialog, IDC_SUBCTYPE), FALSE);
         EnableWindow(GetDlgItem(hDialog, IDC_START), FALSE);
         EnableWindow(GetDlgItem(hDialog, IDC_STOP), FALSE);

         // Message send by remote subclassed window proc handler
         WM_Private = RegisterWindowMessage(WMPRIVATE);
         if (!WM_Private)
             WM_Private = WM_PRIVATE;

         // Load "InjLib.dll" library
         if (!(hInjLib = LoadLibrary(TEXT("InjLib.dll"))))
         {
             _stprintf(s, TEXT("LoadLibrary()/Initialization() failed."));
             AddLine(s, 1);
             return TRUE;
         }

         // Load DLL exported functions
         GetProcessInfo = (GETPROCESSINFO)GetProcAddress(hInjLib, "GetProcessInfo");
         RemoteExecute = (REMOTEEXECUTE)GetProcAddress(hInjLib, "RemoteExecute");
#ifdef UNICODE
         InjectDll = (INJECTDLL)GetProcAddress(hInjLib, "InjectDllW");
         EjectDll = (EJECTDLL)GetProcAddress(hInjLib, "EjectDllW");
#else
         InjectDll = (INJECTDLL)GetProcAddress(hInjLib, "InjectDllA");
         EjectDll = (EJECTDLL)GetProcAddress(hInjLib, "EjectDllA");
#endif
         StartRemoteSubclass = (STARTREMOTESUBCLASS)GetProcAddress(hInjLib, "StartRemoteSubclass");
         StopRemoteSubclass = (STOPREMOTESUBCLASS)GetProcAddress(hInjLib, "StopRemoteSubclass");

         if (!GetProcessInfo ||
             !RemoteExecute ||
             !InjectDll || !EjectDll ||
             !StartRemoteSubclass || !StopRemoteSubclass)
         {
             _stprintf(s, TEXT("Failed to load all exported functions."));
             AddLine(s, 1);
             return TRUE;
         }

         // Display OS version
		 DisplayOSVersion();

         // Create timer that updates process list
         SetTimer(hDialog, TIMER_ID, TIMER_REFRESH, NULL);
         PostMessage(hDialog, WM_TIMER, TIMER_ID, 0);

         return TRUE;

        // Update process list
        case WM_TIMER:
             if (!fTimer)
             {
                 fTimer = TRUE;
                 // Update Listview contents
                 _EnumProcesses(EnumProcessCallback, NULL, NULL);
                 SubclassListUpdate(&pSubclassList, pProcList);
                 ListViewUpdate(ghWndList, pProcList);
                 ListDeleteAll((PLIST *)&pProcList);
                 fTimer = FALSE;
             }
             break;

    // ListView control notifications
    case WM_NOTIFY:

         lpnm = (LPNMHDR)lParam;

         // Header column clicked (sort by column)
         if ((lpnm->idFrom == IDC_LIST) && (lpnm->code == LVN_COLUMNCLICK))
         {
             pnmv = (LPNMLISTVIEW)lParam;
             Sort = pnmv->iSubItem;
             PostMessage(hDialog, WM_TIMER, TIMER_ID, 0);
             return TRUE;
         }

         // Row selected
         if ((lpnm->idFrom == IDC_LIST) && (lpnm->code == LVN_ITEMACTIVATE))
         {
             hWndList = GetDlgItem(hDialog, IDC_LIST);

             // Get position of the selected item
             currentItem = ListView_GetSelectionMark(hWndList);
             if (currentItem == -1)
             {
                 _stprintf(s, TEXT("%s"), TEXT("No item selected."));
                 AddLine(s, 1);
                 return TRUE;
             }

             // Get Process name
             memset(s, 0, sizeof(s));
             ListView_GetItemText(hWndList, currentItem, 1, s, sizeof(s));

             // Get PID
             item.mask      = LVIF_PARAM;
             item.iItem     = currentItem;
             item.iSubItem  = 0;
             ListView_GetItem(hWndList, &item);
             PID = item.lParam;

             // Display process info
             ProcessFlags = GetProcessInfo(PID);
             DisplayProcessInfo(ProcessFlags, s, PID);

             // Open selected process
             if (!(hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID)))
                 return TRUE;

             /*** Subclass ***/
             if (IsDlgButtonChecked(hDialog, IDC_SUBCLASS) == BST_CHECKED)
             {
                 // Subclass only allowed in GUI processes
                 if (HIWORD(ProcessFlags) != IMAGE_SUBSYSTEM_WINDOWS_GUI)
                 {
                     _stprintf(s, TEXT("Subclass only allowed in GUI processes."));
                     AddLine(s, 1);
                     CloseHandle(hProcess);
                     return TRUE;
                 }

                 /*** Start subclass ***/
                 if (IsDlgButtonChecked(hDialog, IDC_START) == BST_CHECKED)
                 {
                     // Get process main window
                     hRemoteWnd = GetWinHandle(PID);
                     if (!hRemoteWnd)
                     {
                         _stprintf(s, TEXT("Could not locate remote window !"));
                         AddLine(s, 1);
                         CloseHandle(hProcess);
                         return TRUE;
                     }

                     // Check if process already subclassed
                     pSC = (PSUBCLASSLIST)ListFind((PLIST)pSubclassList, PID, (CMPFUNC)SubclassListCmpFunc);
                     if (pSC)
                     {
                         _stprintf(s, TEXT("Process already subclassed !"));
                         AddLine(s, 1);
                         CloseHandle(hProcess);
                         return TRUE;
                     }

                     // Not found, set data for Subclass
                     if (!(pSC = malloc(sizeof(SUBCLASSLIST))))
                     {
                         _stprintf(s, TEXT("malloc() failed !"));
                         AddLine(s, 1);
                         CloseHandle(hProcess);
                         return TRUE;
                     }

                     memset(pSC, 0, sizeof(SUBCLASSLIST));
                     pSC->dwPID = PID;
                     pSC->myRD.pv.Size = sizeof(MYRDATA);
                     pSC->myRD.pv.hProcess = hProcess;
                     pSC->myRD.pv.ProcessFlags = ProcessFlags;
                     pSC->myRD.pv.dwTimeout = INFINITE;
                     pSC->myRD.pv.hWnd = hRemoteWnd;
                     pSC->myRD.hDisplayWnd = hDialog;
                     pSC->myRD.WM_Private = WM_Private;

#ifdef UNICODE
                     pSC->myRD.PostMessage = (POSTMESSAGE)GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "PostMessageW");
#else
                     pSC->myRD.PostMessage = (POSTMESSAGE)GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "PostMessageA");
#endif

                     if (!pSC->myRD.PostMessage)
                     {
                         _stprintf(s, TEXT("GetProcAddress() failed !"));
                         AddLine(s, 1);
                         free(pSC);
                         CloseHandle(hProcess);
                         return TRUE;
                     }

                     // Subclass the window
                     r = StartRemoteSubclass((PRDATA)&pSC->myRD, MyWndProcHandler);

                     // If OK add data into SubclassList
                     if (r == 0)
                         ListInsert((PLIST *)&pSubclassList, (PLIST)pSC);
                     // If ERROR free memory allocated for Subclass
                     else
                         free(pSC);

                     if (r & LOCAL_EXCEPTION)
                     {
                         _stprintf(s, TEXT("Exception in StartRemoteSubclass(): %X"), r & ~LOCAL_EXCEPTION);
                         AddLine(s, 1);
                     }
                     else
                     {
                         _stprintf(s, TEXT("StartRemoteSubclass(hWnd=%X): %d (%s)"), hRemoteWnd, r, r <= ERROR_MAX ? szError[r] : TEXT("???"));
                         AddLine(s, 1);
                     }
                 }

                 /*** Stop subclass ***/
                 if (IsDlgButtonChecked(hDialog, IDC_STOP) == BST_CHECKED)
                 {
                     // Check if process subclassed
                     pSC = (PSUBCLASSLIST)ListFind((PLIST)pSubclassList, PID, (CMPFUNC)SubclassListCmpFunc);
                     if (!pSC)
                     {
                         _stprintf(s, TEXT("Process not subclassed !"));
                         AddLine(s, 1);
                         CloseHandle(hProcess);
                         return TRUE;
                     }

                     pSC->myRD.pv.hProcess = hProcess;
                     pSC->myRD.pv.ProcessFlags = ProcessFlags;

                     // Restore original window handler
                     r = StopRemoteSubclass((PRDATA)&pSC->myRD);

                     hRemoteWnd = pSC->myRD.pv.hWnd;

                     // If OK delete data from SubclassList
                     if (r == 0)
                         ListDelete((PLIST *)&pSubclassList, PID, (CMPFUNC)SubclassListCmpFunc);

                     if (r & LOCAL_EXCEPTION)
                     {
                         _stprintf(s, TEXT("Exception in StopRemoteSubclass(): %X"), r & ~LOCAL_EXCEPTION);
                         AddLine(s, 1);
                     }
                     else
                     {
                         _stprintf(s, TEXT("StopRemoteSubclass(hWnd=%X): %d (%s)"), hRemoteWnd, r, r <= ERROR_MAX ? szError[r] : TEXT("???"));
                         AddLine(s, 1);
                     }
                 }
             }

             /*** Dll Injection/Ejection ***/
             if (IsDlgButtonChecked(hDialog, IDC_INJDLL) == BST_CHECKED)
             {
                 /*** Inject Dll ***/
                 if (IsDlgButtonChecked(hDialog, IDC_INJECT) == BST_CHECKED)
                 {
                     // DLL name required
                     while (_tcslen(szFilename) == 0)
                        SendMessage(hDialog, WM_COMMAND, IDC_BROWSE, (LPARAM)GetDlgItem(hDialog, IDC_BROWSE));

                     r = InjectDll(hProcess, ProcessFlags, szFilename, INFINITE, NULL);

                     if (r & LOCAL_EXCEPTION)
                     {
                         _stprintf(s, TEXT("Exception in InjectDll(): %X"), r & ~LOCAL_EXCEPTION);
                         AddLine(s, 1);
                     }
                     else
                     {
                         _stprintf(s, TEXT("InjectDll(): %d (%s)"), r, r <= ERROR_MAX ? szError[r] : TEXT("???"));
                         AddLine(s, 1);
                     }
                 }

                 /*** Eject Dll ***/
                 if (IsDlgButtonChecked(hDialog, IDC_EJECT) == BST_CHECKED)
                 {
                     // DLL name required
                     while (_tcslen(szFilename) == 0)
                        SendMessage(hDialog, WM_COMMAND, IDC_BROWSE, (LPARAM)GetDlgItem(hDialog, IDC_BROWSE));

                     r = EjectDll(hProcess, ProcessFlags, szFilename, NULL, INFINITE);

                     if (r & LOCAL_EXCEPTION)
                     {
                         _stprintf(s, TEXT("Exception in EjectDll(): %X"), r & ~LOCAL_EXCEPTION);
                         AddLine(s, 1);
                     }
                     else
                     {
                         _stprintf(s, TEXT("EjectDll(): %d (%s)"), r, r <= ERROR_MAX ? szError[r] : TEXT("???"));
                         AddLine(s, 1);
                     }
                 }
             }

             /*** Code Injection ***/
             if (IsDlgButtonChecked(hDialog, IDC_INJCODE) == BST_CHECKED)
             {
                 MyData.Result = -1;

                 // Execute code in remote process
                 if ((rc = RemoteExecute(hProcess, ProcessFlags, RemoteThread, &MyData, sizeof(MyData), INFINITE, &dwExitCode)) == -1)
                 {
                     _stprintf(s, TEXT("RemoteExecute() returned error %d"), GetLastError());
                     AddLine(s, 1);
                     return TRUE;
                 }

                 AddLine(TEXT(""), 1);

                 if (rc & REMOTE_EXCEPTION)
                 {
                     _stprintf(s, TEXT("Exception in remote code: %X"), rc & ~REMOTE_EXCEPTION);
                     AddLine(s, 1);
                 }
                 else if (rc & LOCAL_EXCEPTION)
                 {
                     _stprintf(s, TEXT("Exception in RemoteExecute(): %X"), rc & ~LOCAL_EXCEPTION);
                     AddLine(s, 1);
                 }
                 else
                 {
                     _stprintf(s, TEXT("RemoteExecute(hProcess=%X): %d (%s)"), hProcess, rc, rc <= ERROR_MAX ? szError[rc] : TEXT("???"));
                     AddLine(s, 1);
                     _stprintf(s, TEXT("Remote code returned: %d"), dwExitCode);
                     AddLine(s, 1);
                     _stprintf(s, TEXT("RDATA.Result: %d"), MyData.Result);
                     AddLine(s, 1);
                 }
             }

             CloseHandle(hProcess);

             return TRUE;
         }
         break;


    case WM_COMMAND:
         switch(LOWORD(wParam))
         {
         // OK button pressed
         case IDOK:
              KillTimer(hDialog, TIMER_ID);

              // Close window.
              EndDialog(hDialog, 0);
              return TRUE;

         case IDC_INJCODE:
              EnableWindow(GetDlgItem(hDialog, IDC_DLLTYPE), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_INJECT), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_EJECT), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_SUBCTYPE), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_START), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_STOP), FALSE);
              return TRUE;

         case IDC_INJDLL:
              EnableWindow(GetDlgItem(hDialog, IDC_DLLTYPE), TRUE);
              EnableWindow(GetDlgItem(hDialog, IDC_INJECT), TRUE);
              EnableWindow(GetDlgItem(hDialog, IDC_EJECT), TRUE);
              EnableWindow(GetDlgItem(hDialog, IDC_SUBCTYPE), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_START), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_STOP), FALSE);
              return TRUE;

         case IDC_SUBCLASS:
              EnableWindow(GetDlgItem(hDialog, IDC_DLLTYPE), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_INJECT), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_EJECT), FALSE);
              EnableWindow(GetDlgItem(hDialog, IDC_SUBCTYPE), TRUE);
              EnableWindow(GetDlgItem(hDialog, IDC_START), TRUE);
              EnableWindow(GetDlgItem(hDialog, IDC_STOP), TRUE);
              return TRUE;

         // Browse File
         case IDC_BROWSE:
              // Initialize fields in the OPENFILENAME struct.
              ZeroMemory(&ofn, sizeof(ofn));
              _tcscpy(szFilename, DLL);
              ofn.lStructSize = sizeof(ofn);
              ofn.hwndOwner = hDialog;
              ofn.lpstrFilter = BROWSEFILTER;
              ofn.lpstrFile = szFilename;
              ofn.nMaxFile = MAX_PATH;
              _tcscpy(szTitle, TITLE);
              ofn.lpstrTitle = szTitle;
              ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NODEREFERENCELINKS;
              ofn.lpstrDefExt = TEXT("dll");

              // Call OpenFileName Dialog.
              if (GetOpenFileName(&ofn)) // Open button pressed ?
                  SetWindowText(GetDlgItem(hDialog, IDC_DLLPATH), szFilename);
              else
                  *szFilename = TEXT('\0');
         }
         break;
    }

    return FALSE;
}


//#define ARRAYSIZE(array) (sizeof(array)/sizeof(*(array)))

WINBASEAPI BOOL WINAPI CheckTokenMembership(HANDLE,PSID,PBOOL);

/************************************************************************ 
 * Routine Description: This routine returns TRUE if the caller's       *
 * process is a member of the Administrators local group. Caller is NOT *
 * expected to be impersonating anyone and is expected to be able to    *
 * open its own process and process token.                              *
 * Arguments: None.                                                     *
 * Return Value:                                                        *
 *     TRUE - Caller has Administrators local group.                    *
 *     FALSE - Caller does not have Administrators local group.         *
 ************************************************************************/ 
BOOL IsUserAdmin(VOID)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup; 
	BOOL b;
    
	b = AllocateAndInitializeSid(&NtAuthority,
                                 2,
                                 SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0,
                                 &AdministratorsGroup); 
    if (b) 
	{
		if (!CheckTokenMembership(NULL, AdministratorsGroup, &b)) 
		{
			b = FALSE;
		} 
        FreeSid(AdministratorsGroup); 
	}

    return(b);
}

#define  APP_TITLE  TEXT("Inject Demo")

/*****************************************
 * Program entry point.                  *
 *****************************************/
int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine,
                   int nCmdShow)
{
    OSVERSIONINFO    osvi;
	HANDLE           hToken;
    TOKEN_ELEVATION  elevation;
	DWORD            infoLen;
	TCHAR            szPath[MAX_PATH];
	SHELLEXECUTEINFO sei = { sizeof(sei) };
	BOOL             Elevated;

    // Get Windows version
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    if (!GetVersionEx(&osvi))
	{
		MessageBox(NULL, TEXT("GetVersionEx() failed!"), APP_TITLE, MB_OK);
        return -1;
	}

	// XP required
	if (osvi.dwMajorVersion < 5)
	{
		MessageBox(NULL, TEXT("Windows XP or above required!"), APP_TITLE, MB_OK);
		return -1;
	}

	Elevated = FALSE;

    // XP
    if (osvi.dwMajorVersion == 5)
	{
		if (IsUserAdmin())
			Elevated = TRUE;
	}
	// Vista
	else if (osvi.dwMajorVersion >= 6)
	{
		OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &hToken);
        GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &infoLen);
		CloseHandle(hToken);

	    if (elevation.TokenIsElevated != 0)
			Elevated = TRUE;
	}

	if (!Elevated)
	{
		// Elevate the process.
		if (GetModuleFileName(NULL, szPath, ARRAYSIZE(szPath)))
		{
			// Launch itself as administrator.
			sei.lpVerb = TEXT("runas");
			sei.lpFile = szPath;
			sei.hwnd = NULL;
			sei.nShow = SW_NORMAL;

			Sleep(1000);
			if (!ShellExecuteEx(&sei))
			{
				MessageBox(NULL, TEXT("ShellExecute() failed!"), APP_TITLE, MB_OK);
				return -1;
			}
		}
		return 0;
	}

    EnablePrivilege(SE_DEBUG_NAME, TRUE);

    // Call Dialog Window
    if(DialogBox(hInstance, TEXT("Inject"), NULL, DlgProc) != IDOK){
        MessageBox(NULL, TEXT("DialogBox() failed!"), APP_TITLE, MB_OK);
    }

    EnablePrivilege(SE_DEBUG_NAME, FALSE);

    return 0;
}
