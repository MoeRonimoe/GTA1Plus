#include "text.h"
#include "log.h"

#include <windows.h>
#include <cwchar>
#include <string>

namespace text
{
    namespace
    {
        const wchar_t* const kKeys[Count] = {
            L"PauseTitle", L"Resume", L"MapAndWaypoints", L"Map", L"QuitLevel",
            L"OnFoot", L"InVehicle", L"OnTrain",
            L"GpsTarget", L"GpsNone", L"TargetWaypoint", L"TargetMission", L"TargetMissionRed", L"WaypointCount",
            L"StyleTextured", L"StyleSchematic", L"ViewFree", L"CursorPos", L"MapUnavailable",
            L"ConfirmTitle", L"ConfirmText", L"Yes", L"No",
            L"HelpList", L"HelpMap", L"HelpMapNoWaypoints", L"HelpConfirm",
            L"HintActive", L"HintMenu", L"HintMap", L"HintMinimap",
            L"HelpMapMouse", L"HelpMapMouseNoWaypoints",
            L"HintBurning", L"PadJumpButton", L"KeySpace",
        };

        struct LangTable
        {
            const wchar_t* code;
            const wchar_t* texts[Count];
        };

        const LangTable kBuiltin[] = {
            { L"de", {
                L"PAUSE", L"Fortsetzen", L"Karte & Wegpunkte", L"Karte", L"Level beenden",
                L"zu Fuß", L"im Fahrzeug", L"im Zug",
                L"GPS: %s  ·  %d m", L"GPS: kein Ziel", L"Wegpunkt", L"Missionsziel", L"Missionsziel (rot)", L"Wegpunkte: %d",
                L"Texturen", L"Schema", L"frei", L"Fadenkreuz  X %d  Y %d", L"Karte nicht verfügbar",
                L"Level beenden?", L"Du kehrst ins Hauptmenü des Spiels zurück.", L"Ja", L"Nein",
                L"[↑ / ↓] Auswahl    [Enter] Bestätigen    [%s] Karte    [Esc] Weiterspielen",
                L"[Pfeile] Verschieben   [+ / -] Zoom   [Enter] Wegpunkt setzen/entfernen   [Entf] Alle löschen   [Leertaste] Zum Spieler   [Tab] Stil   [Esc] Zurück",
                L"[Pfeile] Verschieben   [+ / -] Zoom   [Leertaste] Zum Spieler   [Tab] Stil   [Esc] Zurück",
                L"[← / →] Auswahl    [Enter] Bestätigen    [Esc] Abbrechen",
                L"GTA1Plus aktiv", L"[Esc] Pausemenü", L"[%s] Karte", L"[%s] Minimap",
                L"[Ziehen] Verschieben   [Mausrad] Zoom   [Klick] Wegpunkt setzen/entfernen   [Entf] Alle löschen   [Leertaste] Zum Spieler   [Rechtsklick] Zurück",
                L"[Ziehen] Verschieben   [Mausrad] Zoom   [Leertaste] Zum Spieler   [Tab] Stil   [Rechtsklick] Zurück",
                L"Du brennst!   %s  am Boden wälzen und löschen", L"Kreuz/A", L"Leertaste",
            } },
            { L"en", {
                L"PAUSED", L"Resume", L"Map & waypoints", L"Map", L"Quit level",
                L"on foot", L"in a vehicle", L"on a train",
                L"GPS: %s  ·  %d m", L"GPS: no destination", L"Waypoint", L"Mission target", L"Mission target (red)", L"Waypoints: %d",
                L"Textured", L"Schematic", L"free", L"Cursor  X %d  Y %d", L"Map not available",
                L"Quit level?", L"You will return to the game's main menu.", L"Yes", L"No",
                L"[↑ / ↓] Select    [Enter] Confirm    [%s] Map    [Esc] Resume",
                L"[Arrows] Pan   [+ / -] Zoom   [Enter] Set/remove waypoint   [Del] Clear all   [Space] Center on player   [Tab] Style   [Esc] Back",
                L"[Arrows] Pan   [+ / -] Zoom   [Space] Center on player   [Tab] Style   [Esc] Back",
                L"[← / →] Select    [Enter] Confirm    [Esc] Cancel",
                L"GTA1Plus active", L"[Esc] Pause menu", L"[%s] Map", L"[%s] Minimap",
                L"[Drag] Pan   [Wheel] Zoom   [Click] Set/remove waypoint   [Del] Clear all   [Space] Center on player   [Right-click] Back",
                L"[Drag] Pan   [Wheel] Zoom   [Space] Center on player   [Tab] Style   [Right-click] Back",
                L"You're on fire!   %s  roll on the ground to put it out", L"Cross/A", L"Space",
            } },
            { L"fr", {
                L"PAUSE", L"Reprendre", L"Carte et repères", L"Carte", L"Quitter le niveau",
                L"à pied", L"en véhicule", L"dans le train",
                L"GPS : %s  ·  %d m", L"GPS : aucune destination", L"Repère", L"Objectif", L"Objectif (rouge)", L"Repères : %d",
                L"Textures", L"Schéma", L"libre", L"Réticule  X %d  Y %d", L"Carte indisponible",
                L"Quitter le niveau ?", L"Vous retournerez au menu principal du jeu.", L"Oui", L"Non",
                L"[↑ / ↓] Choisir    [Entrée] Valider    [%s] Carte    [Échap] Reprendre",
                L"[Flèches] Déplacer   [+ / -] Zoom   [Entrée] Placer/retirer un repère   [Suppr] Tout effacer   [Espace] Centrer sur le joueur   [Tab] Style   [Échap] Retour",
                L"[Flèches] Déplacer   [+ / -] Zoom   [Espace] Centrer sur le joueur   [Tab] Style   [Échap] Retour",
                L"[← / →] Choisir    [Entrée] Valider    [Échap] Annuler",
                L"GTA1Plus actif", L"[Échap] Menu pause", L"[%s] Carte", L"[%s] Mini-carte",
                L"[Glisser] Déplacer   [Molette] Zoom   [Clic] Placer/retirer un repère   [Suppr] Tout effacer   [Espace] Centrer sur le joueur   [Clic droit] Retour",
                L"[Glisser] Déplacer   [Molette] Zoom   [Espace] Centrer sur le joueur   [Tab] Style   [Clic droit] Retour",
                L"Tu es en feu !   %s  te rouler par terre pour l'éteindre", L"Croix/A", L"Espace",
            } },
            { L"it", {
                L"PAUSA", L"Riprendi", L"Mappa e segnaposti", L"Mappa", L"Esci dal livello",
                L"a piedi", L"in un veicolo", L"sul treno",
                L"GPS: %s  ·  %d m", L"GPS: nessuna destinazione", L"Segnaposto", L"Obiettivo", L"Obiettivo (rosso)", L"Segnaposti: %d",
                L"Texture", L"Schema", L"libera", L"Mirino  X %d  Y %d", L"Mappa non disponibile",
                L"Uscire dal livello?", L"Tornerai al menu principale del gioco.", L"Sì", L"No",
                L"[↑ / ↓] Seleziona    [Invio] Conferma    [%s] Mappa    [Esc] Riprendi",
                L"[Frecce] Sposta   [+ / -] Zoom   [Invio] Aggiungi/rimuovi segnaposto   [Canc] Rimuovi tutti   [Spazio] Centra sul giocatore   [Tab] Stile   [Esc] Indietro",
                L"[Frecce] Sposta   [+ / -] Zoom   [Spazio] Centra sul giocatore   [Tab] Stile   [Esc] Indietro",
                L"[← / →] Seleziona    [Invio] Conferma    [Esc] Annulla",
                L"GTA1Plus attiva", L"[Esc] Menu di pausa", L"[%s] Mappa", L"[%s] Minimappa",
                L"[Trascina] Sposta   [Rotella] Zoom   [Clic] Aggiungi/rimuovi segnaposto   [Canc] Rimuovi tutti   [Spazio] Centra sul giocatore   [Clic destro] Indietro",
                L"[Trascina] Sposta   [Rotella] Zoom   [Spazio] Centra sul giocatore   [Tab] Stile   [Clic destro] Indietro",
                L"Stai bruciando!   %s  rotola a terra per spegnerti", L"Croce/A", L"Spazio",
            } },
        };
        constexpr int kBuiltinCount = sizeof(kBuiltin) / sizeof(kBuiltin[0]);

        std::wstring g_texts[Count];
        std::wstring g_language = L"en";

        // Sprachcode des Spiels (Registry-Wert "Language": 0 en, 1 fr, 2 de, 3 it, 4 jp)
        std::wstring GameLanguage()
        {
            DWORD value = 0, size = sizeof(value);
            if (RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\DMA Design\\Grand Theft Auto", L"Language",
                             RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
                return L"en";
            switch (value)
            {
            case 1: return L"fr";
            case 2: return L"de";
            case 3: return L"it";
            default: return L"en";
            }
        }

        const LangTable* Builtin(const std::wstring& code)
        {
            for (const LangTable& l : kBuiltin)
                if (code == l.code)
                    return &l;
            return nullptr;
        }

        // Neue Datei als UTF-16 anlegen, damit GetPrivateProfileStringW Umlaute korrekt liest
        void EnsureFile(const wchar_t* path)
        {
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
                return;
            HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE)
                return;
            std::wstring content =
                L"﻿; GTA1Plus - Texte / texts\r\n"
                L"; One section per language, chosen via [Language] in the main INI.\r\n"
                L"; Placeholders such as %s and %d must be kept.\r\n"
                L"; To add a language, simply add another section (e.g. [es]).\r\n";
            for (const LangTable& l : kBuiltin)
            {
                content += L"\r\n[";
                content += l.code;
                content += L"]\r\n";
                for (int i = 0; i < Count; ++i)
                {
                    content += kKeys[i];
                    content += L"=";
                    content += l.texts[i];
                    content += L"\r\n";
                }
            }
            DWORD written;
            WriteFile(f, content.data(), (DWORD)(content.size() * sizeof(wchar_t)), &written, nullptr);
            CloseHandle(f);
        }
    }

    void Init(const wchar_t* langFile, const wchar_t* language)
    {
        EnsureFile(langFile);

        // Fehlende Einträge der mitgelieferten Sprachen ergänzen, eigene Änderungen bleiben erhalten
        wchar_t buf[512];
        for (const LangTable& l : kBuiltin)
            for (int i = 0; i < Count; ++i)
                if (!GetPrivateProfileStringW(l.code, kKeys[i], L"", buf, 512, langFile))
                    WritePrivateProfileStringW(l.code, kKeys[i], l.texts[i], langFile);

        g_language = (!language[0] || _wcsicmp(language, L"auto") == 0) ? GameLanguage() : std::wstring(language);
        const LangTable* fallback = Builtin(g_language);
        if (!fallback)
            fallback = Builtin(L"en");
        for (int i = 0; i < Count; ++i)
        {
            GetPrivateProfileStringW(g_language.c_str(), kKeys[i], fallback->texts[i], buf, 512, langFile);
            g_texts[i] = buf;
        }
        LOG("Sprache: %ls", g_language.c_str());
    }

    const wchar_t* Get(Id id) { return g_texts[id].c_str(); }
    const wchar_t* Language() { return g_language.c_str(); }
}
