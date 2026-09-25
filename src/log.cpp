#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace logx
{
    static HANDLE s_file = INVALID_HANDLE_VALUE;
    static CRITICAL_SECTION s_lock;
    static DWORD s_start;

    void Open(const wchar_t* path)
    {
        InitializeCriticalSection(&s_lock);
        s_start = GetTickCount();
        s_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
