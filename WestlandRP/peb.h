#pragma once

#include <stdio.h>
#include <windows.h>

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _PEB_LDR_DATA {
    ULONG      Length;
    BOOLEAN    Initialized;
    BYTE       Reserved[3];
    LIST_ENTRY InLoadOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY     InLoadOrderLinks;
    LIST_ENTRY     InMemoryOrderLinks;
    LIST_ENTRY     InInitializationOrderLinks;
    PVOID          DllBase;
    PVOID          EntryPoint;
    ULONG          SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _RTL_USER_PROCESS_PARAMETERS {
    BYTE           Reserved1[16];
    PVOID          Reserved2[10];
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB {
    BYTE                         InheritedAddressSpace;
    BYTE                         ReadImageFileExecOptions;
    BYTE                         BeingDebugged;
    BYTE                         BitField;
    HANDLE                       Mutant;
    PVOID                        ImageBaseAddress;
    PPEB_LDR_DATA                Ldr;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
} PEB, *PPEB;

inline PPEB GetPeb()
{
#ifdef _WIN64
    return reinterpret_cast<PPEB>(__readgsqword(0x60));
#else
    return reinterpret_cast<PPEB>(__readfsdword(0x30));
#endif
}

inline bool IsReadableMemory(const void* address, size_t size)
{
    if (!address || size == 0)
        return false;

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) == 0)
        return false;

    if (info.State != MEM_COMMIT)
        return false;

    const DWORD protect = info.Protect & 0xFF;
    if (protect == PAGE_NOACCESS || protect == PAGE_GUARD)
        return false;

    const uintptr_t regionStart = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t regionEnd = regionStart + info.RegionSize;
    const uintptr_t requestStart = reinterpret_cast<uintptr_t>(address);
    const uintptr_t requestEnd = requestStart + size;

    return requestStart >= regionStart && requestEnd <= regionEnd;
}

inline bool CanWriteUnicodeString(const UNICODE_STRING* str, size_t textLen)
{
    if (!str || !str->Buffer || str->MaximumLength < sizeof(wchar_t))
        return false;

    if (textLen + 1 > str->MaximumLength / sizeof(wchar_t))
        return false;

    return IsReadableMemory(str, sizeof(UNICODE_STRING))
        && IsReadableMemory(str->Buffer, str->MaximumLength);
}

inline bool WriteUnicodeString(UNICODE_STRING* str, const wchar_t* text)
{
    if (!str || !text || !text[0])
        return false;

    const size_t textLen = wcslen(text);
    if (!CanWriteUnicodeString(str, textLen))
        return false;

    wcsncpy_s(str->Buffer, str->MaximumLength / sizeof(wchar_t), text, _TRUNCATE);
    str->Length = static_cast<USHORT>(textLen * sizeof(wchar_t));
    return true;
}

inline bool BuildBrandedExePath(wchar_t* path, size_t pathCapacity, const wchar_t* displayName)
{
    if (!path || pathCapacity == 0 || !displayName || !displayName[0])
        return false;

    if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(pathCapacity)) == 0)
        return false;

    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash)
        return false;

    const size_t prefixLen = static_cast<size_t>(slash - path + 1);
    const int written = swprintf_s(path + prefixLen, pathCapacity - prefixLen, L"%s.exe", displayName);
    return written > 0;
}

inline bool TrySetMainModuleNames(const wchar_t* fullPath, const wchar_t* baseName)
{
    __try
    {
        PPEB peb = GetPeb();
        if (!peb || !peb->Ldr || !IsReadableMemory(peb->Ldr, sizeof(PEB_LDR_DATA)))
            return false;

        PLIST_ENTRY head = &peb->Ldr->InLoadOrderModuleList;
        if (!head->Flink || head->Flink == head)
            return false;

        PLDR_DATA_TABLE_ENTRY module =
            CONTAINING_RECORD(head->Flink, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if (!IsReadableMemory(module, sizeof(LDR_DATA_TABLE_ENTRY)))
            return false;

        return WriteUnicodeString(&module->FullDllName, fullPath)
            && WriteUnicodeString(&module->BaseDllName, baseName);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

inline bool ApplyProcessBranding(const wchar_t* displayName)
{
    if (!displayName || !displayName[0])
        return false;

    wchar_t brandedPath[MAX_PATH]{};
    if (!BuildBrandedExePath(brandedPath, MAX_PATH, displayName))
        return false;

    wchar_t baseName[MAX_PATH]{};
    swprintf_s(baseName, L"%s.exe", displayName);

    bool updated = false;

    __try
    {
        PPEB peb = GetPeb();
        if (!peb || !peb->ProcessParameters)
            return false;

        if (!IsReadableMemory(peb->ProcessParameters, sizeof(RTL_USER_PROCESS_PARAMETERS)))
            return false;

        PRTL_USER_PROCESS_PARAMETERS params = peb->ProcessParameters;

        if (WriteUnicodeString(&params->ImagePathName, brandedPath))
            updated = true;

        if (WriteUnicodeString(&params->CommandLine, brandedPath))
            updated = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        updated = false;
    }

    if (TrySetMainModuleNames(brandedPath, baseName))
        updated = true;

    return updated;
}
