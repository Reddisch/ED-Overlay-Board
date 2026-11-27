## Neu in 0.10.0

**Behoben: Controller in Elite (EDVR) funktionieren wieder ohne weiteres Overlay.** Elite nutzt über EDVR keine
eigene Controller-Eingabe (nur Maus, Tastatur, HOTAS). Der Layer hängte seine Controller-Aktionen bisher nur an die
eines anderen Programms an – das hatte zufällig OpenKneeboard übernommen. Ohne OpenKneeboard fehlten Zeiger-Punkt,
Klicks und Greifen. Jetzt meldet der Layer seine Controller selbst an, wenn das Spiel etwa 5 Sekunden nach dem
VR-Start keine eigenen hat.

**Starter:**
- **Kompaktansicht (VR):** kleines Fenster mit einem großen Knopf pro VR-Modus (Klick = sofort starten), mittig auf
  dem Hauptmonitor – gut zu bedienen in der Desktop-Ansicht von Virtual Desktop, SteamVR oder Meta Horizon Link.
- **Immer im Vordergrund** bis zum Start; wenn alles läuft, bekommt Elite den Fokus.
- **Sprachausgabe** (optional): eine Windows-Stimme sagt, was passiert – Runtime-Umstellung mit Hinweis auf die
  Admin-Abfrage, jedes Programm, das läuft oder nicht startet, und das Ergebnis.
- Kein Einfrieren mehr direkt nach dem Klick (Elite über Steam blockierte das Fenster einige Sekunden).

Der Layer ist neu: **Elite nach dem Update einmal neu starten.**

**Installation:** `EDOverlayBoard-Setup-<Version>.exe` herunterladen und ausführen. Meldet Windows
„Der Computer wurde durch Windows geschützt“: **Weitere Informationen** → **Trotzdem ausführen**.
Der Installer ist nicht kostenpflichtig signiert; er wird von GitHub Actions aus dem öffentlichen
Quellcode gebaut.

---

## New in 0.10.0

**Fixed: controllers in Elite (EDVR) work again without another overlay.** Through EDVR, Elite has no controller
input of its own (mouse, keyboard, HOTAS only). The layer used to add its controller actions only to someone
else's – which OpenKneeboard happened to do. Without OpenKneeboard the pointer dot, clicks and grabbing were gone.
Now the layer attaches its own controller actions when the game has none about 5 seconds after the VR start.

**Starter:**
- **Compact view (VR):** small window with one big button per VR mode (click = start right away), centered on the
  main monitor – easy to use in the desktop view of Virtual Desktop, SteamVR or Meta Horizon Link.
- **Always on top** until you start; when everything runs, Elite gets the focus.
- **Speech output** (optional): a Windows voice says what happens – runtime switch with a hint to confirm the admin
  prompt, each program that runs or failed, and the result.
- No more freeze right after the click (Elite via Steam blocked the window for a few seconds).

The layer is new: **restart Elite once after the update.**

**Installation:** download and run `EDOverlayBoard-Setup-<version>.exe`. If Windows says
"Windows protected your PC": **More info** → **Run anyway**. The installer has no paid code signing
certificate; GitHub Actions builds it from the public source code.
