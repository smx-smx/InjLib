// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
// ;; Stub.asm                                      ;;
// ;; Assembly code used for remote code injection. ;;
// ;;                                               ;;
// ;; (c) A. Miguel Feijao, 13/7/2005               ;;
// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
// 
// ;.386
// ;.model flat, stdcall
// ;option casemap :none
// 
// ;include \masm32\include\windows.inc
.intel_syntax noprefix

// ;; Modified EXCEPTION_REGISTRATION
E_FRAME  STRUC
         NextFrame   DD  ?   ; Pointer or -1 for last member
         OurHandler  DD  ?   ; Pointer to our SEH handler
         SafeExit    DD  ?   ; Pointer to our safe exit (Cleanup)
         SafeEBP     DD  ?   ; Safe EBP
E_FRAME  ENDS

// ;; RDATA (from Inject.h)
RDATA   STRUC
        sSize                   DD      ?       ; Size of structure
        hProcess                DD      ?       ; Process handle
        ProcessFlags            DD      ?       ; Process flags
        dwTimeout               DD      ?       ; Timeout
        hWnd                    DD      ?       ; Window handle
        pRDATA                  DD      ?       ; Pointer to RDATA structure
        pfnStubWndProc          DD      ?       ; Address of stub window handler
        pfnUserWndProc          DD      ?       ; Address of user's window procedure handler
        pfnOldWndProc           DD      ?       ; Address of old window handler
        Result                  DD      ?       ; Result from user's window procedure handler
        pfnSetWindowLong        DD      ?       ; Address of SetWindowLong()
        pfnCallWindowProc       DD      ?       ; Address of CallWindowProc()
RDATA   ENDS

// ;;  OFFSETS (from Inject.h)
// ;; Filled by GetOffsets()
OFFSETS  STRUC
         ; Stub() data
         StubStart             DD  ?
         StubSize              DD  ?
         PUserFunc             DD  ?
         PLdrShutdownThread    DD  ?
         PNtFreeVirtualMemory  DD  ?
         PNtTerminateThread    DD  ?
         PNative               DD  ?
         PFinished             DD  ?
         ; StubWndProc() data
         StubWndProcStart      DD  ?
         StubWndProcSize       DD  ?
         pRDATA                DD  ?
OFFSETS  ENDS

.const
// ;; (from Inject.h)
REMOTE_EXCEPTION equ 10000000h                  ; Indicates that an exception ocurred

.code

// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
// ;; All the code between Code1Start and Code1End ;;
// ;; will be injected into the remote process.    ;;
// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Code1Start EQU $

// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
// ;; Stub()                                     ;;
// ;; - Sets an SEH frame                        ;;
// ;; - Calls UserFunc()                         ;;
// ;; - Handle special exit for native process   ;;
// ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
Stub proc pParams:DWORD
        assume fs: nothing

        nop
        nop

        ; Save registers
        push    ebx
        push    esi
        push    edi

        ; Make code relocatable
        ; EAX = ExceptionHandler()
        ; ESI = Cleanup
        ; EDI = Finished
        call    $+5
Reloc1: pop     eax
        mov     esi, eax
        mov     edi, eax
        add     eax, offset ExceptionHandler1 - offset Reloc1
        add     esi, offset Cleanup - offset Reloc1
        add     edi, offset Finished - offset Reloc1

        ; Set new SEH frame
        push    ebp                             ; EBP at safe-place
        push    esi                             ; Addr. for safe-place (Cleanup)
        push    eax                             ; Addr. of handler function
        push    dword ptr fs:[0]                ; Addr. of previous handler
        mov     fs:[0], esp                     ; Install new SEH frame

        ; Finished byte indicates when UserFunc() has finished
        mov     dword ptr [edi], 0              ; Finished = FALSE

        ; Call UserFunc()
        push    pParams
PatchUserFunc   EQU $+1                         ; Patched at runtime
        mov     eax, 0                          ; UserFunc()
        call    eax                             ; Function return EAX=ExitCode

Cleanup:
        ; Remove SEH frame
        pop     dword ptr fs:[0]
        add     esp, 3*4

        ; Make code relocatable
        ; ESI = Native
        ; EDI = Finished
        call    $+5
Reloc2: pop     edi
        mov     esi, edi

        add     edi, offset Finished - offset Reloc2
        mov     dword ptr [edi], 1              ; Finished = TRUE

        add     esi, offset Native - offset Reloc2
        cmp     dword ptr [esi], 0              ; Native process (<> 0) ?

        ; Restore registers
        pop     edi
        pop     esi
        pop     ebx

        je      NormalExit

        ; Native process needs a different exit
        call    NativeExit                      ; Yes, go to NativeExit
NormalExit:
        ret                                     ; No, go to NormalExit
Stub endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ExceptionHandler1()                        ;;
;; Called by the OS when an exception occurs. ;;
;; Return Exception code and resume execution ;;
;; at Cleanup label.                          ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; EXCEPTION_DISPOSITION __cdecl ExceptionHandler(
;          struct _EXCEPTION_RECORD *ExceptionRecord,
;          void                     *EstablisherFrame,
;          struct _CONTEXT          *ContextRecord,
;          void                     *DispatcherContext);

ExceptionHandler1 proc C pExcept:DWORD, pFrame:DWORD, pContext:DWORD, pDispatch:DWORD
        ; Save used registers
        push    ebx
        push    ecx

        mov     eax, pContext                   ; ContextRecord
        assume  eax: ptr CONTEXT
        mov     ebx, pExcept                    ; ExceptionRecord
        assume  ebx: ptr EXCEPTION_RECORD
        mov     ebx, [ebx].ExceptionCode        ; ExceptionRecord->ExceptionCode
        or      ebx, REMOTE_EXCEPTION           ; ExceptionCode | REMOTE_EXCEPTION
        mov     [eax].regEax, ebx               ; ContextRecord->cx_Eax = ExceptionCode
        assume  ebx: ptr E_FRAME
        mov     ebx, pFrame                     ; EstablisherFrame
        mov     ecx, [ebx].SafeExit             ; Cleanup
        mov     [eax].regEip, ecx               ; ContextRecord->cx_Eip = Cleanup
        mov     ecx, [ebx].SafeEBP              ; Safe-EBP
        mov     [eax].regEbp, ecx               ; ContextRecord->cx_Ebp = Safe-EBP
        mov     [eax].regEsp, ebx               ; ContextRecord->cx_Esp = Current SEH frame
        mov     eax, ExceptionContinueExecution ; return ExceptionContinueExecution

        ; Restore used registers
        assume  eax: nothing
        assume  ebx: nothing
        pop     ecx
        pop     ebx
        ret
ExceptionHandler1  endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; NativeExit()                                                           ;;
;; This is the Kernel32 ExitThread() equivalent but for native processes. ;;
;; - Calls LdrShutdownThread()                                            ;;
;; - Get current stack                                                    ;;
;; - Switch to a temporary stack (within the TEB)                         ;;
;; - Free stack using NtFreeVirtualMemory()                               ;;
;; - Terminate thread using NtTerminateThread()                           ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
NativeExit proc
        mov     esi, eax                        ; dwExitCode

        ; Call LdrShutdownThread
PatchLdrShutdownThread   EQU $+1                ; Patched at runtime
        mov     eax, 0                          ; LdrShutdownThread()
        call    eax

        ; Get thread stack addr.
        mov     eax, fs:[018h]                  ; TEB
        add     eax, 0E0Ch
        mov     ecx, [eax]                      ; TEB.DeallocationStack

        ; Change to a temp. stack
        ; (TEB.UserReserved[] area should be used ?!)
        mov     esp, eax
        mov     ebp, esp

        ; Free thread stack
        push    0                               ; dwRegionSize = 0
        push    ecx                             ; lpAddress = TEB.DeallocationStack
        push    MEM_RELEASE                     ; FreeType = MEM_RELEASE
        lea     eax, [ebp-4]
        push    eax                             ; &dwRegionSize
        lea     eax, [ebp]
        push    eax                             ; &lpAddress
        push    0FFFFFFFFh                      ; hProcess = GetCurrentProcess()
PatchNtFreeVirtualMemory   EQU $+1              ; Patched at runtime
        mov     eax, 0                          ; NtFreeVirtualMemory()
        call    eax

        ; Adjust stack (not really needed)
        add     esp, 8

        ; Terminate thread
        push    esi                             ; dwExitStatus
        push    0FFFFFFFEh                      ; hThread = GetCurrentThread()
PatchNtTerminateThread   EQU $+1                ; Patched at runtime
        mov     eax, 0                          ; NtTerminateThread()
        call    eax
        jmp     $                               ; Should never reach here ?!?
NativeExit      endp

Native    dd    0                               ; <> 0 for native process
Finished  dd    0                               ; <> 0 after UserFunc() finished

Code1End   EQU $

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

Code2Start EQU $

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; StubWndProc()                              ;;
;; - Sets an SEH frame                        ;;
;; - Calls UserWndProc()                      ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
StubWndProc proc STDCALL hWnd: DWORD, Message: DWORD, wParam: DWORD, lParam:DWORD
        assume fs: nothing
        ; Save registers
        push    ebx
        push    esi
        push    edi

        ; Make code relocatable
        ; EAX = ExceptionHandler()
        ; ESI = Cleanup
        call    $+5
Reloc:  pop     eax
        mov     esi, eax
        add     eax, offset ExceptionHandler2 - offset Reloc
        add     esi, offset Cleanup - offset Reloc

        ; Set new SEH frame
        push    ebp                             ; EBP at safe-place
        push    esi                             ; Addr. for safe-place (Cleanup)
        push    eax                             ; Addr. of handler function
        push    dword ptr fs:[0]                ; Addr. of previous handler
        mov     fs:[0], esp                     ; Install new SEH frame

        ; Call UserWndProc()
PatchRDATA   EQU $+1                            ; Patched at runtime
        assume  esi: ptr RDATA
        mov     esi, 0                          ; RDATA ptr
        cmp     esi, 0                          ; Paranoid check !
        mov     eax, 0                          ; Return FALSE
        je      Cleanup                         ; if pData == NULL cannot do anything !

        push    esi                             ; Save RDATA ptr

        push    lParam
        push    wParam
        push    Message
        push    hWnd
        push    esi
        call    [esi].pfnUserWndProc            ; pData->pfnUserWndProc(pData, hWnd, Message, wParam, lParam)

        pop     esi                             ; Retreive RDATA ptr

        cmp     eax, TRUE                       ; FALSE => return pData->pfnCallWindowProc()
        jne     __FALSE

        mov     eax, [esi].Result               ; TRUE => return pData->Result
        jmp     Cleanup

__FALSE:
        push    lParam
        push    wParam
        push    Message
        push    hWnd
        push    [esi].pfnOldWndProc
        call    [esi].pfnCallWindowProc         ; pData->pfnCallWindowProc(pData->pfnOldWndProc, hWnd, Message, wParam, lParam)

Cleanup:
        ; Remove SEH frame
        pop     dword ptr fs:[0]
        add     esp, 3*4

        ; Restore registers
        pop     edi
        pop     esi
        pop     ebx

        ret
StubWndProc endp


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ExceptionHandler2()                        ;;
;; Called by the OS when an exception occurs. ;;
;; Return FALSE and resume execution at       ;;
;; Cleanup label.                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
; EXCEPTION_DISPOSITION __cdecl ExceptionHandler(
;          struct _EXCEPTION_RECORD *ExceptionRecord,
;          void                     *EstablisherFrame,
;          struct _CONTEXT          *ContextRecord,
;          void                     *DispatcherContext);

ExceptionHandler2 proc C pExcept: DWORD, pFrame: DWORD, pContext: DWORD, pDispatch: DWORD
        ; Save used registers
        push    ebx
        push    ecx

        mov     eax, pContext                   ; ContextRecord
        assume  eax: ptr CONTEXT
        mov     [eax].regEax, 0                 ; ContextRecord->cx_Eax = 0 (FALSE)
        assume  ebx: ptr E_FRAME
        mov     ebx, pFrame                     ; EstablisherFrame
        mov     ecx, [ebx].SafeExit             ; Cleanup
        mov     [eax].regEip, ecx               ; ContextRecord->cx_Eip = Cleanup
        mov     ecx, [ebx].SafeEBP              ; Safe-EBP
        mov     [eax].regEbp, ecx               ; ContextRecord->cx_Ebp = Safe-EBP
        mov     [eax].regEsp, ebx               ; ContextRecord->cx_Esp = Current SEH frame
        mov     eax, ExceptionContinueExecution ; return ExceptionContinueExecution

        ; Restore used registers
        assume  eax: nothing
        assume  ebx: nothing
        pop     ecx
        pop     ebx
        ret
ExceptionHandler2  endp

Code2End   EQU $

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; GetOffsets()                                  ;;
;; This function return the code start and       ;;
;; size and offsets within the assembly code     ;;
;; necessary to patch some addresses at runtime. ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
GetOffsets proc ofs:DWORD
        mov     esi, ofs
        assume  esi: ptr OFFSETS
        mov     [esi].StubStart, Code1Start
        mov     [esi].StubSize, Code1End - Code1Start
        mov     [esi].PUserFunc, PatchUserFunc - Code1Start
        mov     [esi].PLdrShutdownThread, PatchLdrShutdownThread - Code1Start
        mov     [esi].PNtFreeVirtualMemory, PatchNtFreeVirtualMemory - Code1Start
        mov     [esi].PNtTerminateThread, PatchNtTerminateThread - Code1Start
        mov     [esi].PNative, offset Native - Code1Start
        mov     [esi].PFinished, offset Finished - Code1Start

        mov     [esi].StubWndProcStart, Code2Start
        mov     [esi].StubWndProcSize, Code2End - Code2Start
        mov     [esi].pRDATA, PatchRDATA - Code2Start
        ret
GetOffsets endp

end
