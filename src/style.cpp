#include "style.h"
#include "gta1.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace style
{
    namespace
    {
        bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
        {
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "rb") || !f)
                return false;
            fseek(f, 0, SEEK_END);
            const long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            out.resize(size > 0 ? size : 0);
            const bool ok = size > 0 && fread(out.data(), 1, out.size(), f) == out.size();
            fclose(f);
            return ok;
        }

        // Reihenfolge der Sprite-Arten in der Style-Datei
        enum Category { Arrow, Digits, Boat, Box, Bus, Car, Object, Ped, Speedo, Tank, TLights, Train, TrDoors, Bike,
                        Tram, WBus, WCar, Ex, TumCar, TumTruck, Ferry, CategoryCount };
    }

    bool File::Load()
    {
        d_.clear();
        const std::string cmp = gta1::MapFile();
        std::vector<uint8_t> head;
        if (cmp.empty() || !ReadFile(cmp, head) || head.size() < 8)
            return false;
        char name[32];
        sprintf_s(name, "style%03u.g24", head[4]);
        if (!ReadFile(cmp.substr(0, cmp.find_last_of("\\/") + 1) + name, d_) || d_.size() < 64)
            return false;
        uint32_t h[16];
        memcpy(h, d_.data(), sizeof(h));
        if (h[0] != 336)   // G24
            return false;
        clutOff_ = 64 + ((size_t(h[1]) + h[2] + h[3] + 0x3FFF) & ~size_t(0x3FFF)) + h[4];
        const size_t clutBytes = (size_t(h[5]) + 0xFFFF) & ~size_t(0xFFFF);
        numCluts_ = clutBytes / 1024;
        palOff_ = clutOff_ + clutBytes;
        palCount_ = h[10] / 2;
        tileCluts_ = h[6] / 1024;
        objOff_ = palOff_ + h[10];
        objSize_ = h[11];
        carOff_ = objOff_ + objSize_;
        carSize_ = h[12];
        sprInfoOff_ = carOff_ + carSize_;
        sprGfxOff_ = sprInfoOff_ + h[13];
        sprGfxSize_ = h[14];
        const size_t numsOff = sprGfxOff_ + sprGfxSize_;
        if (numsOff + sizeof(nums_) > d_.size())
        {
            d_.clear();
            return false;
        }
        memcpy(nums_, d_.data() + numsOff, sizeof(nums_));
        return true;
    }

    bool File::SpriteAt(int index, Sprite& out) const
    {
        out = Sprite();
        if (d_.empty() || index < 0)
            return false;
        const auto u16 = [&](size_t o) { return uint16_t(d_[o] | (d_[o + 1] << 8)); };
        size_t o = sprInfoOff_;
        for (int i = 0; i < index; ++i)
        {
            if (o + 12 > sprGfxOff_)
                return false;
            o += 12 + size_t(d_[o + 2]) * 6;   // Einträge mit Änderungsbildern sind länger
        }
        if (o + 12 > sprGfxOff_)
            return false;
        const int w = d_[o], hgt = d_[o + 1];
        const int clut = u16(o + 6), sx = d_[o + 8], sy = d_[o + 9], page = u16(o + 10);
        const size_t logical = tileCluts_ + clut;
        if (logical >= palCount_)
            return false;
        const size_t phys = u16(palOff_ + logical * 2);
        if (phys >= numCluts_)
            return false;
        const size_t pageBase = clutOff_ + (phys / 64) * 65536 + (phys % 64) * 4;
        const size_t inPage = std::min<size_t>(64, numCluts_ - (phys / 64) * 64);
        out.w = w;
        out.h = hgt;
        out.px.assign(size_t(w) * hgt, 0);
        for (int j = 0; j < hgt; ++j)
            for (int i = 0; i < w; ++i)
            {
                const size_t src = sprGfxOff_ + size_t(page) * 65536 + size_t(sy + j) * 256 + sx + i;
                if (src >= sprGfxOff_ + sprGfxSize_)
                    return false;
                const uint8_t v = d_[src];
                if (!v)
                    continue;
                const uint8_t* e = d_.data() + pageBase + v * inPage * 4;   // B, G, R, 0
                out.px[size_t(j) * w + i] = 0xFF000000u | (e[2] << 16) | (e[1] << 8) | e[0];
            }
        return true;
    }

    int File::CarSprite(int model) const
    {
        if (d_.empty())
            return -1;
        // car_info: Maße und Werte (22 Byte), Farben (72 + 12), vtype und model bei +106, Türen (int16) bei +172,
        // dahinter 8 Byte je Tür
        size_t o = carOff_;
        const size_t end = carOff_ + carSize_;
        while (o + 174 <= end)
        {
            const int spr = int16_t(d_[o + 6] | (d_[o + 7] << 8));
            const int vtype = d_[o + 106], m = d_[o + 107];
            const int doors = int16_t(d_[o + 172] | (d_[o + 173] << 8));
            if (doors < 0 || doors > 10)
                return -1;
            if (m == model)
            {
                int category;
                switch (vtype)
                {
                case 0: category = Bus; break;
                case 3: category = Bike; break;
                case 8: category = Train; break;
                case 9: category = Tram; break;
                case 13: category = Boat; break;
                case 14: category = Tank; break;
                default: category = Car; break;
                }
                int base = 0;
                for (int c = 0; c < category; ++c)
                    base += nums_[c];
                return spr >= 0 && spr < nums_[category] ? base + spr : -1;
            }
            o += 174 + size_t(doors) * 8;
        }
        return -1;
    }

    int File::ObjectSprite(int type, int& frames) const
    {
        frames = 0;
        // object_info: Breite, Höhe, Tiefe (int32; bei Animationen Breite = Zahl der Bilder, Rest 0), Sprite (uint16) ...
        const size_t o = objOff_ + size_t(type) * 20;
        if (d_.empty() || type < 0 || o + 20 > objOff_ + objSize_)
            return -1;
        int32_t w, h, depth;
        memcpy(&w, &d_[o], 4);
        memcpy(&h, &d_[o + 4], 4);
        memcpy(&depth, &d_[o + 8], 4);
        const int spr = d_[o + 12] | (d_[o + 13] << 8);
        frames = h == 0 && depth == 0 && w > 0 && w < 64 ? w : 1;
        int base = 0;
        for (int c = 0; c < Object; ++c)
            base += nums_[c];
        return spr + frames <= nums_[Object] ? base + spr : -1;
    }
}
