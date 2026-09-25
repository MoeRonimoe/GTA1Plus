#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Draufsicht einer GTA1-Stadt, gebaut aus der .cmp-Karte und der passenden styleXXX.g24
class CityMap
{
public:
    enum Style { Textured = 0, Schematic = 1 };

    bool Load(const std::string& cmpPath);
    bool Loaded() const { return m_loaded; }
    bool HasTextures() const { return m_hasTextures; }
    const std::string& Path() const { return m_path; }
    const wchar_t* CityName() const { return m_cityName.c_str(); }

    // Rendert den Ausschnitt um (centerX, centerY) [Blöcke] mit pixelsPerBlock nach dst (pitch in Pixeln)
    void Render(uint32_t* dst, int pitch, int w, int h, float centerX, float centerY, float pixelsPerBlock, int style) const;

    // Oberflächentyp (0 Luft, 1 Wasser, 2 Straße, 3 Gehweg, 4 Wiese, 5 Gebäude) und Höhe der Deckelfläche
    uint8_t Surface(int x, int y) const { return m_type[(y & 255) * 256 + (x & 255)]; }
    uint8_t LidLevel(int x, int y) const { return m_z[(y & 255) * 256 + (x & 255)]; }

    // Befahrbare Straße auf irgendeiner Ebene: Brücken, Tunnel, Überwege und Bahnübergänge eingeschlossen
    bool IsRoad(int x, int y) const { return m_road[(y & 255) * 256 + (x & 255)] != 0; }

private:
    struct Tile
    {
        std::vector<uint32_t> mip[7]; // 64x64 .. 1x1
    };

    bool LoadStyle(const std::string& path);
    void BuildSchematic();

    bool m_loaded = false;
    bool m_hasTextures = false;
    std::string m_path;
    std::wstring m_cityName;

    uint8_t m_lid[256 * 256] = {};   // Deckel-Textur des obersten sichtbaren Blocks (1-basiert, 0 = keine)
    uint8_t m_remap[256 * 256] = {}; // Paletten-Variante 0..3 (Bit 3-4 von type_map_ext)
    uint8_t m_rot[256 * 256] = {};   // Drehung der Deckel-Textur in 90°-Schritten im Uhrzeigersinn
    uint8_t m_type[256 * 256] = {};  // 0 Luft, 1 Wasser, 2 Straße, 3 Gehweg, 4 Wiese, 5 Gebäude
    uint8_t m_z[256 * 256] = {};     // Höhe der Deckelfläche (0 = ganz oben, 5 = ganz unten)
    uint8_t m_road[256 * 256] = {};  // 1 Straße in der Spalte, 2 kurze Lücke zwischen Straßen
    std::vector<uint32_t> m_schematic;
    std::vector<Tile> m_tiles;       // Index lid * 4 + remap

    mutable std::vector<int> m_colBlock, m_colTex;
};
