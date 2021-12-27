/*******************************************************************************
 * Injection Library                                                           *
 * Library that implements functions used in remote code injection.            *
 *                                                                             *
 * GetProcessInfo(): Returns info about a remote process.                      *
 * RemoteExecute(): Execute code in the context of a remote process.           *
 * InjectDll(): Inject a DLL into the address space of a remote process.       *
 * EjectDll(): Unload a DLL from the address space of a remote process.        *
 * StartRemoteSubclass(): Subclass a remote process window procedure.          *
 * StopRemoteSubclass(): Restore the remote process original window procedure. *
 *                                                                             *
 * (c) A. Miguel Feijao, 10/8/2005 - 1/11/2011                                 *
 *******************************************************************************/

/*******************************************************************************
 * VS2010 compilation notes:                                                   *
 * - Basic Runtime Checks = Default                                            *
 * - BufferSecurity Check = No (/GS-)                                          *
 *******************************************************************************/

#define   DLL_EXPORT    // EXPORT functions

#define   WIN32_LEAN_AND_MEAN

#include  <windows.h>

/** mingw **/
#include "seh.h"
/** mingw **/

#include  "Inject.h"
#include  "Remote.h"
#include  "Struct.h"
#include  "GetProcAddress.h"
#include  "LenDis.h"

/////////////////////////////////// GetProcessInfo() ///////////////////////////////////

/****************************************************************************
 * GetProcessInfo()                                                         *
 *                                                                          *
 * Return info about a running process.                                     *
 * The returned DWORD consists of two parts:                                *
 * The HIWORD contains the process subsystem:                               *
 *  0 = IMAGE_SUBSYSTEM_UNKNOWN                  (unknown process type)     *
 *  1 = IMAGE_SUBSYSTEM_NATIVE                   (native process)           *
 *  2 = IMAGE_SUBSYSTEM_WINDOWS_GUI              (GUI process)              *
 *  3 = IMAGE_SUBSYSTEM_WINDOWS_CUI              (character mode process)   *
 *  5 = IMAGE_SUBSYSTEM_OS2_CUI                  (OS/2 character process)   *
 *  7 = IMAGE_SUBSYSTEM_POSIX_CUI                (Posix character process)  *
 *  8 = IMAGE_SUBSYSTEM_NATIVE_WINDOWS           (Win9x driver)             *
 *  9 = IMAGE_SUBSYSTEM_WINDOWS_CE_GUI           (Windows CE process)       *
 * 10 = IMAGE_SUBSYSTEM_EFI_APPLICATION          (EFI Application)          *
 * 11 = IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER  (EFI Boot Service Driver)  *
 * 12 = IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER       (EFI Runtime Driver)       *
 * 13 = IMAGE_SUBSYSTEM_EFI_ROM                  (EFI ROM)                  *
 * 14 = IMAGE_SUBSYSTEM_XBOX                     (XBox system)              *
 * 16 = IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION (Windows Boot Application) *
 * The LOWORD contains one or more flags:                                   *
 *  1 = fWIN9X           (Win 9x process)                                   *
 *  2 = fWINNT           (Win NT process)                                   *
 *  4 = fINVALID         (invalid process)                                  *
 *  8 = fDEBUGGED        (process is being debugged)                        *
 * 16 = fNOTINITIALIZED  (process didn't finished initialization)           *
 * 20 = fPROTECTED       (protected process)                                *
 * In case of error HIWORD=Error Code and LOWORD=-1                         *
 ****************************************************************************/
DWORD GetProcessInfo(DWORD dwPID)
{
    WORD                ProcessFlags = 0;	// Initialize to zero
    HANDLE              hProcess;
    DWORD               dwTID;
    PIMAGE_NT_HEADERS32 pNTHeader;

    PPDB        pPDB;
    PTDB        pTDB;
    DWORD       *pThreadHead;
    PTHREADLIST pThreadNode;
    DWORD       TIBFlags;
    PVOID       pvStackUserTop;
    DWORD       StackUserTopContents;
    WORD        MTEIndex;
    PIMTE       pIMTE;

    PPEB        pPEB;
    PEB         PEB;
    BOOL        DebugPort;
	NTSTATUS    Status;
    PROCESS_BASIC_INFORMATION          pbi;
    PROCESS_EXTENDED_BASIC_INFORMATION ExtendedBasicInformation;

    __seh_try
    {
        /********* Win 9x *********/
        if (OSWin9x)
        {
            // Assume Win9x process
            ProcessFlags |= fWIN9X;

            // Get process handle
            if (!(hProcess = OpenProcess(PROCESS_VM_READ, FALSE, dwPID)))
                return MAKELONG(-1, ERROR_OPENPROCESS);

            // Pointer to PDB (Process Database)
            if (!(pPDB = GetPDB(dwPID)))
                return MAKELONG(-1, ERROR_GETPDB);

            // Process is being debugged
            if (((PPDB98)pPDB)->DebuggeeCB || ((PPDB98)pPDB)->Flags & fDebugSingle)
                ProcessFlags |= fDEBUGGED;

            // Termination status must be 0x103
            if (((PPDB98)pPDB)->TerminationStatus != 0x103)
                ProcessFlags |= fINVALID;

            // Invalid PDB flags
            if (((PPDB98)pPDB)->Flags & (fTerminated | fTerminating | fNearlyTerminating | fDosProcess | fWin16Process))
                ProcessFlags |= fINVALID;

            // Get thread list (from PDB)
            if (!(pThreadHead = (DWORD *)((PPDB98)pPDB)->ThreadList))
                return MAKELONG(-1, ERROR_THREADLIST);
            if (!(pThreadNode = (THREADLIST *)*pThreadHead))
                return MAKELONG(-1, ERROR_THREADLIST);

            // TDB of 1st (main) thread
            pTDB = (PTDB)pThreadNode->pTDB;

            // Check if TID is valid
            dwTID = (DWORD)pTDB ^ dwObsfucator;
            if (!IsThreadId(dwTID))
                return MAKELONG(-1, ERROR_ISTHREADID);

            // If pointers are bellow 0x80000000 process not initialized (?!?)
            // (c) R. Picha
            if ((int)pThreadHead > 0 || (int)pThreadNode > 0 || (int)pTDB > 0)
                ProcessFlags |= fNOTINITIALIZED;

            // Get TIB flags
            if (OSWin95)
                TIBFlags = ((PTDB95)pTDB)->tib.TIBFlags;
            else if (OSWin98)
                TIBFlags = ((PTDB98)pTDB)->tib.TIBFlags;
            else if (OSWinMe)
                TIBFlags = ((PTDBME)pTDB)->tib.TIBFlags;
            else
                TIBFlags = 0;

            // Check if Win32 process initialized
            if (TIBFlags & TIBF_WIN32)
            {
                // Get top of stack
                if (OSWin95)
                    pvStackUserTop = ((PTDB95)pTDB)->tib.pvStackUserTop;
                else if (OSWin98)
                    pvStackUserTop = ((PTDB98)pTDB)->tib.pvStackUserTop;
                else if (OSWinMe)
                    pvStackUserTop = ((PTDBME)pTDB)->tib.pvStackUserTop;
                else
                    pvStackUserTop = NULL;

                // Last DWORD pushed on stack
                pvStackUserTop = (DWORD *)((DWORD)pvStackUserTop - sizeof(DWORD));

                // Read last DWORD pushed on stack
                if (!ReadProcessMemory(hProcess, pvStackUserTop, &StackUserTopContents, sizeof(StackUserTopContents), NULL))
                    return MAKELONG(-1, ERROR_READPROCESSMEMORY);

                // Process finished initialization if last DWORD on stack is < 0x80000000 (2GB)
                // (c) R. Picha
                if ((int)StackUserTopContents < 0)
                    ProcessFlags |= fNOTINITIALIZED;
            }

            // Get IMTE pointer for the process
            MTEIndex = ((PPDB98)pPDB)->MTEIndex;
            pIMTE = pMTEModTable[MTEIndex];

            // Get pointer to NTHeader from the IMTE
            pNTHeader = pIMTE->pNTHdr;
            if (pNTHeader->Signature != LOWORD(IMAGE_NT_SIGNATURE)) // "PE"
                return MAKELONG(-1, ERROR_INVALIDNTHEADER);

            CloseHandle(hProcess);

            // Return Subsystem + Process Flags
            return MAKELONG(ProcessFlags, pNTHeader->OptionalHeader.Subsystem);
        }

        /***** Win NT *****/
        else if (OSWinNT)
        {
            // Assume Win NT process
            ProcessFlags |= fWINNT;

			// Get process handle
            hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwPID);

            // Get process Extended Basic Info (this will fail if Windows version is less than Vista)
			memset(&ExtendedBasicInformation, 0, sizeof(ExtendedBasicInformation));
			ExtendedBasicInformation.Size = sizeof(PROCESS_EXTENDED_BASIC_INFORMATION);
            NtQueryInformationProcess(hProcess,
                                      ProcessBasicInformation,
                                      &ExtendedBasicInformation,
                                      sizeof(ExtendedBasicInformation),
                                      NULL);

            CloseHandle(hProcess);

			// Protected process
			if (ExtendedBasicInformation.IsProtectedProcess)
			{
				ProcessFlags |= fPROTECTED;
				return MAKELONG(ProcessFlags, IMAGE_SUBSYSTEM_UNKNOWN);
			}

            // Get process handle
            if (!(hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID)))
                return MAKELONG(-1, ERROR_OPENPROCESS);

            // Get Debug port status
            Status = NtQueryInformationProcess(hProcess,
                                               ProcessDebugPort,
                                               &DebugPort,
                                               sizeof(DebugPort),
                                               NULL);

            if (!NT_SUCCESS(Status))
                return MAKELONG(-1, ERROR_NTQUERYINFORMATIONPROCESS);

            // Process is being debugged
            if (DebugPort)
                ProcessFlags |= fDEBUGGED;

            // Get PEB base address of process
            Status = NtQueryInformationProcess(hProcess,
                                               ProcessBasicInformation,
                                               &pbi,
                                               sizeof(pbi),
                                               NULL);

            if (!NT_SUCCESS(Status))
                return MAKELONG(-1, ERROR_NTQUERYINFORMATIONPROCESS);

            // Exit status must be 0x103
            if (pbi.ExitStatus != 0x103)
                ProcessFlags |= fINVALID;

            // Read PEB
            // (for local process this is the same as FS:[0x30])
            pPEB = pbi.PebBaseAddress;
            if (pPEB == NULL)
            {
                ProcessFlags |= fINVALID;
                return MAKELONG(ProcessFlags, IMAGE_SUBSYSTEM_NATIVE);
            }
            else
            {
                if (!ReadProcessMemory(hProcess, pPEB, &PEB, sizeof(PEB), NULL))
                    return MAKELONG(-1, ERROR_READPROCESSMEMORY);

                // Process is being debugged
                if (PEB.BeingDebugged)
                    ProcessFlags |= fDEBUGGED;

                // Process not yet initialized
                if (!PEB.Ldr || !PEB.LoaderLock)
                    ProcessFlags |= fNOTINITIALIZED;
            }

            CloseHandle(hProcess);

            // Return Subsystem + Process Flags
            return MAKELONG(ProcessFlags, PEB.ImageSubsystem);
        }
        else
            return MAKELONG(-1, ERROR_INVALIDOS);
    }
    // Exception ocurred
    __seh_except(EXCEPTION_EXECUTE_HANDLER)
    {
        return MAKELONG(-1, ERROR_EXCEPTION);
    }
    __seh_end_except
}


/////////////////////////// RemoteExecute() ////////////////////////////

/****************************************
 * InitializeAndPatchStub()             *
 *                                      *
 * Patches remote stub data at runtime. *
 ****************************************/
int InitializeAndPatchStub(HANDLE hProcess, PBYTE pCode, OFFSETS offs, DWORD UserFunc, DWORD Native)
{
    DWORD   nBytesWritten = 0;
    BOOL    fFinished = FALSE;

    if (OSWin9x)
    {
        *(PDWORD)(pCode + offs.PUserFunc) = UserFunc;
        *(PDWORD)(pCode + offs.PLdrShutdownThread) = (DWORD)LdrShutdownThread;
        *(PDWORD)(pCode + offs.PNtFreeVirtualMemory) = (DWORD)NtFreeVirtualMemory;
        *(PDWORD)(pCode + offs.PNtTerminateThread) = (DWORD)NtTerminateThread;
        *(PDWORD)(pCode + offs.PNative) = Native;
        *(PDWORD)(pCode + offs.PFinished) = FALSE;
        return 0;
    }
    else
    {
        if (!WriteProcessMemory(hProcess, pCode + offs.PUserFunc, &UserFunc, sizeof(UserFunc), &nBytesWritten) ||
            nBytesWritten != sizeof(UserFunc))
            return -1;
        if (!WriteProcessMemory(hProcess, pCode + offs.PLdrShutdownThread, &LdrShutdownThread,
            sizeof(LdrShutdownThread), &nBytesWritten) || nBytesWritten != sizeof(LdrShutdownThread))
            return -1;
        if (!WriteProcessMemory(hProcess, pCode + offs.PNtFreeVirtualMemory, &NtFreeVirtualMemory,
            sizeof(NtFreeVirtualMemory), &nBytesWritten) || nBytesWritten != sizeof(NtFreeVirtualMemory))
            return -1;
        if (!WriteProcessMemory(hProcess, pCode + offs.PNtTerminateThread, &NtTerminateThread,
            sizeof(NtTerminateThread), &nBytesWritten) || nBytesWritten != sizeof(NtTerminateThread))
            return -1;
        if (!WriteProcessMemory(hProcess, pCode + offs.PNative, &Native, sizeof(Native), &nBytesWritten) ||
            nBytesWritten != sizeof(Native))
            return -1;
        if (!WriteProcessMemory(hProcess, pCode + offs.PFinished, &fFinished, sizeof(fFinished), &nBytesWritten) ||
            nBytesWritten != sizeof(fFinished))
            return -1;
        return 0;
    }
}

/****************************************************
 * RemoteExecute()                                  *
 *                                                  *
 * Execute code in the context of a remote process. *
 * Return zero if everything went ok or error code. *
 ****************************************************/
int RemoteExecute(HANDLE hProcess,                      // Remote process handle
                  DWORD  ProcessFlags,                  // ProcessFlags returned by GetProcessInfo()
                  LPTHREAD_START_ROUTINE Function,      // Remote thread function
                  PVOID  pData,                         // User data passed to remote thread
                  DWORD  Size,                          // Size of user data block (0=treat pData as DWORD)
                  DWORD  dwTimeout,                     // Timeout value
                  PDWORD ExitCode)                      // Return exit code from remote code
{
    PBYTE       pStubCode = NULL;
    PBYTE       pRemoteCode = NULL;
    PBYTE       pRemoteData = NULL;
    PVOID       pParams = NULL;
    DWORD       FunctionSize;
    DWORD       nBytesWritten = 0, nBytesRead = 0;
    HANDLE      hThread = NULL;
    DWORD       dwThreadId;
    DWORD       dwExitCode = -1;
    int         ErrorCode = 0;
    DWORD       dwCreationFlags = 0; // dwCreationFlags parameter for CreateRemoteThread()
    NTSTATUS    Status;
    BOOL        fNative;
    BOOL        fFinished;
    DWORD       dwTmpTimeout = 100;	// 100 ms
    OFFSETS     StubOffs;

    PBYTE       data;
	DWORD       offset;

    __seh_try
    {
        __seh_try 
        {
            // Initialize ExitCode to -1
            if (ExitCode)
                *ExitCode = -1;

            // ProcessFlags = 0 ?
            if (!ProcessFlags)
                ProcessFlags = GetProcessInfo(_GetProcessId(hProcess));

            // Invalid Process flags
            if (ProcessFlags & fINVALID)
            {
                ErrorCode = ERROR_INVALIDPROCESS;
                __seh_leave;
            }

            // Get ASM code offsets
            GetOffsets(&StubOffs);

            // a fix for function addresses pointing to "JMP "
            data = (PBYTE)Function;
            if (*data == 0xE9) // JMP => a thunk
			{
               offset = *(PDWORD)(data + 1);
               data = data + 5 + offset;
               Function = (LPTHREAD_START_ROUTINE)data;
			}

            // Check if function code is safe to be relocated
            if (IsCodeSafe((PBYTE)Function, &FunctionSize) != 0)
            {
                ErrorCode = ERROR_ISCODESAFE;
                __seh_leave;
            }

            // Allocate memory for function in remote process
            if (!(pRemoteCode = _VirtualAllocEx(hProcess, 0, FunctionSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)))
            {
                ErrorCode = ERROR_VIRTUALALLOCEX;
                __seh_leave;
            }

            // Copy function code to remote process
            if (!WriteProcessMemory(hProcess, pRemoteCode, Function, FunctionSize, &nBytesWritten) ||
                nBytesWritten != FunctionSize)
            {
                ErrorCode = ERROR_WRITEPROCESSMEMORY;
                __seh_leave;
            }

            // Data block specified ?
            if (pData && Size)
            {
                // Allocate memory for data block in remote process
                if (!(pRemoteData = _VirtualAllocEx(hProcess, 0, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)))
                {
                    ErrorCode = ERROR_VIRTUALALLOCEX;
                    __seh_leave;
                }

                // Copy data block to remote process
                if (!WriteProcessMemory(hProcess, pRemoteData, pData, Size, &nBytesWritten) || nBytesWritten != Size)
                {
                    ErrorCode = ERROR_WRITEPROCESSMEMORY;
                    __seh_leave;
                }

                pParams = pRemoteData;
            }
            // Pass value directly to CreateThread()
            else
                pParams = pData;

            // Size of stub code
            FunctionSize = StubOffs.StubSize;

            // Allocate memory for stub code in remote process
            if (!(pStubCode = _VirtualAllocEx(hProcess, 0, FunctionSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)))
            {
                ErrorCode = ERROR_VIRTUALALLOCEX;
                __seh_leave;
            }

            // Copy stub code to remote process
            if (!WriteProcessMemory(hProcess, pStubCode, (LPVOID)StubOffs.StubStart,
                 FunctionSize, &nBytesWritten) || nBytesWritten != FunctionSize)
            {
                ErrorCode = ERROR_WRITEPROCESSMEMORY;
                __seh_leave;
            }

			// NT native process requires a different stub exit code
            fNative = ((HIWORD(ProcessFlags) == IMAGE_SUBSYSTEM_NATIVE) && (ProcessFlags & fWINNT));

            // Patch Stub data
            if (InitializeAndPatchStub(hProcess, pStubCode, StubOffs, (DWORD)pRemoteCode, fNative) != 0)
            {
                ErrorCode = ERROR_PATCH;
                __seh_leave;
            }

            // Process not initialized
            if (ProcessFlags & fNOTINITIALIZED)
            {
                // Win9x
                if (ProcessFlags & fWIN9X)
                {
                    dwCreationFlags |= CREATE_SILENT;
                    ProcessFlags &= ~fNOTINITIALIZED;       // Goto initialized
                }

                // WinNT
                else if (ProcessFlags & fWINNT)
                {
                    if (!(dwThreadId = _GetProcessThread(_GetProcessId(hProcess))))
                    {
                        ProcessFlags |= ~fNOTINITIALIZED;
                        goto Initialized;					// Try initialized
                    }

                    if (!(hThread = _OpenThread(THREAD_SET_CONTEXT, FALSE, dwThreadId)))
                    {
                        ProcessFlags |= ~fNOTINITIALIZED;
                        goto Initialized;					// Try initialized
                    }

                    Status = NtQueueApcThread(hThread,                      // hThread
                                              (PKNORMAL_ROUTINE)pStubCode,  // APC Routine
                                              pParams,                      // Argument 1
                                              NULL,                         // Argument 2
                                              NULL);                        // Argument 3

                    if (!NT_SUCCESS(Status))
                    {
                        ProcessFlags |= ~fNOTINITIALIZED;
                        goto Initialized;					// Try initialized
                    }

                    // Wait for remote code to finish
                    dwTmpTimeout = min(dwTmpTimeout, dwTimeout);
                    for (fFinished = FALSE; !fFinished && dwTimeout != 0; dwTimeout -= min(dwTmpTimeout, dwTimeout))
                    {
                        WaitForSingleObject(GetCurrentThread(), dwTmpTimeout);
                        if (!ReadProcessMemory(hProcess, pStubCode + StubOffs.PFinished, &
                                               fFinished, sizeof(fFinished), &nBytesRead) || nBytesRead != sizeof(fFinished))
                        {
                            ErrorCode = ERROR_READPROCESSMEMORY;
                            __seh_leave;
                        }
                    }

                    // Timeout ocurred
                    if (dwTimeout == 0 && !fFinished)
                        ErrorCode = ERROR_WAITTIMEOUT;

                    // Doesn't make sense to GetExitCodeThread() on a "hijacked" thread !
                    dwExitCode = 0;
                }/*Win NT*/
            }/*Not initialized*/

Initialized:
            // Initialized process
            if (!(ProcessFlags & fNOTINITIALIZED))
            {
                // NT native
                if (fNative)
                {
                    Status = RtlCreateUserThread(hProcess,      // hProcess
                                                 NULL,          // &SecurityDescriptor
                                                 FALSE,         // CreateSuspended
                                                 0,             // StackZeroBits
                                                 NULL,          // StackReserved
                                                 NULL,          // StackCommit
                                                 pStubCode,     // StartAddress
                                                 pParams,       // StartParameter
                                                 &hThread,      // &hThread
                                                 NULL);         // &ClientId

                    if (!NT_SUCCESS(Status))
                    {
                        SetLastError(RtlNtStatusToDosError(Status));
                        ErrorCode = ERROR_RTLCREATETHREAD;
                        __seh_leave;
                    }
                }

                // Win32 process
                else
                    // Create remote thread
                    hThread = _CreateRemoteThread(hProcess,
                                                  NULL,
                                                  0,
                                                  (LPTHREAD_START_ROUTINE)pStubCode,
                                                  pParams,
                                                  dwCreationFlags,
                                                  &dwThreadId);


                // Error in creating thread
                if (!hThread)
                {
                    ErrorCode = ERROR_CREATETHREAD;
                    __seh_leave;
                }

                // Wait for thread to terminate
                if (WaitForSingleObject(hThread, dwTimeout) != WAIT_OBJECT_0)
                {
                    ErrorCode = ERROR_WAITTIMEOUT;
                    __seh_leave;
                }

                // Get thread exit code
                GetExitCodeThread(hThread, &dwExitCode);
            }/*Initialized*/

            // Data block specified ?
            if (pData && Size)
            {
                // Read back remote data block
                if (!ReadProcessMemory(hProcess, pRemoteData, pData, Size, &nBytesRead) || nBytesRead != Size)
                {
                    ErrorCode = ERROR_READPROCESSMEMORY;
                    __seh_leave;
                }
            }
        }
        // Cleanup
        __seh_finally
        {
            if (pStubCode)
                _VirtualFreeEx(hProcess, pStubCode, 0, MEM_RELEASE);
            if (pRemoteCode)
                _VirtualFreeEx(hProcess, pRemoteCode, 0, MEM_RELEASE);
            if (pRemoteData)
                _VirtualFreeEx(hProcess, pRemoteData, 0, MEM_RELEASE);
            if (hThread)
                CloseHandle(hThread);
        }
        __seh_end_finally
    }
    // Exception ocurred
    __seh_except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode() | LOCAL_EXCEPTION;
    }
    __seh_end_except

    // Return remote stub exit code
    if (ExitCode)
        *ExitCode = dwExitCode;

    // Return RemoteExecute() error code or remote code exception code
    if ((ErrorCode == 0) && (dwExitCode & REMOTE_EXCEPTION))
        return dwExitCode;
    else
        return ErrorCode;
}

/////////////////////////// InjectDll() ////////////////////////////

/****************************
 * Remote InjectDll thread. *
 ****************************/
#pragma check_stack(off)
static DWORD WINAPI RemoteInjectDll(PRDATADLL pData)
{
    pData->hRemoteDll = pData->LoadLibrary(pData->szDll);

    if (pData->hRemoteDll == NULL)
        pData->Result = -1;
    else
        pData->Result = 0; // 0 = OK

    return pData->Result;
}


/**********************************************************
 * Load a Dll into the address space of a remote process. *
 * (ANSI version)                                         *
 **********************************************************/
int InjectDllA(HANDLE    hProcess,       // Remote process handle
               DWORD     ProcessFlags,   // ProcessFlags returned by GetProcessInfo()
               LPCSTR    szDllPath,      // Path of Dll to load
               DWORD     dwTimeout,      // Timeout value
               HINSTANCE *hRemoteDll)    // Return handle of loaded Dll
{
    int       rc;
    int       ErrorCode = 0;
    DWORD     ExitCode = -1;
    HINSTANCE hKernel32 = 0;
    RDATADLL  rdDll;

    __seh_try
    {
        __seh_try
        {
            // Load Kernel32.dll
            if (!(hKernel32 = LoadLibraryA("Kernel32.dll")))
            {
                ErrorCode = ERROR_LOADLIBRARY;
                __seh_leave;
            }

            // Initialize data block passed to RemoteInjectDll()
            rdDll.Result = -1;
            rdDll.hRemoteDll = NULL;
            lstrcpyA(rdDll.szDll, szDllPath);
            rdDll.LoadLibrary = (LOADLIBRARY)GetProcAddress(hKernel32, "LoadLibraryA");

            if (!rdDll.LoadLibrary)
            {
                ErrorCode = ERROR_GETPROCADDRESS;
                __seh_leave;
            }

            // Execute RemoteInjectDll() in remote process
            rc = RemoteExecute(hProcess,
                               ProcessFlags,
                               RemoteInjectDll,
                               &rdDll,
                               sizeof(rdDll),
                               dwTimeout,
                               &ExitCode);
        }

        __seh_finally
        {
            if (hKernel32)
                FreeLibrary(hKernel32);
        }
        __seh_end_finally
    }
    // Exception ocurred
    __seh_except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode() | LOCAL_EXCEPTION;
    }
    __seh_end_except

    // Return handle of loaded dll
    if (hRemoteDll)
        *hRemoteDll = rdDll.hRemoteDll;

    // Return error code
    if (ErrorCode == 0 && rc != 0)
        return rc;
    else if (ErrorCode == 0 && ExitCode != 0)
        return ERROR_REMOTE;
    else
        return ErrorCode;
}


/**********************************************************
 * Load a Dll into the address space of a remote process. *
 * (Unicode version)                                      *
 **********************************************************/
int InjectDllW(HANDLE    hProcess,       // Remote process handle
               DWORD     ProcessFlags,   // ProcessFlags returned by GetProcessInfo()
               LPCWSTR   szDllPath,      // Path of Dll to load
               DWORD     dwTimeout,      // Timeout value
               HINSTANCE *hRemoteDll)    // Return handle of loaded Dll
{
    char DllPath[MAX_PATH + 1];

	// Convert from Unicode to Ansi
    *DllPath = '\0';
    WideCharToMultiByte(CP_ACP, 0, szDllPath, -1, DllPath, MAX_PATH, NULL, NULL);

    return InjectDllA(hProcess, ProcessFlags, DllPath, dwTimeout, hRemoteDll);
}

/****************************
 * Remote EjectDll thread. *
 ****************************/
/*
#pragma check_stack(off)
static DWORD WINAPI RemoteEjectDll(PRDATADLL pData)
{
    if (pData->szDll[0] != '\0')
        pData->hRemoteDll = pData->GetModuleHandle(pData->szDll);

    pData->Result = pData->FreeLibrary(pData->hRemoteDll);

    return (pData->Result == 0); // 0 = OK
}
*/

#pragma check_stack(off)
static DWORD WINAPI RemoteEjectDll(PRDATADLL pData)
{
    int i = 0;

    do
    {
        if (pData->szDll[0] != '\0')
            pData->hRemoteDll = pData->GetModuleHandle(pData->szDll);

        pData->Result = pData->FreeLibrary(pData->hRemoteDll);
        i++;
    } while (pData->Result);

    return (i > 1 ? 0 : -1); // 0 = OK
}


/************************************************************
 * Unload a Dll from the address space of a remote process. *
 * (ANSI version)                                           *
 ************************************************************/
int EjectDllA(HANDLE     hProcess,       // Remote process handle
              DWORD      ProcessFlags,   // ProcessFlags returned by GetProcessInfo()
              LPCSTR     szDllPath,      // Path of Dll to unload
              HINSTANCE  hRemoteDll,     // Dll handle
              DWORD      dwTimeout)      // Timeout value
{
    int       rc;
    int       ErrorCode = 0;
    DWORD     ExitCode = -1;
    HINSTANCE hKernel32 = 0;
    RDATADLL  rdDll;

    __seh_try
    {
        __seh_try
        {
            // Load Kernel32.dll
            if (!(hKernel32 = LoadLibraryA("Kernel32.dll")))
            {
                ErrorCode = ERROR_LOADLIBRARY;
                __seh_leave;
            }

            // Initialize data block passed to RemoteInjectDll()
            rdDll.Result = -1;
            rdDll.hRemoteDll = hRemoteDll;
            if (szDllPath)
                lstrcpyA(rdDll.szDll, szDllPath);
            rdDll.FreeLibrary = (FREELIBRARY)GetProcAddress(hKernel32, "FreeLibrary");
            rdDll.GetModuleHandle = (GETMODULEHANDLE)GetProcAddress(hKernel32, "GetModuleHandleA");

            if (!rdDll.FreeLibrary || !rdDll.GetModuleHandle)
            {
                ErrorCode = ERROR_GETPROCADDRESS;
                __seh_leave;
            }

            // Execute RemoteEjectDll() in remote process
            rc = RemoteExecute(hProcess,
                               ProcessFlags,
                               RemoteEjectDll,
                               &rdDll,
                               sizeof(rdDll),
                               dwTimeout,
                               &ExitCode);
        }

        __seh_finally
        {
            if (hKernel32)
                FreeLibrary(hKernel32);
        }
        __seh_end_finally
    }
    // Exception ocurred
    __seh_except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode() | LOCAL_EXCEPTION;
    }
    __seh_end_except

    // Return error code
    if (ErrorCode == 0 && rc != 0)
        return rc;
    else if (ErrorCode == 0 && ExitCode != 0)
        return ERROR_REMOTE;
    else
        return ErrorCode;
}


/************************************************************
 * Unload a Dll from the address space of a remote process. *
 * (Unicode version)                                        *
 ************************************************************/
int EjectDllW(HANDLE     hProcess,       // Remote process handle
              DWORD      ProcessFlags,   // ProcessFlags returned by GetProcessInfo()
              LPCWSTR    szDllPath,      // Path of Dll to unload
              HINSTANCE  hRemoteDll,     // Dll handle
              DWORD      dwTimeout)      // Timeout value
{
    char DllPath[MAX_PATH + 1];

	// Convert from Unicode to Ansi
    *DllPath = '\0';
    WideCharToMultiByte(CP_ACP, 0, szDllPath, -1, DllPath, MAX_PATH, NULL, NULL);

    return EjectDllA(hProcess, ProcessFlags, DllPath, hRemoteDll, dwTimeout);
}


///////////////////////////// Start/StopRemoteSubclass() ////////////////////////////

/************************************************
 * InitializeAndPatchStubWndProc()               *
 *                                               *
 * Patches remote StubWndProc() data at runtime. *
 *************************************************/
int InitializeAndPatchStubWndProc(HANDLE hProcess, PBYTE pCode, OFFSETS offs, DWORD pRDATA)
{
    DWORD   nBytesWritten = 0;

    if (OSWin9x)
    {
        *(PDWORD)(pCode + offs.pRDATA) = pRDATA;
        return 0;
    }
    else
    {
        if (!WriteProcessMemory(hProcess, pCode + offs.pRDATA, &pRDATA, sizeof(pRDATA), &nBytesWritten) ||
            nBytesWritten != sizeof(pRDATA))
            return -1;
        return 0;
    }
}


/*********************************************************
 * Change window handler by our own.                     *
 * (must be runned in the context of the remote process) *
 *********************************************************/
#pragma check_stack(off)
static DWORD WINAPI RemoteStartSubclass(PRDATA pData)
{
    // Subclass window procedure
    pData->pfnOldWndProc = (WNDPROC)pData->pfnSetWindowLong(pData->hWnd, GWL_WNDPROC, (long)pData->pfnStubWndProc);
    return (pData->pfnOldWndProc == 0); // 0 = OK
}


/******************************************************
 * StartRemoteSubclass()                              *
 *                                                    *
 * Change remote process window procedure by our own. *
 ******************************************************/
int StartRemoteSubclass(PRDATA rd, USERWNDPROC WndProc)
{
    BOOL    fUnicode;               // True if remote window is unicode
    HMODULE hUser32  = NULL;        // Handle of user32.dll
    HANDLE  hThread = 0;            // The handle and ID of the thread executing
    DWORD   dwThreadId = 0;         //  the remote StartSubclass().
    int     nSuccess = FALSE;       // Subclassing succeded?
    int     WndProcSize, StubWndProcSize;
    int     NumBytesWritten, NumBytesRead;
    int     rc;
    int     ErrorCode = 0;
    DWORD   ExitCode = -1;
    OFFSETS StubOffs;
    PBYTE   pStubWndProc;

    // Size of RDATA structure must be specified
    if (rd->Size < sizeof(RDATA))
        return ERROR_INVALIDPARAMETER;

    // These fields must be initialized
    if (!rd->hProcess || !rd->hWnd)
        return ERROR_INVALIDPARAMETER;

    __seh_try
    {
        __seh_try
        {
            // Get ASM code offsets
            GetOffsets(&StubOffs);

            /*** Allocate memory in remote process and write a copy of WndProc() to it ***/
            if (IsCodeSafe((PBYTE)WndProc, &WndProcSize) != 0)
            {
                ErrorCode = ERROR_ISCODESAFE;
                __seh_leave;
            }

            rd->pfnUserWndProc = _VirtualAllocEx(rd->hProcess, NULL, WndProcSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!rd->pfnUserWndProc)
            {
                ErrorCode = ERROR_VIRTUALALLOCEX;
                __seh_leave;
            }

            if (!WriteProcessMemory(rd->hProcess, rd->pfnUserWndProc, WndProc, WndProcSize, &NumBytesWritten) || NumBytesWritten != WndProcSize)
            {
                ErrorCode = ERROR_WRITEPROCESSMEMORY;
                __seh_leave;
            }

            /*** Allocate memory in remote process and write a copy of StubWndProc() to it ***/
            pStubWndProc = (PBYTE)StubOffs.StubWndProcStart;
            StubWndProcSize = StubOffs.StubWndProcSize;

            rd->pfnStubWndProc = _VirtualAllocEx(rd->hProcess, NULL, StubWndProcSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!rd->pfnStubWndProc)
            {
                ErrorCode = ERROR_VIRTUALALLOCEX;
                __seh_leave;
            }

            if (!WriteProcessMemory(rd->hProcess, rd->pfnStubWndProc, pStubWndProc, StubWndProcSize, &NumBytesWritten) || NumBytesWritten != StubWndProcSize)
            {
                ErrorCode = ERROR_WRITEPROCESSMEMORY;
                __seh_leave;
            }

            // Get handle of "USER32.DLL"
            hUser32 = LoadLibrary(TEXT("User32.dll"));
            if (!hUser32)
            {
                ErrorCode = ERROR_LOADLIBRARY;
                __seh_leave;
            }

            // Remote window is unicode ?
            fUnicode = IsWindowUnicode(rd->hWnd);

            // Save address of SetWindowLong() and CallWindowProc()
            rd->pfnSetWindowLong = (SETWINDOWLONG)  GetProcAddress(hUser32, fUnicode ? "SetWindowLongW" : "SetWindowLongA");
            rd->pfnCallWindowProc = (CALLWINDOWPROC) GetProcAddress(hUser32, fUnicode ? "CallWindowProcW": "CallWindowProcA");
            if (rd->pfnSetWindowLong  == NULL || rd->pfnCallWindowProc == NULL)
            {
                ErrorCode = ERROR_GETPROCADDRESS;
                __seh_leave;
            }

            rd->pfnOldWndProc = NULL;

            /*** Allocate memory in remote process and write a copy of RDATA struct to it ***/
            rd->pRDATA = _VirtualAllocEx(rd->hProcess, NULL, rd->Size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!rd->pRDATA)
            {
                ErrorCode = ERROR_VIRTUALALLOCEX;
                __seh_leave;
            }

            if (!WriteProcessMemory(rd->hProcess, rd->pRDATA, rd, rd->Size, &NumBytesWritten) || NumBytesWritten != rd->Size)
            {
                ErrorCode = ERROR_WRITEPROCESSMEMORY;
                __seh_leave;
            }

            // Patch pRDATA in StubWndProc()
            if (InitializeAndPatchStubWndProc(rd->hProcess, (PVOID)rd->pfnStubWndProc, StubOffs, (DWORD)rd->pRDATA) != 0)
            {
                ErrorCode = ERROR_PATCH;
                __seh_leave;
            }

            // Execute RemoteStartSubclass() in remote process
            rc = RemoteExecute(rd->hProcess,
                               rd->ProcessFlags,
                               RemoteStartSubclass,
                               rd->pRDATA,
                               0,
                               rd->dwTimeout,
                               &ExitCode);

            // Subclass succeded ?
            if (rc == 0 && ExitCode == 0)
            {
                // Read pfnOldWndProc from RDATA block to rd.pfnOldWndProc
                // (needed for StopRemoteSubclass())
                if (!ReadProcessMemory(rd->hProcess, &rd->pRDATA->pfnOldWndProc, &rd->pfnOldWndProc, sizeof(rd->pfnOldWndProc), &NumBytesRead) ||
                    NumBytesRead != sizeof(rd->pfnOldWndProc))
                {
                    ErrorCode = ERROR_READPROCESSMEMORY;
                    __seh_leave;
                }

                if (rd->pfnOldWndProc)
                    nSuccess = TRUE;
                else
                    ErrorCode = ERROR_REMOTE;
                }
        }

        // Cleanup
        __seh_finally
        {
            // An error ocurred ?
            if (!nSuccess)
            {
                // Release allocated memory
                if (rd->pfnUserWndProc)
                    _VirtualFreeEx(rd->hProcess, rd->pfnUserWndProc, 0, MEM_RELEASE);
                if (rd->pfnStubWndProc)
                    _VirtualFreeEx(rd->hProcess, rd->pfnStubWndProc, 0, MEM_RELEASE);
                if (rd->pRDATA)
                    _VirtualFreeEx(rd->hProcess, rd->pRDATA, 0, MEM_RELEASE);

                // Clear RDATA fields
                rd->pRDATA = NULL;
                rd->pfnStubWndProc = NULL;
                rd->pfnOldWndProc = NULL;
                rd->pfnUserWndProc = NULL;
                rd->pfnSetWindowLong = NULL;
                rd->pfnCallWindowProc = NULL;
            }

            // Release library handle
            if (hUser32)
                FreeLibrary(hUser32);
        }
        __seh_end_finally
    }

    // Exception ocurred
    __seh_except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode() | LOCAL_EXCEPTION;
    }
    __seh_end_except

    // Return error code
    if (ErrorCode == 0 && rc != 0)
        return rc;
    else if (ErrorCode == 0 && ExitCode != 0)
        return ERROR_REMOTE;
    else
        return ErrorCode;
}


/*********************************************************
 * Restore default window handler.                       *
 * (must be runned in the context of the remote process) *
 *********************************************************/
#pragma check_stack(off)
static DWORD WINAPI RemoteStopSubclass(PRDATA pData)
{
    // Restore original window procedure handler
    return (pData->pfnSetWindowLong(pData->hWnd, GWL_WNDPROC, (long)pData->pfnOldWndProc) == 0); // 0 = OK
}


/*****************************************************
 * StopRemoteSubclass()                              *
 *                                                   *
 * Restore remote process original window procedure. *
 *****************************************************/
int StopRemoteSubclass(PRDATA rd)
{
    int     ErrorCode = 0;
    int     rc;
    DWORD   ExitCode = -1;

    // These fields must be initialized
    if (!rd->hProcess ||
        !rd->hWnd ||
        !rd->pRDATA ||
        !rd->pfnStubWndProc ||
        !rd->pfnOldWndProc ||
        !rd->pfnUserWndProc ||
        !rd->pfnSetWindowLong)
        return ERROR_INVALIDPARAMETER;

    __seh_try
    {
        __seh_try
        {
            // Execute remote RemoteStopSubclass()
            rc = RemoteExecute(rd->hProcess,
                               rd->ProcessFlags,
                               RemoteStopSubclass,
                               rd->pRDATA,
                               0,
                               rd->dwTimeout,
                               &ExitCode);
        }

        // Cleanup
        __seh_finally
        {
            // Release memory
            if (rd->pfnStubWndProc)
                    _VirtualFreeEx(rd->hProcess, rd->pfnStubWndProc, 0, MEM_RELEASE);
            if (rd->pfnUserWndProc)
                    _VirtualFreeEx(rd->hProcess, rd->pfnUserWndProc, 0, MEM_RELEASE);
            if (rd->pRDATA)
                    _VirtualFreeEx(rd->hProcess, rd->pRDATA, 0, MEM_RELEASE);

            // Clear RDATA fields
            rd->pRDATA = NULL;
            rd->pfnStubWndProc = NULL;
            rd->pfnOldWndProc = NULL;
            rd->pfnUserWndProc = NULL;
            rd->pfnSetWindowLong = NULL;
            rd->pfnCallWindowProc = NULL;
        }
        __seh_end_finally
    }

    // Exception ocurred
    __seh_except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode() | LOCAL_EXCEPTION;
    }
    __seh_end_except

    // Return error code
    if (ErrorCode == 0 && rc != 0)
        return rc;
    else if (ErrorCode == 0 && ExitCode != 0)
        return ERROR_REMOTE;
    else
        return ErrorCode;
}

/////////////////////////////////////// DllMain ///////////////////////////////////////////

BOOL WINAPI DllMain(HINSTANCE hinstDLL,  // Handle to DLL module
                    DWORD fdwReason,     // Reason for calling function
                    LPVOID lpReserved )  // Reserved
{
    // Perform actions based on the reason for calling
    switch(fdwReason)
    {
        // Initialize once for each new process
        // Return FALSE to fail DLL load
        case DLL_PROCESS_ATTACH:
             // Disable DLL_THREAD_ATTACH and DLL_THREAD_DETACH messages
             DisableThreadLibraryCalls(hinstDLL);

             return Initialization();
             break;

             // Do thread-specific initialization
        case DLL_THREAD_ATTACH:
             break;

             // Do thread-specific cleanup
        case DLL_THREAD_DETACH:
             break;

             // Perform any necessary cleanup
        case DLL_PROCESS_DETACH:
             break;
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH
}
