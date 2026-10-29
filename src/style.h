#pragma once

#include <cstdint>
#include <vector>

// Grafik aus der Style-Datei der geladenen Karte (styleNNN.g24 neben der .cmp), Aufbau wie im Lader 0x426A70
namespace style
{
    struct Sprite
    {
        std::vector<uint32_t> px;   // ARGB, Alpha 0 = durchsichtig
        int w = 0, h = 0;
    };

    class File
    {
    public:
        bool Load();                                    // Style der Karte, die das Spiel gerade geladen hat
        bool SpriteAt(int index, Sprite& out) const;    // Nummer in der Sprite-Tabelle (die Pfeile/HUD zuerst)
        int  CarSprite(int model) const;                // Sprite eines Fahrzeugmodells (car_info), -1 unbekannt
        int  ObjectSprite(int type, int& frames) const; // erstes Sprite einer Objektart und Zahl der Bilder

    private:
        std::vector<uint8_t> d_;
        size_t clutOff_ = 0, numCluts_ = 0, palOff_ = 0, palCount_ = 0, tileCluts_ = 0;
        size_t objOff_ = 0, objSize_ = 0, carOff_ = 0, carSize_ = 0, sprInfoOff_ = 0, sprGfxOff_ = 0, sprGfxSize_ = 0;
        uint16_t nums_[21] = {};
    };
}
