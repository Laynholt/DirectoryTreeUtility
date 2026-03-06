// Adapted from win32-darkmode / PolyHook_2_0 (MIT).
#pragma once

#include <windows.h>
#include <delayimp.h>
#include <cstdint>
#include <cstring>

template <typename T, typename T1, typename T2>
constexpr T RVA2VA(T1 base, T2 rva) {
    return reinterpret_cast<T>(reinterpret_cast<ULONG_PTR>(base) + rva);
}

template <typename T = PIMAGE_DATA_DIRECTORY>
constexpr T DataDirectoryFromModuleBase(void* moduleBase, size_t entryID) {
    auto dosHdr = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    auto ntHdr = RVA2VA<PIMAGE_NT_HEADERS>(moduleBase, dosHdr->e_lfanew);
    auto dataDir = ntHdr->OptionalHeader.DataDirectory;
    return RVA2VA<T>(moduleBase, dataDir[entryID].VirtualAddress);
}

inline PIMAGE_THUNK_DATA FindAddressByName(void* moduleBase, PIMAGE_THUNK_DATA impName, PIMAGE_THUNK_DATA impAddr, const char* funcName) {
    for (; impName->u1.Ordinal; ++impName, ++impAddr) {
        if (IMAGE_SNAP_BY_ORDINAL(impName->u1.Ordinal)) {
            continue;
        }
        auto import = RVA2VA<PIMAGE_IMPORT_BY_NAME>(moduleBase, impName->u1.AddressOfData);
        if (strcmp(reinterpret_cast<const char*>(import->Name), funcName) != 0) {
            continue;
        }
        return impAddr;
    }
    return nullptr;
}

inline PIMAGE_THUNK_DATA FindDelayLoadThunkInModule(void* moduleBase, const char* dllName, const char* funcName) {
    auto imports = DataDirectoryFromModuleBase<PImgDelayDescr>(moduleBase, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    for (; imports->rvaDLLName; ++imports) {
        if ((imports->grAttrs & dlattrRva) == 0) {
            continue;
        }
        if (_stricmp(RVA2VA<const char*>(moduleBase, imports->rvaDLLName), dllName) != 0) {
            continue;
        }
        auto impName = RVA2VA<PIMAGE_THUNK_DATA>(moduleBase, imports->rvaINT);
        auto impAddr = RVA2VA<PIMAGE_THUNK_DATA>(moduleBase, imports->rvaIAT);
        return FindAddressByName(moduleBase, impName, impAddr, funcName);
    }
    return nullptr;
}
