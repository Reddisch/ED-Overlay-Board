# ED Overlay Board

Frei verschiebbare VR-Overlay-Fenster für **Elite Dangerous** (und andere OpenXR-Spiele):
Kneeboard-Seiten, EDDiscovery, EDMC, ein Browser mit Inara/Spansh – direkt im Cockpit.

*English summary: OpenXR API layer + companion app that shows multiple desktop windows and image
"kneeboard" pages as floating panels in VR. Panels can be grabbed, moved and resized with controllers
or hand tracking; recentering via keyboard hotkeys or HOTAS/joystick buttons. Works with any OpenXR
runtime (Virtual Desktop VDXR, SteamVR, Meta/Oculus, WMR, Pimax …); Elite Dangerous needs OpenComposite.*

## Funktionen

- **Mehrere Fenster gleichzeitig**, jedes einzeln konfigurierbar
- **Quellen**
  - `window` – spiegelt ein beliebiges Desktop-Fenster (Windows.Graphics.Capture), optional zugeschnitten
  - `images` – Bilder aus einem Ordner als blätterbare Kneeboard-Seiten (PNG, JPG, BMP, GIF, TIFF, WebP)
- **Greifen & Verschieben** mit Controllern oder **echtem Handtracking** (`XR_EXT_hand_tracking`)
- **Größe ändern** per Thumbstick oder mit beiden Händen (auseinanderziehen)
- **Zentrieren** per Tastatur-Hotkey **oder Joystick-/HOTAS-Taste** (DirectInput, bis zu 128 Tasten)
- Fenster einzeln/alle ein-/ausblenden, Seiten blättern, Interaktion sperren
- **Blickabhängig:** Fenster erscheinen nur, wenn man in ihre Richtung schaut, und sind nur dann bedienbar
- **Einstellungsfenster** (Tray-Icon): Fenster anlegen/bearbeiten, Programm aus Liste wählen, Tastatur- und
  Joystick-Tasten per Tastendruck „lernen“ – alles wirkt sofort
- **VR-Menü** (z. B. Strg+Alt+M): Fenster ein/aus, Zentrieren, Sperren, Schärfe – mit Controller oder Pinch bedienbar
- **Titelleiste ausblenden** und Ränder abschneiden für gespiegelte Fenster
- **Ruhiges Bild:** Inhalte werden per Mipmapping passend zur Displayauflösung vorgefiltert (kein Flimmern bei großen Quellen wie 4K-Kneeboards). `settings.sharpness`: 1.0 = ruhig, 1.5–2 = schärfer
- Layout wird automatisch gespeichert, Zeiger-Cursor, Rahmen beim Anvisieren, Haptik

## Wie es funktioniert

```
 EDOverlayBoard.exe  ──(Shared Memory + geteilte GPU-Texturen)──►  EDOverlayBoard_Layer.dll
 Begleit-App: Fenster-Capture,                                     OpenXR-API-Layer im Spiel:
 Kneeboard, Config, Hotkeys, Joystick                              Quad-Layer, Greifen, Handtracking
```

Die DLL wird vom OpenXR-Loader automatisch in jedes OpenXR-Spiel geladen (wie bei OpenKneeboard).
Sie zeigt nur dann etwas an, wenn die Begleit-App läuft.

## Voraussetzungen

| | |
|---|---|
| Windows 10 (2004+) / 11 | |
| eine **OpenXR-Runtime** | Virtual Desktop (**VDXR**), SteamVR, Meta/Oculus, WMR, Pimax, Varjo … |
| **OpenComposite** für Elite Dangerous | Elite spricht nur OpenVR. OpenComposite übersetzt OpenVR → OpenXR. |
| Spiel nutzt **Direct3D 11** | trifft auf Elite Dangerous zu |

### Elite Dangerous mit OpenComposite einrichten

1. [OpenComposite](https://gitlab.com/znixian/OpenOVR) installieren – entweder systemweit über den
   OpenComposite-Launcher („Switch to OpenComposite“) oder nur für Elite, indem die `openvr_api.dll`
   im Spielordner durch die von OpenComposite ersetzt wird.
2. Für **Virtual Desktop**: in VD als OpenXR-Runtime **VDXR** wählen. SteamVR wird dann nicht mehr benötigt.
3. Elite normal im VR-Modus starten.

## Installation & Start

1. Release-ZIP entpacken (z. B. nach `C:\Tools\EDOverlayBoard`).
2. `EDOverlayBoard.exe` starten – vor oder nach dem Spiel, egal.
   - Beim ersten Start wird der OpenXR-Layer für deinen Benutzer registriert (kein Admin nötig)
     und `config.json` aus `resources\config.example.json` erzeugt.
3. Elite starten – die Fenster erscheinen vor dir. Konsole zeigt `VR connected: …`.

Layer wieder entfernen: `EDOverlayBoard.exe --uninstall-layer`.
Den Layer kurzfristig abschalten: Umgebungsvariable `DISABLE_XR_APILAYER_EDOB_OVERLAY_BOARD=1`.

## Einstellungen

- **Am PC:** Doppelklick auf das Tray-Icon (oder erneuter Start der exe) öffnet das Einstellungsfenster.
  Reiter *Fenster* (anlegen, löschen, Programm auswählen, Titelleiste ausblenden, Position/Größe),
  *Tasten* (Aktion wählen, „Tastatur“/„Joystick“ anklicken und die Taste drücken, Esc = abbrechen),
  *Allgemein* und *Status*. Schließen des Fensters beendet die App nicht – „Beenden“ im Tray-Menü.
- **Im VR:** Strg+Alt+M (oder eine eigene Belegung „VR-Menü ein/aus“) holt das Menü vor dich.
  Controller: Trigger = klicken, Stick = scrollen, Grip = verschieben.
  Handtracking: Pinch = klicken, Pinch auf die orange Kopfleiste oder den Rand = verschieben.
  Pro Fenster lässt sich dort umschalten: sichtbar, verschiebbar und (bei Programmfenstern) klickbar.
- **Sprache:** Deutsch oder Englisch, per Schieberegler im Reiter *Allgemein* oder im VR-Menü
  (Standard: Windows-Anzeigesprache).

## Bedienung im VR

| Aktion | Controller | Handtracking |
|---|---|---|
| Fenster greifen/verschieben | Grip halten, Hand bewegen | Pinch (Daumen + Zeigefinger) halten |
| Am Rand greifen | Grip oder Trigger auf dem Rahmen | Pinch auf dem Rahmen |
| Programmfenster bedienen (z. B. EDCoPilot): klicken, ziehen | Trigger (halten = Maustaste halten) | Pinch (halten = Maustaste halten) |
| In Programmfenstern scrollen | beim Zeigen Stick hoch/runter | Scrollbalken greifen und ziehen |
| Kneeboard blättern | Trigger (rechte/linke Hälfte = vor/zurück) | kurzer Pinch |
| Größe ändern | beim Greifen Stick hoch/runter | mit zweiter Hand ebenfalls greifen und auseinanderziehen |
| Abstand näher/weiter | beim Greifen Stick links/rechts | – |
| Zwei-Hand-Skalierung | beide Grips am selben Fenster | beide Hände pinchen |

Um jedes verschiebbare Fenster liegt ein Greifrand, der beim Zeigen als oranger Rahmen erscheint
(hell = hier wird gegriffen). Auf dem Rand wird nie geklickt. Nicht verschiebbare Fenster haben keinen Rahmen.

Bedienbare Programmfenster (`forwardClicks`, im VR-Menü „klickbar“) verhalten sich wie das VR-Menü:
Trigger bzw. Pinch ist die linke Maustaste, verschoben wird mit Grip oder über den Rand. Die Maus-Eingaben
gehen als Fensternachrichten an das Programm: Das Spiel behält den Fokus, die echte Maus bewegt sich nicht.
Funktioniert mit EDCoPilot; Programme, die als Administrator laufen, nehmen die Eingaben nicht an.

Controller funktionieren, sobald das Spiel Controller-Bindings anmeldet (OpenComposite tut das).
Der Layer hängt seine Aktionen nur an Controller-Profile an, die das Spiel selbst nutzt, und verändert
die Spielsteuerung nicht. Handtracking wird genutzt, wenn die Runtime `XR_EXT_hand_tracking` kann
(VDXR, Meta, …) und für die jeweilige Hand kein Controller aktiv ist.

## Konfiguration (`config.json`)

```json
{
  "settings": { "language": "de", "showCursor": true, "haptics": true, "scaleSpeed": 1.0, "pushPullSpeed": 0.75,
                "lookToShow": true, "lookMarginDeg": 10, "sharpness": 1.0 },
  "windows": [
    { "name": "Kneeboard", "type": "images", "folder": "kneeboard",
      "position": [-0.3, -0.35, -0.45], "rotation": [-40, 30, 0], "width": 0.3 },
    { "name": "EDDiscovery", "type": "window", "processName": "EDDiscovery.exe",
      "position": [0.5, 0.05, -0.75], "rotation": [0, -35, 0], "width": 0.6 },
    { "name": "Browser", "type": "window", "titleContains": "Spansh",
      "crop": [0, 120, 1920, 940], "width": 0.7 }
  ],
  "bindings": [
    { "action": "recenter", "key": "Ctrl+Alt+Numpad5" },
    { "action": "recenter", "joystick": "VKB", "button": 12 },
    { "action": "toggleWindow", "window": "Kneeboard", "key": "Ctrl+Alt+Numpad1" }
  ]
}
```

### Fenster (`windows`, max. 16)

| Feld | Bedeutung |
|---|---|
| `name` | Anzeigename, wird von Bindings referenziert |
| `type` | `window` oder `images` |
| `processName` | (window) Prozessname, z. B. `EDDiscovery.exe`, `EDMarketConnector.exe`, `msedge.exe` |
| `titleContains` | (window) Teil des Fenstertitels (Groß-/Kleinschreibung egal) |
| `hideTitleBar` | (window) nur den Inhaltsbereich zeigen – ohne Titelleiste und Rahmen |
| `trim` | (window) zusätzlich Ränder abschneiden `[links, oben, rechts, unten]` in Pixeln |
| `crop` | (window) Ausschnitt `[x, y, breite, höhe]` in Pixeln, z. B. um Browser-Leisten abzuschneiden |
| `captureCursor` | (window) Mauszeiger mit aufnehmen |
| `forwardClicks` | (window) im VR mit Trigger/Pinch/Stick bedienbar – klicken, ziehen, scrollen (Standard `true`) |
| `folder` / `images` | (images) Bilderordner bzw. Dateiliste, relativ zur config.json |
| `position` | Meter relativ zur zentrierten Kopfposition: x = rechts, y = oben, −z = vorne |
| `rotation` | Grad: `[Neigung, Drehung, Rollen]` |
| `width` | Breite in Metern (Höhe ergibt sich aus dem Seitenverhältnis) |
| `opacity` | 0.05 – 1.0 (wenn die Runtime `XR_KHR_composition_layer_color_scale_bias` kann) |
| `visible` | beim Start sichtbar |
| `locked` | Fenster kann nicht gegriffen werden (kein Rahmen; ein Pinch klickt sofort) |
| `lookToShow` | `true` = nur sichtbar und bedienbar, wenn man in seine Richtung schaut; `false` = immer sichtbar (Standard: `settings.lookToShow`) |

Position, Rotation, Größe, Sichtbarkeit und Seite werden beim Verschieben automatisch zurückgeschrieben.
Das gespiegelte Fenster darf hinter dem Spiel liegen, aber **nicht minimiert** sein.

### Tasten (`bindings`)

| `action` | Wirkung |
|---|---|
| `recenter` | Fenster vor der aktuellen Blickrichtung neu ausrichten |
| `toggleAll` | alle Fenster ein-/ausblenden |
| `toggleWindow` | ein Fenster (`window`) ein-/ausblenden |
| `nextPage` / `prevPage` | Kneeboard-Seite blättern (`window`, leer = alle) |
| `toggleInteraction` | Greifen sperren/entsperren (gegen versehentliches Verschieben) |
| `toggleMenu` | VR-Menü ein-/ausblenden |
| `openSettings` | Einstellungsfenster am PC öffnen |

- **Tastatur:** `"key": "Ctrl+Alt+Numpad5"` – Modifier `Ctrl`, `Alt`, `Shift`, `Win`; Tasten `A`–`Z`, `0`–`9`,
  `F1`–`F24`, `Numpad0`–`Numpad9`, `NumpadAdd`, `Home`, `End`, `PageUp`, `Insert`, `Pause`, …
  Systemweit registriert, funktioniert also auch, wenn Elite den Fokus hat.
- **Joystick:** `"joystick": "<Teil des Gerätenamens>", "button": <Nummer>`. Namen und Nummern anzeigen:
  ```
  EDOverlayBoard.exe --joysticks
  ```
  Die Abfrage ist nicht exklusiv: dieselbe Taste kann in Elite auf *„HMD-Ausrichtung zurücksetzen“* liegen.

## Fehlersuche

- App-Log: `EDOverlayBoard.log` neben der exe
- Layer-Log (im Spiel): `%LOCALAPPDATA%\EDOverlayBoard\layer-<spiel>.log`
- Keine Fenster im Spiel → steht im Layer-Log überhaupt etwas? Wenn nicht, läuft das Spiel nicht über
  OpenXR (OpenComposite prüfen) oder der Layer ist nicht registriert (App einmal starten).
- `cannot open shared texture … different GPU` → App und Spiel laufen auf verschiedenen Grafikkarten
  (Laptop mit iGPU): beiden in den Windows-Grafikeinstellungen dieselbe GPU zuweisen.

## Selbst bauen

Voraussetzungen: Visual Studio 2022 mit „Desktopentwicklung mit C++“, Windows SDK 10.0.19041+,
CMake ≥ 3.20 (in Visual Studio enthalten).

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

OpenXR-SDK-Header und nlohmann/json werden beim Konfigurieren automatisch (versioniert) geladen.
Ergebnis in `build\bin\Release\`: `EDOverlayBoard.exe`, `EDOverlayBoard_Layer.dll`, `EDOverlayBoard_Layer.json`.

Hinweis: Solange ein OpenXR-Spiel läuft, ist die Layer-DLL gesperrt und kann nicht neu gebaut werden.

Ein Git-Tag `v*` lässt die GitHub Action ein Release-ZIP erzeugen.

## Projektstruktur

```
src/app/      Begleit-App
  main.cpp                 Kommandozeile, Layer-Registrierung beim Start
  App.*                    Hauptschleife, geteilte Texturen, Aktionen, Speichern
  WindowCaptureSource.*    Fenster spiegeln (Windows.Graphics.Capture)
  ImageSource.*            Kneeboard-Bilder (WIC)
  HotkeyManager.*          globale Tastatur-Hotkeys
  JoystickManager.*        DirectInput-Joystick-Tasten
  LayerRegistration.*      OpenXR-Implicit-Layer in HKCU eintragen
  Config.*, D3D.*          config.json, D3D11 + Platzhalter-Panels
src/layer/    OpenXR-API-Layer (DLL)
  Layer.cpp                Loader-Verhandlung, abgefangene OpenXR-Funktionen
  OverlaySession.*         Swapchains, Quad-Layer, Greifen/Skalieren/Zentrieren
  Input.*                  eigene Actions (in die Spiel-Bindings eingehängt), Handtracking
src/common/   Ipc.h (Shared-Memory-Protokoll), VrMath.h, Log
resources/    EDOverlayBoard_Layer.json (Layer-Manifest), config.example.json
```

## Roadmap

- Direct3D 12 / Vulkan im Layer (für andere Spiele)
- Rechtsklick und Doppelklick in gespiegelten Fenstern
- PDF-Seiten als Kneeboard
- Fenster an Kopf/Hand anheften

## Rechtliches

Lizenz: MIT (siehe [LICENSE](LICENSE)). Verwendet [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
[nlohmann/json](https://github.com/nlohmann/json) (MIT) und die OpenXR-SDK-Header (Apache 2.0).
Elite Dangerous ist eine Marke von Frontier Developments plc. Dieses Projekt ist ein inoffizielles
Fan-Werkzeug und steht in keiner Verbindung zu Frontier Developments, Valve, Khronos oder Virtual Desktop.
Inspiriert von OpenKneeboard, aber eigenständig entwickelt.
