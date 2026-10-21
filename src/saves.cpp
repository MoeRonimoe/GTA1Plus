#include "saves.h"
#include "config.h"
#include "gta1.h"
#include "log.h"
#include "text.h"

#include <windows.h>
#include <cstring>
#include <cwchar>
#include <vector>

namespace saves
{
    namespace
    {
        constexpr uint32_t kMagic = 0x53503147;    // "G1PS"
        constexpr uint32_t kVersion = 4;   // 2: mit Trigger-Tabelle, 3: mit dem Schalter der Tore, 4: mit den Pfeilen
        constexpr DWORD kAutosaveDelay = 2500;      // nach MISSION_END, bis Belohnung und Ansage durch sind
        constexpr DWORD kOfferDelay = 3000;         // frühestens so lange nach Levelstart anbieten

        std::wstring g_dir;
        DWORD    g_autosaveAt = 0;                  // 0 = kein Autosave fällig
        unsigned g_autosaveLevel = 0;
        unsigned g_level = ~0u;                     // zuletzt gesehener Levelstart (gta1::LevelLoads)
        DWORD    g_levelSince = 0;
        unsigned g_offered = ~0u;                   // Levelstart, für den das Laden schon angeboten wurde
        std::wstring g_notice;
        bool     g_noticeError = false;

        struct Header
        {
            uint32_t magic = 0, version = 0;
            FILETIME saved = {};
        };

        struct Writer
        {
            std::vector<uint8_t> buf;
            template <class T> void Put(const T& v)
            {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                buf.insert(buf.end(), p, p + sizeof(T));
            }
            void Blob(const std::vector<uint8_t>& b)
            {
                Put(static_cast<uint32_t>(b.size()));
                buf.insert(buf.end(), b.begin(), b.end());
            }
        };

        struct Reader
        {
            const uint8_t* p;
            const uint8_t* end;
            bool ok = true;
            template <class T> void Get(T& v)
            {
                if (!ok || end - p < (ptrdiff_t)sizeof(T))
                {
                    ok = false;
                    return;
                }
                memcpy(&v, p, sizeof(T));
                p += sizeof(T);
            }
            void Blob(std::vector<uint8_t>& b)
            {
                uint32_t n = 0;
                Get(n);
                if (!ok || n > (uint32_t)(end - p))
                {
                    ok = false;
                    return;
                }
                b.assign(p, p + n);
                p += n;
            }
        };

        std::vector<uint8_t> Serialize(const gta1::GameState& s)
        {
            Writer w;
            Header h;
            h.magic = kMagic;
            h.version = kVersion;
            GetSystemTimeAsFileTime(&h.saved);
            w.Put(h.magic);
            w.Put(h.version);
            w.Put(h.saved);
            w.Put(s.exeStamp);
            w.Put(s.levelHash);
            w.Put(s.character);
            w.Put(s.score);
            w.Put(s.multiplier);
            w.Put(s.armor);
            w.Put(s.jailCard);
            w.Put(s.lives);
            w.Put(s.ammo);
            w.Put(s.hasPos);
            w.Put(s.hasAngle);
            w.Put(s.x);
            w.Put(s.y);
            w.Put(s.z);
            w.Put(s.angle);
            w.Blob(s.objects);
            w.Blob(s.procBlock);
            w.Blob(s.procState);
            w.Blob(s.lines);
            w.Blob(s.triggers);
            w.Blob(s.doors);
            w.Blob(s.arrows);
            return w.buf;
        }

        bool Parse(const std::vector<uint8_t>& data, gta1::GameState& s, Header& h)
        {
            Reader r{ data.data(), data.data() + data.size() };
            r.Get(h.magic);
            r.Get(h.version);
            if (!r.ok || h.magic != kMagic || h.version < 1 || h.version > kVersion)
                return false;
            r.Get(h.saved);
            r.Get(s.exeStamp);
            r.Get(s.levelHash);
            r.Get(s.character);
            r.Get(s.score);
            r.Get(s.multiplier);
            r.Get(s.armor);
            r.Get(s.jailCard);
            r.Get(s.lives);
            r.Get(s.ammo);
            r.Get(s.hasPos);
            r.Get(s.hasAngle);
            r.Get(s.x);
            r.Get(s.y);
            r.Get(s.z);
            r.Get(s.angle);
            r.Blob(s.objects);
            r.Blob(s.procBlock);
            r.Blob(s.procState);
            r.Blob(s.lines);
            if (h.version >= 2)
                r.Blob(s.triggers);
            if (h.version >= 3)
                r.Blob(s.doors);
            if (h.version >= 4)
                r.Blob(s.arrows);
            return r.ok;
        }

        bool ReadAll(const std::wstring& path, std::vector<uint8_t>& out)
        {
            HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE)
                return false;
            const DWORD size = GetFileSize(f, nullptr);
            bool ok = size != INVALID_FILE_SIZE && size < 16 * 1024 * 1024;
            DWORD read = 0;
            if (ok)
            {
                out.resize(size);
                ok = ReadFile(f, out.data(), size, &read, nullptr) && read == size;
            }
            CloseHandle(f);
            return ok;
        }

        // Erst in eine Nebendatei schreiben, dann ersetzen: ein Absturz beim Speichern lässt den alten Stand stehen
        bool WriteAll(const std::wstring& path, const std::vector<uint8_t>& data)
        {
            const std::wstring tmp = path + L".tmp";
            HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE)
                return false;
            DWORD written = 0;
            const bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
            CloseHandle(f);
            return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        }

        // Ein Stand je Spielversion, Level und Charakter
        std::wstring SlotPath()
        {
            if (g_dir.empty() || !gta1::CanUseSaves())
                return L"";
            const int character = gta1::PlayerCharacter();
            const uint32_t level = gta1::LevelHash();
            if (character < 0 || !level)
                return L"";
            wchar_t name[48];
            swprintf_s(name, L"\\%08X_%d.sav", level, character);
            return g_dir + name;
        }

        void Notice(text::Id id, bool error)
        {
            g_notice = text::Get(id);
            g_noticeError = error;
        }

        // Levelstart erkennen (die Karte wird bei jedem Levelstart neu geöffnet)
        void TrackLevel()
        {
            const unsigned level = gta1::LevelLoads();
            if (level != g_level)
            {
                g_level = level;
                g_levelSince = GetTickCount();
                g_autosaveAt = 0;
            }
        }
    }

    void Init(const wchar_t* basePath)
    {
        g_dir = std::wstring(basePath) + L"_saves";
    }

    bool Available() { return !g_dir.empty() && gta1::CanUseSaves(); }

    bool HasSave()
    {
        const std::wstring path = SlotPath();
        return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // Das Skript richtet den Level in den ersten Sekunden noch ein; ein Stand von vorher oder ein Laden mittendrin
    // ließe einen Teil davon ausfallen (Tore der Bombenläden, Schranken, Waffen, Kräne)
    bool LevelReady()
    {
        TrackLevel();
        return Available() && gta1::LevelSetUp(GetTickCount() - g_levelSince);
    }

    bool CanSave() { return LevelReady() && !gta1::MissionActive() && !gta1::IsPlayerDead(); }

    bool Save(bool autosave)
    {
        if (!CanSave())
            return false;
        gta1::GameState state;
        const std::wstring path = SlotPath();
        if (path.empty() || !gta1::CaptureGameState(state))
            return false;
        CreateDirectoryW(g_dir.c_str(), nullptr);
        const std::vector<uint8_t> data = Serialize(state);
        const bool ok = WriteAll(path, data);
        LOG("%s: %ls (%u Byte, %d Punkte) %s", autosave ? "Autosave" : "Spielstand gespeichert", path.c_str(),
            (unsigned)data.size(), state.score, ok ? "ok" : "FEHLGESCHLAGEN");
        return ok;
    }

    const char* Load()
    {
        if (!Available())
            return "nicht im Level";
        if (!LevelReady())
            return "Level wird noch eingerichtet";
        std::vector<uint8_t> data;
        const std::wstring path = SlotPath();
        if (path.empty() || !ReadAll(path, data))
            return "kein Spielstand";
        gta1::GameState state;
        Header h;
        if (!Parse(data, state, h))
            return "Datei beschädigt oder alte Version";
        const char* error = gta1::RestoreGameState(state);
        g_autosaveAt = 0;
        LOG("Spielstand laden: %ls (%d Punkte) %s", path.c_str(), state.score, error ? error : "ok");
        return error;
    }

    bool SlotInfo(std::wstring& out)
    {
        out.clear();
        std::vector<uint8_t> data;
        gta1::GameState state;
        Header h;
        if (!ReadAll(SlotPath(), data) || !Parse(data, state, h))
            return false;
        FILETIME local;
        SYSTEMTIME st;
        wchar_t date[64] = L"", time[32] = L"", when[100], line[200];
        FileTimeToLocalFileTime(&h.saved, &local);
        FileTimeToSystemTime(&local, &st);
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 64);
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, 32);
        swprintf_s(when, L"%s %s", date, time);
        swprintf_s(line, text::Get(text::SaveSlotInfo), when, state.score);
        out = line;
        return true;
    }

    void OnMissionEnd()
    {
        if (!g_cfg.autosave)
            return;
        TrackLevel();
        g_autosaveAt = GetTickCount() + kAutosaveDelay;
        g_autosaveLevel = g_level;
        if (!g_autosaveAt)
            g_autosaveAt = 1;
    }

    void Tick()
    {
        TrackLevel();
        if (!g_autosaveAt || (int)(GetTickCount() - g_autosaveAt) < 0)
            return;
        g_autosaveAt = 0;
        if (g_autosaveLevel != g_level || !g_cfg.autosave || !Available())
            return;
        if (!CanSave())
        {
            LOG("Autosave übersprungen (neue Mission läuft schon oder Spielfigur tot)");
            return;
        }
        const bool ok = Save(true);
        Notice(ok ? text::AutoSaved : text::SaveFailed, !ok);
    }

    bool TakeLoadOffer()
    {
        TrackLevel();
        if (g_level == g_offered || GetTickCount() - g_levelSince < kOfferDelay || !LevelReady())
            return false;
        g_offered = g_level;
        const std::wstring path = SlotPath();
        const bool have = HasSave();
        LOG("Levelstart %u: Spielstand %ls %s", g_level, path.c_str(), have ? "vorhanden" : "nicht vorhanden");
        return have;
    }

    bool TakeNotice(std::wstring& out, bool& error)
    {
        if (g_notice.empty())
            return false;
        out.swap(g_notice);
        g_notice.clear();
        error = g_noticeError;
        return true;
    }
}
