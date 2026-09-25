#pragma once

// Feuer löschen: Brennt die Spielfigur, wirft sie sich auf die Sprungtaste zu Boden, wälzt sich und
// erstickt so die Flammen. Im Originalspiel ist Brennen der sichere Tod.
namespace fire
{
    // Einmal beim Start (installiert die Aussetzung des Brennschadens)
    void Init();

    // Sprungtaste (gehalten) aus der Eingabe des Frames; true = der Sprung wird fürs Löschen verwendet
    bool OnJump(bool held);

    // Spiel-Thread, einmal je Frame direkt vor dem Zeichnen der Welt
    void BeforeRender();

    // Figur wälzt sich gerade am Boden (Eingaben sperren)
    bool Rolling();

    // Figur brennt und kann sich jetzt zu Boden werfen (Hinweis anzeigen)
    bool CanRoll();
}
