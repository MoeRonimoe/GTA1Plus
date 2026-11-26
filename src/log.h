#pragma once

namespace logx
{
    void Open(const wchar_t* path);
    void Write(const char* fmt, ...);

    // Diagnose: Stelle merken (der Absturzmelder nennt die letzte) und, mit EnableHeapCheck, alle Heaps des Prozesses
    // prüfen. Die erste Beschädigung kommt mit dieser und der letzten einwandfreien Stelle ins Log. where muss ein
    // fester Text sein.
    void HeapCheck(const char* where);
    void EnableHeapCheck(bool on);
}

#define LOG(...) logx::Write(__VA_ARGS__)
