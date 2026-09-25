#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace hooks
{
    bool WriteMemory(void* dst, const void* src, size_t size);
    bool BytesMatch(uintptr_t addr, const uint8_t* bytes, size_t size);

    // Ersetzt einen Eintrag in der Import-Tabelle (IAT) eines Moduls; liefert den bisherigen Zeiger
    void* PatchImport(HMODULE module, const char* dll, const char* func, void* replacement);

    // Adresse einer Funktion direkt aus der Exporttabelle eines Moduls. Umgeht IAT-Hooks, die andere
    // (z.B. cnc-ddraw mit hook=4) auch in unserer ASI setzen. Weiterleitungen liefern nullptr.
    void* FindExport(HMODULE module, const char* name);

    // Ersetzt einen Eintrag der VTable eines COM-Objekts (gilt für alle Objekte dieser Klasse)
    void* PatchVTable(void* object, int index, void* replacement);

    // Biegt ein 5-Byte "call rel32" (E8) bzw. "jmp rel32" (E9) um, wenn es auf expectedTarget zeigt
    bool RedirectRel32(uintptr_t site, uint8_t opcode, uintptr_t expectedTarget, const void* newTarget);

    // Biegt "mov reg, [expectedAddr]" (8B /r mit absoluter Adresse) auf eine eigene Speicherzelle um
    bool RedirectLoad(uintptr_t site, uintptr_t expectedAddr, void* const* newCell);
}
