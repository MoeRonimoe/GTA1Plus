#include "fire.h"
#include "config.h"
#include "gta1.h"
#include "log.h"
#include "overlay.h"

namespace fire
{
    namespace
    {
        // Ablauf in Spiel-Frames (GTA1 simuliert etwa 21 Frames pro Sekunde). Gezeigt werden die Bilder,
        // mit denen das Spiel eine getroffene Figur zu Boden gehen lässt; die Spiellogik bleibt dabei
        // unverändert bei "steht", nur das Zeichnen bekommt ein anderes Bild.
        constexpr int kStep = 2;                        // Frames je Einzelbild
        const int kFall[] = { 0xAF, 0xB0, 0xB1, 0xB2 }; // hinfallen, rückwärts gelesen: aufstehen
        const int kRoll[] = { 0xE9, 0xEA, 0xEB, 0xEA }; // am Boden liegen und sich winden
        constexpr int kFallFrames = 4 * kStep;
        constexpr int kRolls = 5;                       // so oft wälzt sich die Figur
        constexpr int kRollFrames = 4 * kStep;
        constexpr int kOutAt = kFallFrames + kRolls * kRollFrames; // hier erlöschen die Flammen
        constexpr int kTotal = kOutAt + kFallFrames;
        constexpr int kTurn = 2048;                     // Drehung beim Wälzen insgesamt (2 volle Umdrehungen)

        bool g_enabled = false;
        bool g_rolling = false;
        bool g_out = false;         // Flammen schon gelöscht
        bool g_jumpHeld = false;
        int  g_frame = 0;
        int  g_angle = 0;           // Blickrichtung beim Hinwerfen, danach wieder hergestellt

        void Stop(bool restorePose)
        {
            gta1::SetPlayerBurnPaused(false);
            if (restorePose)
                gta1::ShowPlayerPose(-1, g_angle);
            g_rolling = false;
        }

        void Start()
        {
            g_angle = 0;
            gta1::GetPlayerPedAngle(g_angle);
            g_rolling = true;
            g_out = false;
            g_frame = 0;
            gta1::SetPlayerBurnPaused(true);
            LOG("Feuer: Spielfigur wirft sich zu Boden (Lebensenergie %d)", gta1::PlayerHealth());
        }

        void Pose(int frame, int& anim, int& angle)
        {
            angle = g_angle;
            if (frame < kFallFrames)
            {
                anim = kFall[frame / kStep];
                return;
            }
            if (frame < kOutAt)
            {
                const int f = frame - kFallFrames;
                anim = kRoll[f % kRollFrames / kStep];
                angle = g_angle + f * kTurn / (kRolls * kRollFrames);
                return;
            }
            anim = kFall[3 - (frame - kOutAt) / kStep];
        }
    }

    void Init()
    {
        if (!g_cfg.extinguish)
            return;
        g_enabled = gta1::EnableBurnPause();
        LOG("Feuer löschen mit der Sprungtaste: %s", g_enabled ? "aktiv" : "Spielcode passt nicht");
    }

    bool CanRoll()
    {
        return g_enabled && !g_rolling && gta1::IsPlayerOnFoot() && gta1::IsPlayerBurning() && !gta1::IsPlayerDead();
    }

    bool Rolling() { return g_rolling; }

    bool OnJump(bool held)
    {
        const bool pressed = held && !g_jumpHeld;
        g_jumpHeld = held;
        if (g_rolling)
            return true;
        if (!CanRoll())
            return false;
        // Brennend wird nie normal gesprungen, der Druck wirft die Figur zu Boden
        if (pressed)
            Start();
        return true;
    }

    void BeforeRender()
    {
        if (!g_rolling)
            return;
        if (!gta1::IsPlayerOnFoot() || gta1::IsPlayerDead())
        {
            Stop(false);
            return;
        }

        if (g_frame >= kOutAt && !g_out)
        {
            g_out = true;
            gta1::ExtinguishPlayer();
            gta1::SetPlayerBurnPaused(false);
            LOG("Feuer: Flammen gelöscht (Lebensenergie %d)", gta1::PlayerHealth());
        }
        if (g_frame >= kTotal)
        {
            Stop(true);
            return;
        }

        int anim = -1, angle = -1;
        Pose(g_frame, anim, angle);
        gta1::ShowPlayerPose(anim, angle);

        // Im Pausemenü bzw. bei angehaltenem Spiel bleibt das Bild stehen
        if (gta1::RunState() == 1 && !overlay::MenuActive())
            ++g_frame;
    }
}
