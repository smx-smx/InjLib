/*****************************************************
 * Unitialized.c                                     *
 * Test Injection library for unitialized processes. *
 *****************************************************/

#include <windows.h>
#include <stdio.h>
#include <conio.h>

#include "..\..\DLL\Inject.h"

typedef struct {
    int    Result;     // Result from remote thread
} PARAMBLOCK, *PPARAMBLOCK;

/************************************
 * Test thread for RemoteExecute(). *
 ************************************/
#pragma check_stack(off)
static DWORD WINAPI RemoteThread(PPARAMBLOCK pData)
{
    __asm mov eax,0
//    __asm mov [eax], eax

    // Return value
    pData->Result = 123;
    return 456;
}


int main()
{
    #define TIMEOUT 1000

    DWORD                   dwPID, dwTID;
    HANDLE                  hProcess, hThread;
    STARTUPINFO             si;
    PROCESS_INFORMATION     pi;

    DWORD           ProcessFlags;
    PARAMBLOCK      MyData;
    DWORD           dwExitCode;
    int             rc;

    HMODULE         hInjLib;
    GETPROCESSINFO  GetProcessInfo ;
    REMOTEEXECUTE   RemoteExecute ;

    // Load "InjLib.dll" library
    if (!(hInjLib = LoadLibrary("InjLib.dll")))
    {
        printf("LoadLibrary() failed.");
        getch();
        return -1;
    }

    // Load DLL exported functions
    GetProcessInfo = (GETPROCESSINFO)GetProcAddress(hInjLib, "GetProcessInfo");
    RemoteExecute = (REMOTEEXECUTE)GetProcAddress(hInjLib, "RemoteExecute");

    if (!GetProcessInfo || !RemoteExecute)
    {
        printf("Failed to load all exported functions.");
        getch();
        return -1;
    }

    // Zero these structs
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Start the child process
    if (!CreateProcess(NULL,             // No module name (use command line).
                       "Notepad.exe",    // Command line.
                       NULL,             // Process handle not inheritable.
                       NULL,             // Thread handle not inheritable.
                       FALSE,            // Set handle inheritance to FALSE.
                       CREATE_SUSPENDED, // No creation flags.
                       NULL,             // Use parent's environment block.
                       NULL,             // Use parent's starting directory.
                       &si,              // Pointer to STARTUPINFO structure.
                       &pi))             // Pointer to PROCESS_INFORMATION structure.
    {
        printf("CreateProcess(): failed.\n");
        getch();
        return -1;
    }

    hProcess = pi.hProcess;
    hThread= pi.hThread;
    dwPID = pi.dwProcessId;
    dwTID = pi.dwThreadId;
    printf("PID: %X, TID: %X\n\n", dwPID, dwTID);

    ProcessFlags = GetProcessInfo(dwPID);
    printf("ProcessFlags: %08X\n", ProcessFlags);
    MyData.Result = -1;
    rc = RemoteExecute(hProcess, ProcessFlags, RemoteThread, &MyData, sizeof(MyData), TIMEOUT, &dwExitCode);
    printf("RemoteExecute(): %d [ExitCode=%d] [RDATA.Result=%d]\n\n", rc, dwExitCode, MyData.Result);

    ResumeThread(hThread);

    ProcessFlags = GetProcessInfo(dwPID);
    printf("ProcessFlags: %08X\n", ProcessFlags);
    MyData.Result = -1;
    rc = RemoteExecute(hProcess, ProcessFlags, RemoteThread, &MyData, sizeof(MyData), TIMEOUT, &dwExitCode);
    printf("RemoteExecute(): %d [ExitCode=%d] [RDATA.Result=%d]\n\n", rc, dwExitCode, MyData.Result);

    WaitForInputIdle(hProcess, INFINITE);

    ProcessFlags = GetProcessInfo(dwPID);
    printf("ProcessFlags: %08X\n", ProcessFlags);
    MyData.Result = -1;
    rc = RemoteExecute(hProcess, ProcessFlags, RemoteThread, &MyData, sizeof(MyData), TIMEOUT, &dwExitCode);
    printf("RemoteExecute(): %d [ExitCode=%d] [RDATA.Result=%d]\n\n", rc, dwExitCode, MyData.Result);

    printf("End.");
    getch();

    FreeLibrary(hInjLib);
    return 0;
}
