/*******************************************************************
 * Enum.c                                                          *
 * Process enumeration library for Win9x and WinNT.                * 
 *                                                                 *
 * (c) A. Miguel Feijao, 25/7/2005                                 *
 *******************************************************************/

#include  <windows.h>
#include  <tchar.h>
#include  <tlhelp32.h>

#include  "Enum.h"

#ifndef offsetof
#   define offsetof(a, b)   ((size_t)&(((a *)0)->b))
#endif

/********************************************************
 * EnumProcessesToolHelp()                              *
 * Enumerates all processes using the Toolhelp library. *
 ********************************************************/
BOOL WINAPI EnumProcessesToolHelp(PFNENUMPROC pfnEnumProc, LPCTSTR pszName, PDWORD pdwPID)
{
    typedef HANDLE (WINAPI *CREATESNAPSHOT) (DWORD, DWORD);
    typedef BOOL   (WINAPI *PROCESSWALK)    (HANDLE, LPPROCESSENTRY32);

    HINSTANCE       hKernel;
    CREATESNAPSHOT  CreateToolhelp32Snapshot;
    PROCESSWALK     Process32First;
    PROCESSWALK     Process32Next;
    HANDLE          hSnapshot;
    PROCESSENTRY32  pe32;
    DWORD           dwPID;
    LPTSTR          pszProcessName;

    // Callback function must be defined
    if (!pfnEnumProc)
        return FALSE;

    // Initialize PID to -1
    if (pdwPID)
        *pdwPID = -1;

    // Get kernel module handle
    if (!(hKernel = GetModuleHandle(TEXT("Kernel32.dll"))))
        return FALSE;

    // We must link to these functions explicitly.
    // Otherwise it will fail on some versions of Windows NT
    // which doesn't have Toolhelp functions defined in Kernel32.
    CreateToolhelp32Snapshot = (CREATESNAPSHOT) GetProcAddress(hKernel, "CreateToolhelp32Snapshot");
#ifdef UNICODE
    Process32First = (PROCESSWALK) GetProcAddress(hKernel, "Process32FirstW");
    Process32Next = (PROCESSWALK) GetProcAddress(hKernel, "Process32NextW");
#else
    Process32First = (PROCESSWALK) GetProcAddress(hKernel, "Process32First");
    Process32Next = (PROCESSWALK) GetProcAddress(hKernel, "Process32Next");
#endif

    // Error
    if (!CreateToolhelp32Snapshot || !Process32First || !Process32Next)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    // Create a snapshot
    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return FALSE;

    // First process
    pe32.dwSize = sizeof(pe32);
    if (!Process32First(hSnapshot, &pe32))
    {
        CloseHandle(hSnapshot);
        return FALSE;
    }

    // Walk through all processes
    do
    {
        if (pe32.dwSize > offsetof(PROCESSENTRY32, szExeFile))
        {
            // Get process name (without path)
            pszProcessName = _tcsrchr(pe32.szExeFile, TEXT('\\'));
            if (pszProcessName)
                pszProcessName++;
            else
                pszProcessName = pe32.szExeFile;
            dwPID = pe32.th32ProcessID;
        }
        else
        {
            pszProcessName = NULL;
            dwPID = -1;
        }

        // Call callback function
        // If returned FALSE stop enumeration
        if (!pfnEnumProc(dwPID, pszProcessName, pdwPID, pszName))
            break;

        pe32.dwSize = sizeof(pe32);
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);

    return TRUE;
}


/**************************************************************
 * EnumProcessesNtQuerySystemInformation()                    *
 * Enumerates all processes using NtQuerySystemInformation(). *
 **************************************************************/
BOOL WINAPI EnumProcessesNtQuerySystemInformation(PFNENUMPROC pfnEnumProc, LPCTSTR pszName, PDWORD pdwPID)
{
    // Some definitions from NTDDK and other sources

    typedef LONG    NTSTATUS;
    typedef LONG    KPRIORITY;

    #define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
    #define STATUS_INFO_LENGTH_MISMATCH    ((NTSTATUS)0xC0000004L)
    #define SystemProcessesAndThreadsInformation    5

    typedef struct _CLIENT_ID {
        DWORD       UniqueProcess;
        DWORD       UniqueThread;
    } CLIENT_ID;

    typedef struct UNICODE_STRING {
        USHORT      Length;
        USHORT      MaximumLength;
        PWSTR       Buffer;
    } UNICODE_STRING;

    typedef struct _VM_COUNTERS {
        SIZE_T      PeakVirtualSize;
        SIZE_T      VirtualSize;
        ULONG       PageFaultCount;
        SIZE_T      PeakWorkingSetSize;
        SIZE_T      WorkingSetSize;
        SIZE_T      QuotaPeakPagedPoolUsage;
        SIZE_T      QuotaPagedPoolUsage;
        SIZE_T      QuotaPeakNonPagedPoolUsage;
        SIZE_T      QuotaNonPagedPoolUsage;
        SIZE_T      PagefileUsage;
        SIZE_T      PeakPagefileUsage;
    } VM_COUNTERS;

    typedef struct _SYSTEM_THREADS {
        LARGE_INTEGER   KernelTime;
        LARGE_INTEGER   UserTime;
        LARGE_INTEGER   CreateTime;
        ULONG           WaitTime;
        PVOID           StartAddress;
        CLIENT_ID       ClientId;
        KPRIORITY       Priority;
        KPRIORITY       BasePriority;
        ULONG           ConTEXTSwitchCount;
        LONG            State;
        LONG            WaitReason;
    } SYSTEM_THREADS, * PSYSTEM_THREADS;

    // NOTE: SYSTEM_PROCESSES structure is different on NT 4 and Win2K
    typedef struct _SYSTEM_PROCESSES {
        ULONG           NextEntryDelta;
        ULONG           ThreadCount;
        ULONG           Reserved1[6];
        LARGE_INTEGER   CreateTime;
        LARGE_INTEGER   UserTime;
        LARGE_INTEGER   KernelTime;
        UNICODE_STRING  ProcessName;
        KPRIORITY       BasePriority;
        ULONG           ProcessId;
        ULONG           InheritedFromProcessId;
        ULONG           HandleCount;
        ULONG           Reserved2[2];
        VM_COUNTERS     VmCounters;
    #if _WIN32_WINNT >= 0x500
        IO_COUNTERS     IoCounters;
    #endif
        SYSTEM_THREADS  Threads[1];
    } SYSTEM_PROCESSES, * PSYSTEM_PROCESSES;

    typedef LONG (WINAPI *NTQUERYSYSTEMINFORMATION)(UINT SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);

    NTQUERYSYSTEMINFORMATION    NtQuerySystemInformation;
    PSYSTEM_PROCESSES           pInfo;
    HINSTANCE                   hNTDll;
    PCWSTR                      pszProcessName;
    DWORD                       dwPID;
    ULONG                       BufferLen = 0x8000;
    LPVOID                      pBuffer = NULL;
    LONG                        Status;

#ifndef UNICODE
    CHAR                        szProcessName[MAX_PATH];
#endif

    // Callback function must be defined
    if (!pfnEnumProc)
        return FALSE;

    // Initialize PID to -1
    if (pdwPID)
        *pdwPID = -1;

    // Get NTDLL handle
    if (!(hNTDll = LoadLibrary(TEXT("NTDLL.DLL"))))
        return FALSE;

    // Load NtQuerySystemInformation() dynamically
    if (!(NtQuerySystemInformation = (NTQUERYSYSTEMINFORMATION)GetProcAddress(hNTDll, "NtQuerySystemInformation")))
    {
        FreeLibrary(hNTDll);
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    // Find needed buffer length
    do
    {
        if (!(pBuffer = malloc(BufferLen)))
        {
            FreeLibrary(hNTDll);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        Status = NtQuerySystemInformation(SystemProcessesAndThreadsInformation,
                                          pBuffer, BufferLen, NULL);

        if (Status == STATUS_INFO_LENGTH_MISMATCH)
        {
            free(pBuffer);
            BufferLen *= 2;
        }
        else if (!NT_SUCCESS(Status))
        {
            free(pBuffer);
            FreeLibrary(hNTDll);
            return FALSE;
        }
    }
    while (Status == STATUS_INFO_LENGTH_MISMATCH);

    pInfo = (PSYSTEM_PROCESSES)pBuffer;
    for (;;)
    {
        pszProcessName = pInfo->ProcessName.Buffer;
        if (pszProcessName == NULL)
            pszProcessName = L"Idle";
        dwPID = pInfo->ProcessId;

        // Call callback function
        // If returned FALSE stop enumeration
#ifdef UNICODE
        if (!pfnEnumProc(dwPID, (LPCTSTR)pszProcessName, pdwPID, pszName))
            break;
#else
        WideCharToMultiByte(CP_ACP, 0, pszProcessName, -1, szProcessName, MAX_PATH, NULL, NULL);

        if (!pfnEnumProc(dwPID, (LPCTSTR)szProcessName, pdwPID, pszName))
                break;
#endif

        if (pInfo->NextEntryDelta == 0)
                break;

        // Find the address of the next process structure
        pInfo = (PSYSTEM_PROCESSES)(((PUCHAR)pInfo) + pInfo->NextEntryDelta);
    }

    free(pBuffer);
    FreeLibrary(hNTDll);
    return TRUE;
}


/****************************
 * Enumerate all processes. *
 ****************************/
BOOL _EnumProcesses(PFNENUMPROC pfnEnumProc, LPCTSTR pszName, PDWORD pdwPID)
{
    OSVERSIONINFO   info;

    info.dwOSVersionInfoSize = sizeof(info);
    GetVersionEx(&info);

    // Enumerate processes using Toolhelp or  NtQuerySystemInformation() depending on the OS version
    if (info.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
        return EnumProcessesToolHelp(pfnEnumProc, pszName, pdwPID);
    else if (info.dwPlatformId == VER_PLATFORM_WIN32_NT)
        return EnumProcessesNtQuerySystemInformation(pfnEnumProc, pszName, pdwPID);
    else
        return FALSE;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////


BOOL CALLBACK GetPIDFromNameCallback(DWORD dwProcessId, LPCTSTR pszProcessName, PDWORD pdwPID, LPCTSTR pszNameToFind)
{
    // Process found ?
    if (_tcsicmp(pszProcessName, pszNameToFind) == 0)
    {
        // Stop enumeration and return current PID
        *pdwPID = dwProcessId;
        return FALSE;
    }
    else
        // Continue enumeration
        return TRUE;
}


/********************************
 * Return PID for Process Name. *
 ********************************/
DWORD GetPIDFromName(LPCTSTR szProcessName)
{
    DWORD dwPID;

    // Check szProcessName
    if (!szProcessName)
        return -1;

    if (_EnumProcesses(GetPIDFromNameCallback, szProcessName, &dwPID))
        return dwPID;
    else
        return -1;
}
