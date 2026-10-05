#pragma once

// Arbeit über viele Bildzeilen auf mehrere Prozessorkerne verteilen
namespace parallel
{
    // Ruft fn(first, last, ctx) für Abschnitte von je "chunk" aus [0, count) auf, auch auf Helfer-Threads, und
    // kehrt zurück, wenn alle Abschnitte fertig sind. Nur von einem Thread aus aufrufen (dem des Spiels); fn muss
    // mit verschiedenen Abschnitten gleichzeitig laufen können.
    void For(int count, int chunk, void (*fn)(int first, int last, void* ctx), void* ctx);

    int Threads();   // so viele Threads arbeiten mit (mit dem aufrufenden)
}
