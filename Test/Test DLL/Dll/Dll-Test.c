#include <windows.h>

#define  TITLE  TEXT("Test Dll")

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
             OutputDebugString(TEXT("Dll Process Attach"));
             break;

        case DLL_PROCESS_DETACH:
             OutputDebugString(TEXT("Dll Process Detach"));
             break;

        case DLL_THREAD_ATTACH:
             OutputDebugString(TEXT("Dll Thread Attach"));
             break;

        case DLL_THREAD_DETACH:
             OutputDebugString(TEXT("Dll Thread Detach"));
             break;
    }
    return TRUE;
}
