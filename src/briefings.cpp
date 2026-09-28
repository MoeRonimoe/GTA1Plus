#include "briefings.h"
#include "gta1.h"

#include <deque>
#include <windows.h>

namespace briefings
{
    namespace
    {
        constexpr size_t kMax = 40;
        std::deque<std::wstring> g_texts;   // vorne die neueste
        unsigned g_mapGen = ~0u;
        CRITICAL_SECTION g_lock;
        bool g_lockReady = false;

        // Neues Level: alte Ansagen gehören nicht mehr dazu
        void CheckLevel()
        {
            const unsigned gen = gta1::LevelLoads();
            if (gen != g_mapGen)
            {
                g_mapGen = gen;
                g_texts.clear();
            }
        }

        struct Lock
        {
            Lock()
            {
                if (!g_lockReady)
                {
                    InitializeCriticalSection(&g_lock);
                    g_lockReady = true;
                }
                EnterCriticalSection(&g_lock);
            }
            ~Lock() { LeaveCriticalSection(&g_lock); }
        };
    }

    void OnBrief(int textId)
    {
        std::wstring text;
        if (!gta1::LookupText(textId, text))
            return;
        // Steuerzeichen der Spieltexte als Leerzeichen
        for (wchar_t& c : text)
            if (c < 32)
                c = L' ';
        Lock lock;
        CheckLevel();
        if (!g_texts.empty() && g_texts.front() == text)
            return; // dieselbe Ansage wiederholt (z.B. "zu den Telefonen im Park")
        g_texts.push_front(text);
        if (g_texts.size() > kMax)
            g_texts.pop_back();
    }

    int Count()
    {
        Lock lock;
        CheckLevel();
        return (int)g_texts.size();
    }

    std::wstring Text(int i)
    {
        Lock lock;
        return i >= 0 && i < (int)g_texts.size() ? g_texts[i] : std::wstring();
    }
}
