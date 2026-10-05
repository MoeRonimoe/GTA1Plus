#include <windows.h>
#include <string>

#include "briefings.h"
#include "camera.h"
#include "config.h"
#include "controls.h"
#include "ddhook.h"
#include "fire.h"
#include "gta1.h"
#include "hooks.h"
#include "log.h"
#include "overlay.h"
#include "rumble.h"
#include "saves.h"
#include "text.h"
#include "tiltcam.h"

namespace
{
    using PeekMessageA_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);

    PeekMessageA_t  s_origPeekMessageA = nullptr;
    PeekMessageA_t* s_iatPeekMessageA = nullptr; // gesetzt, wenn wir über die Ladestellen hängen
    void*           s_peekCell = nullptr;

    // Das Spiel (MGL) holt Tastatur-Ereignisse über PeekMessageA; Menü-Tasten werden hier abgefangen
    BOOL WINAPI Hook_PeekMessageA(LPMSG msg, HWND wnd, UINT filterMin, UINT filterMax, UINT remove)
    {
        // Aktuellen IAT-Eintrag weiterrufen, damit auch der PeekMessageA-Hook von cnc-ddraw läuft
        PeekMessageA_t next = s_iatPeekMessageA ? *s_iatPeekMessageA : s_origPeekMessageA;
        if (next == &Hook_PeekMessageA)
            next = s_origPeekMessageA;

        const BOOL got = next(msg, wnd, filterMin, filterMax, remove);
        // Die umgebogenen Aufrufe stammen alle aus den Nachrichtenschleifen von GTA.exe (Haupt-Thread).
        // Schon im Hauptmenü vor dem ersten Level (noch kein Flip) den Spiel-Thread festhalten.
        if (!ddhook::GameThreadId() && s_iatPeekMessageA)
            ddhook::NoteGameThread();
        if (GetCurrentThreadId() == ddhook::GameThreadId())
        {
            const bool removed = (remove & PM_REMOVE) != 0;
            if (got && msg && removed)
                controls::OnMessage(*msg);
            if (got && msg && overlay::FilterMessage(*msg, removed))
                msg->message = WM_NULL;
            overlay::OnPump();
            controls::OnPump();
            ddhook::OnPump();
        }
        return got;
    }

    void OnHudDrawn()
    {
        ddhook::NoteGameThread();
        ddhook::EnsureFlipHook(gta1::PrimarySurface(), gta1::PageCount());
    }

    // Spielfigur passend zum Portrait aus dem Hauptmenü einfärben. Nur die Originalfarbe des Charakters
    // wird ersetzt, eigene Farben von Missionen (z.B. Verkleidungen) bleiben erhalten. Im Mehrspielermodus
    // unterscheiden die Farben die Spieler, dort bleibt alles wie im Original.
    void ApplyPlayerColor()
    {
        if (!g_cfg.portraitColors || !gta1::IsSinglePlayer())
            return;
        const int character = gta1::PlayerCharacter();
        const int original = gta1::PlayerDefaultRemap();
        int remap = 0;
        if (character < 0 || character > 7 || original < 0 || !gta1::GetPlayerPedRemap(remap))
            return;
        const int want = g_cfg.playerColors[character];
        if (remap == original && want > 0 && want <= 63 && want != original)
            gta1::SetPlayerPedRemap(want);
    }

    // Debug: <asi>.burn anlegen -> Spielfigur anzünden, <asi>.douse -> löschen (zum Testen im Spiel)
    std::wstring g_burnRequest, g_douseRequest, g_rumbleRequest;
    DWORD g_debugCheck = 0;

    void CheckDebugRequests()
    {
        const DWORD now = GetTickCount();
        if (g_burnRequest.empty() || now - g_debugCheck < 250)
            return;
        g_debugCheck = now;
        if (GetFileAttributesW(g_burnRequest.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileW(g_burnRequest.c_str());
            LOG("Debug: Spielfigur anzünden: %s", gta1::IgnitePlayer() ? "ok" : "nicht möglich");
        }
        if (GetFileAttributesW(g_douseRequest.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileW(g_douseRequest.c_str());
            LOG("Debug: Spielfigur löschen: %s", gta1::ExtinguishPlayer() ? "ok" : "brennt nicht");
        }
        // <asi>.rumble: Controller eine Sekunde lang vibrieren lassen
        if (GetFileAttributesW(g_rumbleRequest.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileW(g_rumbleRequest.c_str());
            rumble::Test(1000);
        }
    }

    void OnPreRender()
    {
        controls::BeforeRender();
        fire::BeforeRender(); // nach der Twin-Stick-Blickrichtung, das Wälzen hat Vorrang
        ApplyPlayerColor();
        CheckDebugRequests();
        saves::Tick();
    }

    // Aus dem Missionsskript: Ansagen für die Briefings, geschaffte Missionen für das Autosave
    void OnScriptEvent(gta1::ScriptEvent event, int value)
    {
        switch (event)
        {
        case gta1::ScriptEvent::Brief:
            briefings::OnBrief(value);
            break;
        case gta1::ScriptEvent::MissionEnd:
            saves::OnMissionEnd();
            break;
        }
    }

    // Die Tonbibliothek des Spiels (Miles, mss32.dll) bindet DirectSound an "das Fenster des Programms": das aktive
    // Fenster oder, wenn gerade keins aktiv ist, das oberste Fenster des Prozesses. DirectInput legt für den
    // Controller ein verstecktes Fenster an (DIEmWin), das dabei vor dem Spielfenster liegen kann - der Ton bliebe
    // dann stumm, weil dieses Fenster nie im Vordergrund ist. Miles bekommt deshalb als oberstes Fenster immer das
    // sichtbare Fenster des Spiels.
    using GetTopWindow_t = HWND(WINAPI*)(HWND);
    GetTopWindow_t s_origGetTopWindow = nullptr;

    BOOL CALLBACK FindVisibleWindow(HWND wnd, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(wnd, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(wnd))
            return TRUE;
        *reinterpret_cast<HWND*>(out) = wnd;
        return FALSE;
    }

    HWND WINAPI Hook_GetTopWindow(HWND parent)
    {
        if (!parent)
        {
            HWND game = nullptr;
            EnumWindows(&FindVisibleWindow, reinterpret_cast<LPARAM>(&game));
            if (game)
                return game;
        }
        return s_origGetTopWindow(parent);
    }

    void KeepSoundOnGameWindow()
    {
        const HMODULE miles = GetModuleHandleW(L"mss32.dll");
        if (!miles)
            return;
        s_origGetTopWindow = reinterpret_cast<GetTopWindow_t>(
            hooks::PatchImport(miles, "USER32.dll", "GetTopWindow", reinterpret_cast<void*>(&Hook_GetTopWindow)));
        if (!s_origGetTopWindow)
            LOG("Ton: Fenstersuche der Tonbibliothek nicht gefunden - mit Controller kann der Ton stumm bleiben");
    }

    std::wstring SiblingPath(HMODULE self, const wchar_t* ext)
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(self, path, MAX_PATH);
        std::wstring p = path;
        const size_t dot = p.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > p.find_last_of(L"\\/"))
            p.resize(dot);
        return p + ext;
    }
}

void ModInit(HMODULE self)
{
    LoadConfig(SiblingPath(self, L".ini").c_str());
    g_burnRequest = SiblingPath(self, L".burn");
    g_douseRequest = SiblingPath(self, L".douse");
    g_rumbleRequest = SiblingPath(self, L".rumble");
    if (g_cfg.log)
        logx::Open(SiblingPath(self, L".log").c_str());

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    LOG("GTA1Plus geladen, Spiel: %ls", exePath);
    text::Init(SiblingPath(self, L"_lang.ini").c_str(), g_cfg.language);

    KeepSoundOnGameWindow();

    const HMODULE exe = GetModuleHandleW(nullptr);
    if (gta1::Init(exe))
    {
        // cnc-ddraw ersetzt beim Laden die IAT-Einträge von GetProcAddress/PeekMessageA durch eigene Hooks.
        // Darum lesen die betroffenen Befehle in GTA.exe ab jetzt aus unseren eigenen Zellen.
        const gta1::ImportLoads gpa = gta1::GetProcAddressLoads();
        ddhook::InstallAtLoads(gpa.iatSlot, gpa.sites, gpa.count);
        const gta1::CallCell create = gta1::DirectDrawCreateCall();
        if (create.site && !ddhook::InstallAtCall(create.site, create.cell))
            LOG("DirectDrawCreate: Aufrufstelle passt nicht - kein Zwischenpuffer, Hauptmenü bleibt klein");

        const gta1::ImportLoads peek = gta1::PeekMessageLoads();
        s_iatPeekMessageA = reinterpret_cast<PeekMessageA_t*>(peek.iatSlot);
        s_origPeekMessageA = *s_iatPeekMessageA;
        s_peekCell = reinterpret_cast<void*>(&Hook_PeekMessageA);
        size_t ok = 0;
        for (size_t i = 0; i < peek.count; ++i)
            ok += hooks::RedirectLoad(peek.sites[i], peek.iatSlot, &s_peekCell);
        LOG("PeekMessageA: %u von %u Ladestellen umgebogen", (unsigned)ok, (unsigned)peek.count);

        gta1::SetHudCallback(&OnHudDrawn);
        gta1::SetPreRenderCallback(&OnPreRender);
        gta1::SetBeforeHudCallback(&tiltcam::Warp);
        if (!gta1::SetCameraOverride(&tiltcam::BeginFrame))
            LOG("Kippkamera: Spielcode passt nicht");
        int vehicleHeight, vehicleZoom;
        VehicleCameraView(g_cfg.vehicleView, vehicleHeight, vehicleZoom);
        if (!gta1::SetViewOptions(g_cfg.trafficRange / 100.0f, vehicleHeight, vehicleZoom))
            LOG("Sichtbereich und Fahrzeugkamera: Spielcode passt nicht");
        gta1::SetDamage(g_cfg.pistolDamage, g_cfg.machineGunDamage, g_cfg.punchDamage);
        LOG("Schaden statt sofortigem Tod (Pistole %d %%, Maschinenpistole %d %%, Faust %d %%): %s", g_cfg.pistolDamage,
            g_cfg.machineGunDamage, g_cfg.punchDamage, gta1::EnableDamage() ? "aktiv" : "Spielcode passt nicht");
        if (g_cfg.keepMultiplier)
            LOG("Multiplikator bei Verhaftung behalten: %s", gta1::EnableKeepMultiplier() ? "aktiv" : "Spielcode passt nicht");
        if (g_cfg.allCharacters)
            LOG("Alle Charaktere wählbar: %s", gta1::EnableAllCharacters() ? "aktiv" : "Spielcode passt nicht");
        if (g_cfg.quickRetry)
            LOG("Schneller neuer Versuch nach Fehlschlag: %s", gta1::EnableQuickRetry() ? "aktiv" : "Spielcode passt nicht");
        saves::Init(SiblingPath(self, L"").c_str());
        LOG("Briefings und Spielstände: %s", gta1::SetScriptEventCallback(&OnScriptEvent) ? "aktiv" : "Spielcode passt nicht");
        controls::Init(); // auch für die Maus im Pausemenü
        rumble::Init();
        fire::Init();
        // Immer einhängen: Steuerung und Controller lassen sich im Optionsmenü während des Spiels umschalten
        gta1::SetInputFilter(&controls::ProcessPacket);
        if (g_cfg.cameraSteady)
            LOG("Ruhigere Kamera: %s", gta1::EnableSteadyCamera() ? "aktiv" : "Spielcode passt nicht");
        if ((g_cfg.twinStick || g_cfg.controller) && g_cfg.camera)
            LOG("Kamerasteuerung: %s", gta1::SetCameraFilter(&camera::Filter) ? "aktiv" : "Spielcode passt nicht");
    }
    else
    {
        ddhook::Install(exe);
        s_origPeekMessageA = reinterpret_cast<PeekMessageA_t>(
            hooks::PatchImport(exe, "USER32.dll", "PeekMessageA", reinterpret_cast<void*>(&Hook_PeekMessageA)));
        if (!s_origPeekMessageA)
            LOG("PeekMessageA-Import nicht gefunden - keine Tasteneingaben für das Menü");
    }

    overlay::Init(SiblingPath(self, L"").c_str());
}
