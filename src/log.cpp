#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace logx
{
    static HANDLE s_file = INVALID_HANDLE_VALUE;
    static CRITICAL_SECTION s_lock;
    static DWORD s_start;

    void Write(const char* fmt, ...);

    // Abstürze ins Log: Adresse, Modul, Register und ein Stück Stack. Das erspart bei Fehlerberichten das Raten,
    // ob der Mod oder das Spiel gestolpert ist. Nur schwere Fehler (Zugriffsverletzung, ungültiger Befehl ...).
    static LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
    {
        static LONG s_count = 0;
        const EXCEPTION_RECORD* r = info->ExceptionRecord;
        const DWORD code = r->ExceptionCode;
        if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION &&
            code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW)
            return EXCEPTION_CONTINUE_SEARCH;
        if (InterlockedIncrement(&s_count) > 4)
            return EXCEPTION_CONTINUE_SEARCH;
        const CONTEXT* c = info->ContextRecord;
        char module[MAX_PATH] = "?";
        HMODULE m = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(r->ExceptionAddress), &m) && m)
            GetModuleFileNameA(m, module, MAX_PATH);
        const char* name = strrchr(module, '\\');
        Write("Absturz 0x%08lX bei %p (%s + 0x%lX)%s%p", code, r->ExceptionAddress, name ? name + 1 : module,
              (unsigned long)(reinterpret_cast<uintptr_t>(r->ExceptionAddress) - reinterpret_cast<uintptr_t>(m)),
              code == EXCEPTION_ACCESS_VIOLATION ? (r->ExceptionInformation[0] ? ", Schreiben nach " : ", Lesen von ") : " ",
              code == EXCEPTION_ACCESS_VIOLATION ? reinterpret_cast<void*>(r->ExceptionInformation[1]) : nullptr);
        Write("  eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX", c->Eax, c->Ebx, c->Ecx,
              c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        // Aufrufkette grob: was auf dem Stack nach einer Rücksprungadresse ins Spiel oder in den Mod aussieht
        // (bei einem Stapelüberlauf zeigt sich darin die Schleife). Feste Puffer, der Stack ist dann fast voll.
        static char s_chain[600];
        static HMODULE s_exe = GetModuleHandleW(nullptr), s_self = nullptr;
        if (!s_self)
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&OnException), &s_self);
        const auto inImage = [](HMODULE mod, DWORD v) {
            const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
            return mod && v >= base + 0x1000 && v < base + 0x100000;   // Codeabschnitt am Anfang des Moduls
        };
        int len = 0, found = 0;
        const DWORD* sp = reinterpret_cast<const DWORD*>(c->Esp);
        for (int i = 0; i < 6000 && found < 40 && len < (int)sizeof(s_chain) - 16; ++i)
        {
            if ((i & 1023) == 0 && IsBadReadPtr(sp + i, 4096))
                break;
            const bool self = inImage(s_self, sp[i]);
            if (self || inImage(s_exe, sp[i]))
            {
                len += snprintf(s_chain + len, sizeof(s_chain) - len, self ? " mod+%lX" : " %lX",
                                self ? sp[i] - (DWORD)reinterpret_cast<uintptr_t>(s_self) : sp[i]);
                ++found;
            }
        }
        if (found)
            Write("  Kette:%s", s_chain);
        // Stapelüberlauf: welche Module drehen sich im Kreis? (Codeadressen aller Module, je Modul mit Namen)
        if (code == EXCEPTION_STACK_OVERFLOW)
        {
            len = 0;
            found = 0;
            for (int i = 0; i < 700 && found < 30 && len < (int)sizeof(s_chain) - 48; ++i)
            {
                if ((i & 1023) == 0 && IsBadReadPtr(sp + i, 4096))
                    break;
                HMODULE mod = nullptr;
                if (sp[i] < 0x10000 || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                           reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(sp[i])), &mod) || !mod)
                    continue;
                static char s_name[MAX_PATH];
                GetModuleFileNameA(mod, s_name, MAX_PATH);
                const char* base = strrchr(s_name, '\\');
                len += snprintf(s_chain + len, sizeof(s_chain) - len, " %.12s+%lX", base ? base + 1 : s_name,
                                sp[i] - (DWORD)reinterpret_cast<uintptr_t>(mod));
                ++found;
            }
            if (found)
                Write("  Module:%s", s_chain);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void Open(const wchar_t* path)
    {
        InitializeCriticalSection(&s_lock);
        s_start = GetTickCount();
        s_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        AddVectoredExceptionHandler(1, &OnException);
    }

    void Write(const char* fmt, ...)
    {
        if (s_file == INVALID_HANDLE_VALUE)
            return;

        char buf[1024];
        const DWORD t = GetTickCount() - s_start;
        int n = snprintf(buf, sizeof(buf), "[%5lu.%03lu] ", t / 1000, t % 1000);

        va_list ap;
        va_start(ap, fmt);
        const int avail = (int)sizeof(buf) - n - 2;
        const int m = vsnprintf(buf + n, avail, fmt, ap);
        va_end(ap);
        n += (m < 0) ? 0 : (m >= avail ? avail - 1 : m);

        buf[n++] = '\r';
        buf[n++] = '\n';

        EnterCriticalSection(&s_lock);
        DWORD written;
        WriteFile(s_file, buf, n, &written, nullptr);
        LeaveCriticalSection(&s_lock);
    }
}
