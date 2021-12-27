#ifndef __ENUM_H__
#define __ENUM_H__

// EnumProcesses() Callback function prototype
typedef BOOL (CALLBACK * PFNENUMPROC)(DWORD dwProcessId, LPCTSTR pszProcessName, PDWORD pdwPID, LPCTSTR pszName);

BOOL _EnumProcesses(PFNENUMPROC pfnEnumProc, LPCTSTR pszName, PDWORD pdwPID);
DWORD GetPIDFromName(LPCTSTR szProcessName);

#endif // __ENUM_H__