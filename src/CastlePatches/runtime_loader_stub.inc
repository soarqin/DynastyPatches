; Constants for runtime_loader_stub.asm.
; Keep the STUB_* offsets in sync with the RUNTIME_STUB_* enum in main.c.
; The IAT addresses identify the supported RPG.exe image; they belong to the
; game version, not to the launcher or runtime module.
STUB_PATH_OFFSET                EQU 000h
STUB_KERNEL_NAME_OFFSET         EQU 220h
STUB_LOAD_NAME_OFFSET           EQU 240h
STUB_START_NAME_OFFSET          EQU 260h
GET_MODULE_HANDLE_IAT_ADDRESS   EQU 004600C8h
GET_PROC_ADDRESS_IAT_ADDRESS    EQU 00460090h
