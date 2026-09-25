#include "citymap.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
#pragma pack(push, 1)
    struct CmpHeader
    {
        uint32_t version;       // 331
        uint8_t  style;         // -> styleXXX.g24
        uint8_t  sample;
        uint16_t reserved;
        uint32_t routeSize, objectPosSize, columnSize, blockSize, navDataSize;
    };

    struct BlockInfo
    {
        uint16_t typeMap;       // Bit 4-6 Typ, Bit 7 flach, Bit 14-15 Deckel-Drehung
        uint8_t  typeMapExt;
        uint8_t  left, right, top, bottom, lid;
    };

    struct G24Header
    {
        uint32_t version;       // 336
        uint32_t sideSize, lidSize, auxSize, animSize, clutSize;
        uint32_t tileClutSize, spriteClutSize, newCarClutSize, fontClutSize;
        uint32_t paletteIndexSize, objectInfoSize, carSize, spriteInfoSize, spriteGraphicsSize, spriteNumbersSize;
    };
#pragma pack(pop)

    const uint32_t kOutside = 0x14171B;
    const uint32_t kTypeColor[8] = {
        0x1C2127, // nichts
        0x4A90D9, // Wasser
        0xEDEAE3, // Straße
        0xCFCBC1, // Gehweg
        0x86B86A, // Wiese
        0xA89F94, // Gebäude
        0xA89F94, 0xA89F94,
    };

    bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out)
    {
        HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return false;
        const DWORD size = GetFileSize(f, nullptr);
        out.resize(size);
        DWORD got = 0;
        const bool ok = size != INVALID_FILE_SIZE && ReadFile(f, out.data(), size, &got, nullptr) && got == size;
        CloseHandle(f);
        return ok;
    }

    inline uint32_t Scale(uint32_t c, int f) // f: 256 = 1.0
    {
        const uint32_t r = std::min(255u, (((c >> 16) & 0xFF) * f) >> 8);
        const uint32_t g = std::min(255u, (((c >> 8) & 0xFF) * f) >> 8);
        const uint32_t b = std::min(255u, ((c & 0xFF) * f) >> 8);
        return (r << 16) | (g << 8) | b;
    }

    // Höhere Blöcke etwas heller, damit Gebäude plastisch wirken (Boden liegt meist auf Ebene 4)
    const int kHeightShade[7] = { 360, 340, 320, 300, 282, 250, 282 };
}

bool CityMap::Load(const std::string& cmpPath)
{
    m_loaded = m_hasTextures = false;
    m_path = cmpPath;
    m_tiles.clear();

    std::vector<uint8_t> cmp;
    if (!ReadWholeFile(cmpPath, cmp) || cmp.size() < sizeof(CmpHeader) + 65536 * 4)
    {
        LOG("Karte: %s nicht lesbar", cmpPath.c_str());
        return false;
    }

    const auto* hdr = reinterpret_cast<const CmpHeader*>(cmp.data());
    const size_t baseOff = sizeof(CmpHeader);
    const size_t colOff = baseOff + 65536 * 4;
    const size_t blkOff = colOff + hdr->columnSize;
    if (hdr->version != 331 || blkOff + hdr->blockSize > cmp.size())
    {
        LOG("Karte: unbekanntes Format in %s (Version %u)", cmpPath.c_str(), hdr->version);
        return false;
    }

    const auto* base = reinterpret_cast<const uint32_t*>(cmp.data() + baseOff);
    const uint8_t* columns = cmp.data() + colOff;
    const auto* blocks = reinterpret_cast<const BlockInfo*>(cmp.data() + blkOff);
    const uint32_t numBlocks = hdr->blockSize / sizeof(BlockInfo);

    // Jede Spalte listet ihre Blöcke von oben nach unten; der erste Block mit Deckel ist von oben sichtbar.
    // Typ und Fahrtrichtungen stehen im Block über dem Boden (dort, wo Autos und Fußgänger sind).
    for (int i = 0; i < 65536; ++i)
    {
        m_lid[i] = 0;
        m_remap[i] = 0;
        m_rot[i] = 0;
        m_type[i] = 0;
        m_z[i] = 6;
        m_road[i] = 0;

        const uint32_t off = base[i];
        if (off + 2 > hdr->columnSize)
            continue;
        uint16_t height;
        memcpy(&height, columns + off, 2);
        const int count = 6 - height;
        if (height > 6 || off + 2 + count * 2 > hdr->columnSize)
            continue;

        bool typeDone = false;
        for (int k = 0; k < count; ++k)
        {
            uint16_t b;
            memcpy(&b, columns + off + 2 + k * 2, 2);
            if (b >= numBlocks)
                continue;
            const BlockInfo& bi = blocks[b];
            const uint8_t type = (bi.typeMap >> 4) & 7;
            const bool flat = (bi.typeMap & 0x80) != 0;     // durchsichtige Auflage (Brückenträger, Zaun, Überweg)
            const bool rail = (bi.typeMapExt & 0x80) != 0;  // Gleis
            const bool lanes = (bi.typeMap & 0x0F) != 0;    // Fahrtrichtungen für den Verkehr

            // Straße auf irgendeiner Ebene zählt fürs GPS: Brückendeck, Tunnel unter Gebäuden, flacher Überweg
            if (!rail && (type == 2 || (lanes && type != 1)))
                m_road[i] = 1;

            if (bi.lid && !m_lid[i])
            {
                m_lid[i] = bi.lid;
                m_remap[i] = (bi.typeMapExt >> 3) & 3;
                m_rot[i] = (uint8_t)(bi.typeMap >> 14);
                m_z[i] = (uint8_t)(height + k);
            }
            if (typeDone || rail)
                continue;
            if (type && !flat)
            {
                m_type[i] = type;
                typeDone = true;
            }
            else if (bi.lid && !flat)
            {
                m_type[i] = type; // fester Boden ohne eigenen Typ
                typeDone = true;
            }
        }
    }

    // Kurze Unterbrechungen zwischen zwei Straßenstücken (Überwege als Gehweg, Bahnübergänge) sind befahrbar
    const auto roadAt = [&](int x, int y) { return x >= 0 && y >= 0 && x < 256 && y < 256 && m_road[y * 256 + x] == 1; };
    const auto openAt = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < 256 && y < 256 && m_type[y * 256 + x] != 1 && m_type[y * 256 + x] != 5;
    };
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
        {
            if (m_road[y * 256 + x] || !openAt(x, y))
                continue;
            for (int axis = 0; axis < 2 && !m_road[y * 256 + x]; ++axis)
            {
                const int dx = axis == 0, dy = axis == 1;
                int before = 0, after = 0;
                for (int s = 1; s <= 2 && !before; ++s)
                {
                    if (roadAt(x - dx * s, y - dy * s))
                        before = s;
                    else if (!openAt(x - dx * s, y - dy * s))
                        break;
                }
                for (int s = 1; s <= 2 && !after; ++s)
                {
                    if (roadAt(x + dx * s, y + dy * s))
                        after = s;
                    else if (!openAt(x + dx * s, y + dy * s))
                        break;
                }
                if (before && after && before + after <= 3)
                    m_road[y * 256 + x] = 2;
            }
        }

    // Stadtname aus dem Dateinamen
    std::string file = cmpPath.substr(cmpPath.find_last_of("\\/") + 1);
    std::string lower = file;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return (char)tolower((unsigned char)c); });
    if (lower == "nyc.cmp")
        m_cityName = L"Liberty City";
    else if (lower == "sanb.cmp")
        m_cityName = L"San Andreas";
    else if (lower == "miami.cmp")
        m_cityName = L"Vice City";
    else
        m_cityName.assign(file.begin(), file.end() - std::min<size_t>(4, file.size()));

    char styleName[32];
    sprintf_s(styleName, "style%03u.g24", hdr->style);
    const std::string stylePath = cmpPath.substr(0, cmpPath.find_last_of("\\/") + 1) + styleName;
    m_hasTextures = LoadStyle(stylePath);

    BuildSchematic();
    m_loaded = true;
    LOG("Karte geladen: %s (Style %s, Texturen %s)", cmpPath.c_str(), styleName, m_hasTextures ? "ja" : "nein");
    return true;
}

bool CityMap::LoadStyle(const std::string& path)
{
    std::vector<uint8_t> d;
    if (!ReadWholeFile(path, d) || d.size() < sizeof(G24Header))
    {
        LOG("Style: %s nicht lesbar", path.c_str());
        return false;
    }
    const auto* h = reinterpret_cast<const G24Header*>(d.data());
    if (h->version != 336)
    {
        LOG("Style: %s hat Version %u (erwartet 336)", path.c_str(), h->version);
        return false;
    }

    // Layout wie im Lader von GTA.exe (0x426A70): Kacheln auf 16 KB (4 Kacheln) aufgerundet,
    // dann die Animationen, dann die Farbtabellen (CLUTs) auf 64 KB aufgerundet, dann der Palettenindex
    const size_t tileBytes = (size_t)h->sideSize + h->lidSize + h->auxSize;
    const size_t tileOff = sizeof(G24Header);
    const size_t clutOff = tileOff + ((tileBytes + 0x3FFF) & ~size_t(0x3FFF)) + h->animSize;
    const size_t clutBytes = ((size_t)h->clutSize + 0xFFFF) & ~size_t(0xFFFF);
    const size_t numCluts = clutBytes / 1024;
    const size_t palOff = clutOff + clutBytes;
    if (palOff + h->paletteIndexSize > d.size())
    {
        LOG("Style: %s ist kürzer als erwartet", path.c_str());
        return false;
    }
    const auto* palIndex = reinterpret_cast<const uint16_t*>(d.data() + palOff);
    const size_t numPalIndex = h->paletteIndexSize / 2;

    const int numSide = h->sideSize / 4096;
    const int numLid = h->lidSize / 4096;
    m_tiles.assign((numLid + 1) * 4, Tile());

    bool used[256][4] = {};
    for (int i = 0; i < 65536; ++i)
        used[m_lid[i]][m_remap[i]] = true;

    int loaded = 0;
    for (int key = 4; key < (numLid + 1) * 4 && key < 256 * 4; ++key)
    {
        const int lid = key / 4, remap = key % 4;
        if (!used[lid][remap])
            continue;
        // Deckel L ist Kachel numSide + L (Kachel numSide selbst steht für "kein Deckel"),
        // die Palette gehört zur selben Kachel: palIndex[4 * Kachel + Variante] (vgl. 0x427770/0x4292CF)
        const size_t tile = numSide + lid;
        const size_t palEntry = tile * 4 + remap;
        if (palEntry >= numPalIndex)
            continue;
        const size_t clut = palIndex[palEntry];
        if (clut >= numCluts)
            continue;

        // Je 64 CLUTs teilen sich eine 64-KB-Seite, Einträge verschränkt: Farbe i aller CLUTs hintereinander
        const size_t page = clut / 64;
        const size_t inPage = std::min<size_t>(64, numCluts - page * 64);
        const size_t palBase = clutOff + page * 65536 + (clut % 64) * 4;
        if (palBase + 255 * inPage * 4 + 4 > d.size())
            continue;
        uint32_t pal[256];
        for (int i = 0; i < 256; ++i)
        {
            const uint8_t* e = d.data() + palBase + i * inPage * 4; // B, G, R, 0
            pal[i] = (e[2] << 16) | (e[1] << 8) | e[0];
        }

        // Kacheln liegen zu viert nebeneinander in 256 Pixel breiten Zeilen
        const size_t lastByte = tileOff + ((tile / 4) * 64 + 63) * 256 + (tile % 4) * 64 + 63;
        if (lastByte >= d.size())
            continue;
        Tile& t = m_tiles[key];
        t.mip[0].resize(64 * 64);
        for (int y = 0; y < 64; ++y)
        {
            const uint8_t* src = d.data() + tileOff + ((tile / 4) * 64 + y) * 256 + (tile % 4) * 64;
            for (int x = 0; x < 64; ++x)
                t.mip[0][y * 64 + x] = pal[src[x]];
        }
        for (int level = 1; level < 7; ++level)
        {
            const int s = 64 >> level;
            const auto& prev = t.mip[level - 1];
            auto& cur = t.mip[level];
            cur.resize(s * s);
            for (int y = 0; y < s; ++y)
                for (int x = 0; x < s; ++x)
                {
                    const uint32_t a = prev[(2 * y) * (2 * s) + 2 * x], b = prev[(2 * y) * (2 * s) + 2 * x + 1];
                    const uint32_t c = prev[(2 * y + 1) * (2 * s) + 2 * x], e = prev[(2 * y + 1) * (2 * s) + 2 * x + 1];
                    const uint32_t rb = ((a & 0xFF00FF) + (b & 0xFF00FF) + (c & 0xFF00FF) + (e & 0xFF00FF)) >> 2;
                    const uint32_t g = ((a & 0xFF00) + (b & 0xFF00) + (c & 0xFF00) + (e & 0xFF00)) >> 2;
                    cur[y * s + x] = (rb & 0xFF00FF) | (g & 0xFF00);
                }
        }
        ++loaded;
    }
    LOG("Style: %d Deckel-Texturen aus %s geladen", loaded, path.c_str());
    return loaded > 0;
}

void CityMap::BuildSchematic()
{
    m_schematic.resize(65536);
    for (int i = 0; i < 65536; ++i)
    {
        const int z = m_z[i];
        const uint8_t type = m_type[i] == 0 && m_road[i] ? 2 : m_type[i]; // Überwege ohne eigenen Bodentyp
        m_schematic[i] = type == 5 ? Scale(kTypeColor[5], kHeightShade[z] - 66) : kTypeColor[type];
    }
}

void CityMap::Render(uint32_t* dst, int pitch, int w, int h, float cx, float cy, float ppb, int style) const
{
    if (w <= 0 || h <= 0)
        return;
    const bool textured = style == Textured && m_hasTextures;
    const float inv = 1.0f / ppb;
    const float x0 = cx - w * 0.5f * inv;
    const float y0 = cy - h * 0.5f * inv;

    // Mip-Stufe so wählen, dass etwa ein Texel pro Bildschirmpixel gelesen wird
    int level = 0;
    for (float tpp = 64.0f / ppb; tpp >= 2.0f && level < 6; tpp *= 0.5f)
        ++level;
    const int size = 64 >> level;
    const bool grid = !textured && ppb >= 10.0f;

    m_colBlock.resize(w);
    m_colTex.resize(w);
    for (int x = 0; x < w; ++x)
    {
        const float wx = x0 + (x + 0.5f) * inv;
        const int bx = (int)std::floor(wx);
        m_colBlock[x] = bx;
        m_colTex[x] = std::min(size - 1, (int)((wx - bx) * size));
    }

    int prevBy = INT_MIN;
    for (int y = 0; y < h; ++y)
    {
        uint32_t* row = dst + (size_t)y * pitch;
        const float wy = y0 + (y + 0.5f) * inv;
        const int by = (int)std::floor(wy);
        const int tv = std::min(size - 1, (int)((wy - by) * size));
        const bool rowEdge = grid && by != prevBy;
        prevBy = by;

        if (by < 0 || by > 255)
        {
            std::fill_n(row, w, kOutside);
            continue;
        }

        int prevBx = INT_MIN;
        for (int x = 0; x < w; ++x)
        {
            const int bx = m_colBlock[x];
            const bool colEdge = grid && bx != prevBx;
            prevBx = bx;
            if (bx < 0 || bx > 255)
            {
                row[x] = kOutside;
                continue;
            }
            const int i = by * 256 + bx;
            const size_t key = m_lid[i] * 4u + m_remap[i];
            if (textured && m_lid[i] && key < m_tiles.size() && !m_tiles[key].mip[0].empty())
            {
                // Deckel sind in 90°-Schritten im Uhrzeigersinn gedreht
                int u = m_colTex[x], v = tv;
                switch (m_rot[i])
                {
                case 1: { const int t = u; u = v; v = size - 1 - t; break; }
                case 2: u = size - 1 - u; v = size - 1 - v; break;
                case 3: { const int t = u; u = size - 1 - v; v = t; break; }
                }
                row[x] = Scale(m_tiles[key].mip[level][v * size + u], kHeightShade[m_z[i]]);
            }
            else
            {
                uint32_t c = m_schematic[i];
                if (rowEdge || colEdge)
                    c = Scale(c, 225);
                row[x] = c;
            }
        }
    }
}
