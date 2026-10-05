#include "parallel.h"

#include <windows.h>
#include <algorithm>

namespace parallel
{
    namespace
    {
        constexpr int kMaxHelpers = 7;

        HANDLE s_start = nullptr;   // Semaphore: ein Zähler je Helfer, der mitarbeiten soll
        HANDLE s_done = nullptr;    // gesetzt, wenn der letzte Helfer fertig ist
        int    s_helpers = 0;

        // Der laufende Auftrag
        void (*s_fn)(int, int, void*) = nullptr;
        void* s_ctx = nullptr;
        int   s_count = 0, s_chunk = 1;
        volatile LONG s_next = 0;      // nächster freier Abschnitt
        volatile LONG s_pending = 0;   // Helfer, die noch nicht fertig sind

        void RunChunks()
        {
            for (;;)
            {
                const LONG first = InterlockedExchangeAdd(&s_next, s_chunk);
                if (first >= s_count)
                    return;
                s_fn(first, std::min<int>(first + s_chunk, s_count), s_ctx);
            }
        }

        DWORD WINAPI Helper(void*)
        {
            for (;;)
            {
                WaitForSingleObject(s_start, INFINITE);
                RunChunks();
                if (InterlockedDecrement(&s_pending) == 0)
                    SetEvent(s_done);
            }
        }

        // Helfer beim ersten Gebrauch anlegen: einer je weiterem Prozessorkern
        void Start()
        {
            static bool s_started = false;
            if (s_started)
                return;
            s_started = true;
            SYSTEM_INFO info = {};
            GetSystemInfo(&info);
            const int want = std::clamp((int)info.dwNumberOfProcessors - 1, 0, kMaxHelpers);
            s_start = CreateSemaphoreW(nullptr, 0, kMaxHelpers, nullptr);
            s_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!s_start || !s_done)
                return;
            for (int i = 0; i < want; ++i)
            {
                if (HANDLE thread = CreateThread(nullptr, 0, &Helper, nullptr, 0, nullptr))
                {
                    CloseHandle(thread);
                    ++s_helpers;
                }
            }
        }
    }

    void For(int count, int chunk, void (*fn)(int first, int last, void* ctx), void* ctx)
    {
        Start();
        if (count <= 0)
            return;
        s_fn = fn;
        s_ctx = ctx;
        s_count = count;
        s_chunk = std::max(1, chunk);
        s_next = 0;
        // Nicht mehr Helfer wecken, als es außer dem ersten weitere Abschnitte gibt
        const int helpers = std::min(s_helpers, (count - 1) / s_chunk);
        s_pending = helpers;
        if (helpers > 0)
            ReleaseSemaphore(s_start, helpers, nullptr);
        RunChunks();
        if (helpers > 0)
            WaitForSingleObject(s_done, INFINITE);
    }

    int Threads()
    {
        Start();
        return s_helpers + 1;
    }
}
