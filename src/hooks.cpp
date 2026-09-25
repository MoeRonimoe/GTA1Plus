#include "hooks.h"

#include <cstring>

namespace hooks
{
    bool WriteMemory(void* dst, const void* src, size_t size)
    {
        DWORD old;
        if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &old))
            return false;
        memcpy(dst, src, size);
        VirtualProtect(dst, size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), dst, size);
        return true;
    }

    bool BytesMatch(uintptr_t addr, const uint8_t* bytes, size_t size)
    {
        return memcmp(reinterpret_cast<const void*>(addr), bytes, size) == 0;
    }

    void* PatchImport(HMODULE module, const char* dll, const char* func, void* replacement)
    {
        auto base = reinterpret_cast<uint8_t*>(module);
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress)
            return nullptr;

        for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp)
        {
            if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll) != 0 || !imp->OriginalFirstThunk)
                continue;

            auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
            auto iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++iat)
            {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                    continue;
                auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (strcmp(reinterpret_cast<char*>(byName->Name), func) != 0)
                    continue;

                void* previous = reinterpret_cast<void*>(iat->u1.Function);
                return WriteMemory(&iat->u1.Function, &replacement, sizeof(replacement)) ? previous : nullptr;
            }
        }
        return nullptr;
    }

    void* FindExport(HMODULE module, const char* name)
    {
        if (!module)
            return nullptr;
        auto base = reinterpret_cast<uint8_t*>(module);
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress)
            return nullptr;

        auto exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
        auto names = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
        auto ordinals = reinterpret_cast<const WORD*>(base + exp->AddressOfNameOrdinals);
        auto functions = reinterpret_cast<const DWORD*>(base + exp->AddressOfFunctions);
        for (DWORD i = 0; i < exp->NumberOfNames; ++i)
        {
            if (strcmp(reinterpret_cast<const char*>(base + names[i]), name) != 0)
                continue;
            const DWORD rva = functions[ordinals[i]];
            if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size)
                return nullptr; // Weiterleitung in eine andere DLL
            return base + rva;
        }
        return nullptr;
    }

    void* PatchVTable(void* object, int index, void* replacement)
    {
        void** vtable = *reinterpret_cast<void***>(object);
        void* previous = vtable[index];
        if (previous != replacement)
            WriteMemory(&vtable[index], &replacement, sizeof(replacement));
        return previous;
    }

    bool RedirectRel32(uintptr_t site, uint8_t opcode, uintptr_t expectedTarget, const void* newTarget)
    {
        auto p = reinterpret_cast<const uint8_t*>(site);
        int32_t rel;
        memcpy(&rel, p + 1, 4);
        if (p[0] != opcode || site + 5 + rel != expectedTarget)
            return false;

        uint8_t patch[5] = { opcode };
        const int32_t newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(newTarget) - (site + 5));
        memcpy(patch + 1, &newRel, 4);
        return WriteMemory(reinterpret_cast<void*>(site), patch, sizeof(patch));
    }

    bool RedirectLoad(uintptr_t site, uintptr_t expectedAddr, void* const* newCell)
    {
        auto p = reinterpret_cast<const uint8_t*>(site);
        uint32_t addr;
        memcpy(&addr, p + 2, 4);
        // 8B /r mit mod=00, rm=101 -> Operand ist eine absolute 32-Bit-Adresse
        if (p[0] != 0x8B || (p[1] & 0xC7) != 0x05 || addr != expectedAddr)
            return false;
        const uint32_t cell = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(newCell));
        return WriteMemory(reinterpret_cast<void*>(site + 2), &cell, 4);
    }
}
