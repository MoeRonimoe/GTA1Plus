// GTA1Plus-Loader: gibt sich als dsound.dll aus, reicht DirectSound unverändert an Windows weiter und lädt
// beim Start alle scripts\*.asi. Anders als der Ultimate ASI Loader hängt er sich in keine Windows-Funktion;
// so verträgt er sich mit der Musik-DLL des R2P-Pakets (winmm.dll von DxWnd, spielt die CD-Musik als OGG).
// Das Spiel lädt dsound.dll früh über Miles Sound (mss32.dll), noch bevor es DirectDraw anlegt.
#include <windows.h>
#include <cwchar>
#include <string>

extern "C" FARPROC g_dsound[12] = {};

namespace
{
    const char* const kNames[12] = {
        "DirectSoundCreate", "DirectSoundEnumerateA", "DirectSoundEnumerateW", "DllCanUnloadNow",
        "DllGetClassObject", "DirectSoundCaptureCreate", "DirectSoundCaptureEnumerateA",
        "DirectSoundCaptureEnumerateW", "GetDeviceID", "DirectSoundFullDuplexCreate", "DirectSoundCreate8",
        "DirectSoundCaptureCreate8",
    };

    void LoadSystemDsound()
    {
        wchar_t path[MAX_PATH];
        const UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (!n || n >= MAX_PATH - 12)
            return;
        wcscat_s(path, L"\\dsound.dll");
        if (HMODULE m = LoadLibraryW(path))
            for (int i = 0; i < 12; ++i)
                g_dsound[i] = GetProcAddress(m, kNames[i]);
    }

    bool EndsWithAsi(const wchar_t* name)
    {
        const size_t len = wcslen(name);
        return len > 4 && _wcsicmp(name + len - 4, L".asi") == 0;
    }

    void LoadPlugins(HMODULE self)
    {
        wchar_t dir[MAX_PATH];
        if (!GetModuleFileNameW(self, dir, MAX_PATH))
            return;
        if (wchar_t* slash = wcsrchr(dir, L'\\'))
            slash[1] = 0;
        const std::wstring scripts = std::wstring(dir) + L"scripts\\";
        WIN32_FIND_DATAW fd;
        const HANDLE find = FindFirstFileW((scripts + L"*.asi").c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE)
            return;
        do
        {
            // *.asi passt über die kurzen 8.3-Namen auch auf längere Endungen, darum genau prüfen
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && EndsWithAsi(fd.cFileName))
                LoadLibraryW((scripts + fd.cFileName).c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
}

// Weiterleitungen: springen mit unverändertem Stack in die echte Funktion
#define FORWARD(index, name) \
    extern "C" __declspec(naked) void Fwd_##name() { __asm { jmp dword ptr [g_dsound + index * 4] } }

FORWARD(0, DirectSoundCreate)
FORWARD(1, DirectSoundEnumerateA)
FORWARD(2, DirectSoundEnumerateW)
FORWARD(3, DllCanUnloadNow)
FORWARD(4, DllGetClassObject)
FORWARD(5, DirectSoundCaptureCreate)
FORWARD(6, DirectSoundCaptureEnumerateA)
FORWARD(7, DirectSoundCaptureEnumerateW)
FORWARD(8, GetDeviceID)
FORWARD(9, DirectSoundFullDuplexCreate)
FORWARD(10, DirectSoundCreate8)
FORWARD(11, DirectSoundCaptureCreate8)

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(self);
        LoadSystemDsound();
        LoadPlugins(self);
    }
    return TRUE;
}
