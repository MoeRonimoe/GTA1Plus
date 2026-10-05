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
            L"MenuBriefings", L"MenuSave", L"MenuLoad", L"MenuOptions", L"OptOn", L"OptOff", L"OptRestart", L"OptGroupDisplay", L"OptGroupControls", L"OptGroupCamera", L"OptGroupGameplay", L"OptGroupOther", L"OptMinimap", L"OptMinimapOnStart", L"OptHint", L"OptGps", L"OptGpsArrow", L"OptWaypoints", L"OptHearts", L"OptMapStyle", L"OptMinimapCorner", L"OptPauseGame", L"OptTwinStick", L"OptAlwaysAim", L"OptWasdDriving", L"OptController", L"OptCamera", L"OptCameraMouseVehicle", L"OptRangeFoot", L"OptRangeVehicle", L"OptDeadZone", L"OptSteady", L"OptKeepMultiplier", L"OptAllCharacters", L"OptQuickRetry", L"OptExtinguish", L"OptPortraitColors", L"OptAutosave", L"OptLanguage", L"OptShowInfo", L"CornerBottomLeft", L"CornerBottomRight", L"CornerTopLeft", L"CornerTopRight", L"LanguageAuto", L"HelpOptions", L"BriefingsEmpty", L"HelpBriefings", L"SaveDone", L"SaveBlocked", L"SaveFailed", L"LoadTitle", L"LoadText", L"LoadDone", L"LoadFailed", L"NoSave", L"AutoSaved", L"SaveSlotInfo", L"StartLoadTitle", L"StartLoadText", L"OptTilt", L"OptTiltAngle", L"OptTiltRotate", L"TiltAlways", L"OptVehicleView", L"OptTrafficRange", L"ViewGame", L"ViewHigh", L"ViewHigher", L"ViewMax", L"OptPois", L"PoiSpray", L"PoiBombShop", L"PoiCrane", L"OptPistolDamage", L"OptMachineGunDamage", L"OptPunchDamage", L"OptRumble", L"MenuProgress", L"HelpProgress", L"ProgScore", L"ProgMissions", L"ProgSecrets", L"ProgLives", L"ProgMultiplier", L"ProgUnavailable",
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
                L"Briefings",
                L"Spiel speichern",
                L"Spiel laden",
                L"GTA1Plus-Optionen",
                L"An",
                L"Aus",
                L"wirkt nach Neustart",
                L"Anzeige",
                L"Steuerung",
                L"Kamera",
                L"Spielregeln",
                L"Sonstiges",
                L"Minimap",
                L"Minimap beim Levelstart",
                L"Hinweis beim Levelstart",
                L"GPS-Route",
                L"GPS-Pfeil",
                L"Eigene Wegpunkte",
                L"Herzen (Lebensenergie)",
                L"Kartenstil",
                L"Position der Minimap",
                L"Spiel im Menü pausieren",
                L"Twin-Stick (WASD + Maus)",
                L"Immer zum Fadenkreuz schauen",
                L"Mit WASD fahren",
                L"Controller",
                L"Kamera mit Maus/Stick",
                L"Maus-Kamera im Fahrzeug",
                L"Kamera-Weite zu Fuß",
                L"Kamera-Weite im Fahrzeug",
                L"Innerer Bereich (Maus)",
                L"Ruhigere Kamera",
                L"Multiplikator bei Verhaftung behalten",
                L"Alle Charaktere wählbar",
                L"Schneller neuer Versuch",
                L"Feuer löschen (Springen)",
                L"Farbe passend zum Portrait",
                L"Autosave nach Missionen",
                L"Sprache",
                L"Debug-Anzeige",
                L"unten links",
                L"unten rechts",
                L"oben links",
                L"oben rechts",
                L"wie im Spiel",
                L"[↑ / ↓] Auswahl    [← / →] Ändern    [Esc] Zurück",
                L"Noch keine Nachrichten in diesem Level",
                L"[↑ / ↓] Blättern    [Esc] Zurück",
                L"Spielstand gespeichert",
                L"Speichern geht nur außerhalb von Missionen",
                L"Speichern fehlgeschlagen",
                L"Spielstand laden?",
                L"Der aktuelle Fortschritt in diesem Level geht verloren.",
                L"Spielstand geladen",
                L"Laden fehlgeschlagen",
                L"Kein Spielstand für diesen Charakter und dieses Level",
                L"Automatisch gespeichert",
                L"Stand vom %s  ·  %d Punkte",
                L"Gespeicherten Stand laden?",
                L"Für diesen Charakter und dieses Level gibt es einen Spielstand.",
                L"Kippkamera (Chinatown Wars)",
                L"Neigung der Kippkamera",
                L"Bild dreht mit dem Fahrzeug",
                L"immer",
                L"Kamerahöhe im Fahrzeug",
                L"Entfernung für neuen Verkehr",
                L"wie im Spiel",
                L"hoch",
                L"sehr hoch",
                L"maximal",
                L"Orte auf der Karte",
                L"Lackiererei",
                L"Bombenladen",
                L"Autokran",
                L"Schaden Pistole",
                L"Schaden Maschinenpistole",
                L"Schaden Faustschlag",
                L"Controller-Vibration",
                L"Fortschritt",
                L"[Esc] Zurück",
                L"Punkte",
                L"Missionen",
                L"Geheimnisse",
                L"Leben",
                L"Multiplikator",
                L"Keine Angaben für dieses Level",
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
                L"Briefings",
                L"Save game",
                L"Load game",
                L"GTA1Plus options",
                L"On",
                L"Off",
                L"takes effect after restart",
                L"Display",
                L"Controls",
                L"Camera",
                L"Gameplay",
                L"Other",
                L"Minimap",
                L"Minimap at level start",
                L"Hint at level start",
                L"GPS route",
                L"GPS arrow",
                L"Own waypoints",
                L"Hearts (health)",
                L"Map style",
                L"Minimap position",
                L"Pause game in menu",
                L"Twin-stick (WASD + mouse)",
                L"Always face the crosshair",
                L"Drive with WASD",
                L"Controller",
                L"Camera with mouse/stick",
                L"Mouse camera in vehicles",
                L"Camera range on foot",
                L"Camera range in vehicles",
                L"Inner area (mouse)",
                L"Steadier camera",
                L"Keep multiplier when busted",
                L"All characters selectable",
                L"Quick retry",
                L"Put out fire (jump)",
                L"Colour to match portrait",
                L"Autosave after missions",
                L"Language",
                L"Debug info",
                L"bottom left",
                L"bottom right",
                L"top left",
                L"top right",
                L"as in the game",
                L"[↑ / ↓] Select    [← / →] Change    [Esc] Back",
                L"No messages in this level yet",
                L"[↑ / ↓] Scroll    [Esc] Back",
                L"Game saved",
                L"You can only save outside of missions",
                L"Saving failed",
                L"Load saved game?",
                L"Your current progress in this level will be lost.",
                L"Game loaded",
                L"Loading failed",
                L"No saved game for this character and level",
                L"Autosaved",
                L"Saved %s  ·  %d points",
                L"Load your saved game?",
                L"There is a saved game for this character and level.",
                L"Tilted camera (Chinatown Wars)",
                L"Tilt angle",
                L"Picture turns with vehicle",
                L"always",
                L"Camera height in vehicles",
                L"Traffic spawn distance",
                L"as in the game",
                L"high",
                L"very high",
                L"maximum",
                L"Places on the map",
                L"Paint shop",
                L"Bomb shop",
                L"Car crane",
                L"Pistol damage",
                L"Machine gun damage",
                L"Punch damage",
                L"Controller vibration",
                L"Progress",
                L"[Esc] Back",
                L"Score",
                L"Missions",
                L"Secrets",
                L"Lives",
                L"Multiplier",
                L"No figures for this level",
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
                L"Briefings",
                L"Sauvegarder",
                L"Charger",
                L"Options GTA1Plus",
                L"Oui",
                L"Non",
                L"après redémarrage",
                L"Affichage",
                L"Commandes",
                L"Caméra",
                L"Règles du jeu",
                L"Divers",
                L"Mini-carte",
                L"Mini-carte au début",
                L"Astuce au début",
                L"Itinéraire GPS",
                L"Flèche GPS",
                L"Repères perso",
                L"Cœurs (santé)",
                L"Style de carte",
                L"Position mini-carte",
                L"Pause dans le menu",
                L"Twin-stick (ZQSD + souris)",
                L"Toujours viser",
                L"Conduire avec ZQSD",
                L"Manette",
                L"Caméra souris/stick",
                L"Caméra souris en véhicule",
                L"Portée à pied",
                L"Portée en véhicule",
                L"Zone morte (souris)",
                L"Caméra plus stable",
                L"Garder le multiplicateur",
                L"Tous les personnages",
                L"Nouvel essai rapide",
                L"Éteindre le feu (sauter)",
                L"Couleur selon portrait",
                L"Sauvegarde auto",
                L"Langue",
                L"Infos de débogage",
                L"en bas à gauche",
                L"en bas à droite",
                L"en haut à gauche",
                L"en haut à droite",
                L"comme le jeu",
                L"[↑ / ↓] Choisir    [← / →] Modifier    [Échap] Retour",
                L"Aucun message dans ce niveau",
                L"[↑ / ↓] Défiler    [Échap] Retour",
                L"Partie sauvegardée",
                L"Sauvegarde hors mission seulement",
                L"Échec de la sauvegarde",
                L"Charger la sauvegarde ?",
                L"La progression actuelle de ce niveau sera perdue.",
                L"Partie chargée",
                L"Échec du chargement",
                L"Aucune sauvegarde pour ce personnage et ce niveau",
                L"Sauvegarde automatique",
                L"Sauvegarde du %s  ·  %d points",
                L"Charger votre sauvegarde ?",
                L"Il existe une sauvegarde pour ce personnage et ce niveau.",
                L"Caméra inclinée (Chinatown Wars)",
                L"Angle d'inclinaison",
                L"L'image tourne avec le véhicule",
                L"toujours",
                L"Hauteur caméra en véhicule",
                L"Distance d'apparition du trafic",
                L"comme le jeu",
                L"haute",
                L"très haute",
                L"maximale",
                L"Lieux sur la carte",
                L"Carrosserie",
                L"Magasin de bombes",
                L"Grue",
                L"Dégâts du pistolet",
                L"Dégâts de la mitraillette",
                L"Dégâts des coups de poing",
                L"Vibration de la manette",
                L"Progression",
                L"[Échap] Retour",
                L"Score",
                L"Missions",
                L"Secrets",
                L"Vies",
                L"Multiplicateur",
                L"Pas de données pour ce niveau",
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
                L"Briefing",
                L"Salva partita",
                L"Carica partita",
                L"Opzioni GTA1Plus",
                L"Sì",
                L"No",
                L"dopo il riavvio",
                L"Schermo",
                L"Controlli",
                L"Telecamera",
                L"Regole di gioco",
                L"Altro",
                L"Minimappa",
                L"Minimappa all'inizio",
                L"Suggerimento all'inizio",
                L"Percorso GPS",
                L"Freccia GPS",
                L"Segnaposti propri",
                L"Cuori (salute)",
                L"Stile mappa",
                L"Posizione minimappa",
                L"Pausa nel menu",
                L"Twin-stick (WASD + mouse)",
                L"Mira sempre",
                L"Guida con WASD",
                L"Controller",
                L"Telecamera con mouse/stick",
                L"Telecamera mouse nei veicoli",
                L"Portata a piedi",
                L"Portata nei veicoli",
                L"Zona morta (mouse)",
                L"Telecamera più stabile",
                L"Mantieni moltiplicatore",
                L"Tutti i personaggi",
                L"Nuovo tentativo rapido",
                L"Spegnere il fuoco (salto)",
                L"Colore del ritratto",
                L"Salvataggio automatico",
                L"Lingua",
                L"Info di debug",
                L"in basso a sinistra",
                L"in basso a destra",
                L"in alto a sinistra",
                L"in alto a destra",
                L"come il gioco",
                L"[↑ / ↓] Seleziona    [← / →] Modifica    [Esc] Indietro",
                L"Nessun messaggio in questo livello",
                L"[↑ / ↓] Scorri    [Esc] Indietro",
                L"Partita salvata",
                L"Salva solo fuori dalle missioni",
                L"Salvataggio non riuscito",
                L"Caricare il salvataggio?",
                L"I progressi attuali in questo livello andranno persi.",
                L"Partita caricata",
                L"Caricamento non riuscito",
                L"Nessun salvataggio per questo personaggio e livello",
                L"Salvataggio automatico",
                L"Salvato il %s  ·  %d punti",
                L"Caricare il salvataggio?",
                L"Esiste un salvataggio per questo personaggio e livello.",
                L"Telecamera inclinata (Chinatown Wars)",
                L"Angolo di inclinazione",
                L"L'immagine ruota col veicolo",
                L"sempre",
                L"Altezza telecamera nei veicoli",
                L"Distanza di comparsa del traffico",
                L"come il gioco",
                L"alta",
                L"molto alta",
                L"massima",
                L"Luoghi sulla mappa",
                L"Carrozzeria",
                L"Negozio di bombe",
                L"Gru",
                L"Danno pistola",
                L"Danno mitragliatrice",
                L"Danno pugni",
                L"Vibrazione del controller",
                L"Progressi",
                L"[Esc] Indietro",
                L"Punteggio",
                L"Missioni",
                L"Segreti",
                L"Vite",
                L"Moltiplicatore",
                L"Nessun dato per questo livello",
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
