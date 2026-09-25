#include <Windows.h>

void ModInit(HMODULE self);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        // Nur Hooks setzen – gezeichnet wird später im Spiel-Thread, direkt vor jedem Flip
        ModInit(hModule);
    }
    return TRUE;
}
