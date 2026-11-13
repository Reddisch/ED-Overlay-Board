# ED Overlay Board

[Deutsch](README.de.md) | **English**

Freely movable VR overlay windows for **Elite Dangerous** (and other OpenXR games):
kneeboard pages, EDCoPilot, EDDiscovery, EDMC, a browser with Inara/Spansh – right in the cockpit.

An OpenXR API layer plus a companion app: several desktop windows and images ("kneeboard" pages) float
as panels in VR. You can grab, move, resize and use them (click, scroll, type) with controllers or hand
tracking; recenter with a keyboard hotkey or a HOTAS/joystick button. Works with any OpenXR runtime
(Virtual Desktop VDXR, SteamVR, Meta/Oculus, WMR, Pimax …).

> **Note:** This program was created with the help of AI (Claude by Anthropic) – code, texts and
> installer. It was tested by the author with Quest 3, Virtual Desktop (VDXR) and Elite Dangerous.

## Features

- **Several windows at once**, each configured separately
- **Sources**
  - `window` – mirrors any desktop window (Windows.Graphics.Capture), optionally cropped
  - `images` – pictures from a folder as kneeboard pages you can turn (PNG, JPG, BMP, GIF, TIFF, WebP)
- **Grab & move** with controllers or **real hand tracking** (`XR_EXT_hand_tracking`)
- **Resize** with the thumbstick or with both hands (pull apart)
- **Use program windows in VR:** click, drag, scroll with the thumbstick or **swipe like on a phone**
  (with fling) – with controllers and hand tracking
- **PC keyboard into a VR window:** type into a program window while Elite keeps the focus
- **Recenter** with a keyboard hotkey **or a joystick/HOTAS button** (DirectInput, up to 128 buttons,
  also **button combinations** such as 5 + 12)
- Show/hide single windows or all of them, turn pages, lock interaction
- **Gaze dependent:** windows only appear (and can only be used) when you look towards them
- **Gaze information for other programs:** which window you are looking at, in the read-only shared memory
  `Local\EDOverlayBoard.Gaze.v1` (`src/common/GazeIpc.h`) – e.g. for the joystick control of
  [ED Road to Riches](https://github.com/Reddisch/ED-Road-to-Riches)
- **Settings window** (tray icon): add/edit windows, pick a program from a list, "learn" keyboard and
  joystick buttons by pressing them – everything applies immediately
- **VR menu** (e.g. Ctrl+Alt+M): windows on/off, recenter, lock, keyboard, sharpness, brightness,
  effects – usable with controllers or pinch
- **Dimming with a filter color** per window (e.g. against bright PNGs) – works with every runtime, including VDXR
- **Show effects** per window: hologram, zoom, CRT TV
- **Click sound** for clicks in VR (adjustable volume)
- **German / English**
- **Hide the title bar** and trim the edges of mirrored windows
- **Calm picture:** content is pre-filtered with mipmaps to match the display resolution (no shimmer with
  large sources such as 4K kneeboards). `settings.sharpness`: 1.0 = calm, 1.5–2 = sharper
- Layout is saved automatically, pointer cursor, frame when pointing at a window, haptics

## How it works

```
 EDOverlayBoard.exe  ──(shared memory + shared GPU textures)──►  EDOverlayBoard_Layer.dll
 companion app: window capture,                                  OpenXR API layer in the game:
 kneeboard, config, hotkeys, joystick                            quad layers, grabbing, hand tracking
```

The OpenXR loader loads the DLL into every OpenXR game automatically (like OpenKneeboard).
It only shows something while the companion app is running. App and layer must be the same version.

## Requirements

| | |
|---|---|
| Windows 10 (2004+) / 11 | |
| an **OpenXR runtime** | Virtual Desktop (**VDXR**), SteamVR, Meta/Oculus, WMR, Pimax, Varjo … |
| Elite Dangerous via **OpenXR** | Elite only speaks OpenVR – e.g. **OpenComposite** translates OpenVR → OpenXR (or another way to OpenXR) |
| the game uses **Direct3D 11** | true for Elite Dangerous |

### Setting up Elite Dangerous with OpenComposite

1. Install [OpenComposite](https://gitlab.com/znixian/OpenOVR) – either system-wide with the
   OpenComposite launcher ("Switch to OpenComposite") or for Elite only, by replacing the
   `openvr_api.dll` in the game folder with the one from OpenComposite.
2. For **Virtual Desktop**: choose **VDXR** as the OpenXR runtime in VD. SteamVR is then no longer needed.
3. Start Elite in VR mode as usual.

## Installation & start

**With the installer (recommended):**

1. Download `EDOverlayBoard-Setup-<version>.exe` from the Releases page and run it.
   No administrator rights are needed: it installs to `%LOCALAPPDATA%\Programs\ED Overlay Board`
   and registers the OpenXR layer for your user. Optional: desktop shortcut and "Start with Windows".
2. ED Overlay Board then runs in the notification area of the taskbar (tray icon).
3. Start Elite – the windows appear in front of you. The *Status* tab then shows "Connected to …".

After an update, restart the app and Elite.

Uninstall via Windows Settings → Apps. This unregisters the layer; `config.json` and your pictures
stay in the installation folder.

Windows SmartScreen may warn on the first start because the installer is not signed
("More info" → "Run anyway").

**Without the installer (ZIP):** extract the release ZIP (e.g. to `C:\Tools\EDOverlayBoard`) and start
`EDOverlayBoard.exe`. On the first start it registers the layer and creates `config.json` from
`resources\config.example.json`. To remove the layer again: `EDOverlayBoard.exe --uninstall-layer`.

`config.json`, the log and the picture folders are next to the exe. If that folder is read-only
(e.g. under `C:\Program Files`), the app uses `%LOCALAPPDATA%\EDOverlayBoard` instead.
The exact location is shown in the *Status* tab under "About".

To switch the layer off temporarily: environment variable `DISABLE_XR_APILAYER_EDOB_OVERLAY_BOARD=1`.

## Settings

- **On the PC:** double-click the tray icon (or start the exe again) to open the settings window.
  Tabs *Windows* (add, delete, choose a program, hide title bar, position/size, swiping),
  *Bindings* (choose an action, click "Keyboard"/"Joystick" and press the key or button(s), Esc = cancel),
  *General* (click sound, swipe speed, …) and *Status*. Closing the window does not quit the app –
  use "Quit" in the tray menu.
- **In VR:** Ctrl+Alt+M (or your own binding for "VR menu on/off") shows the menu in the middle of
  your view, about 55 cm away. It cannot be moved: it goes along with your head (slightly smoothed,
  always upright). Its size is `menuWidth`.
  Controllers: trigger = click, thumbstick = scroll. Hand tracking: pinch = click.
  For each window you can switch: visible, movable and (for program windows) clickable.
  Under "Window look" (choose the window with `<` `>`): filter color, brightness and show effect.
  The button "PC keyboard into window" switches keyboard mode on and off.
- **Language:** German or English, with the slider in the *General* tab or in the VR menu
  (default: Windows display language).

## Using it in VR

| Action | Controllers | Hand tracking |
|---|---|---|
| Grab/move a window | hold grip, move the hand | hold a pinch (thumb + index finger) |
| Grab at the border | grip or trigger on the frame | pinch on the frame |
| Use a program window (e.g. EDCoPilot): click | trigger | short pinch |
| Scroll in program windows | thumbstick up/down/sideways, or hold trigger and swipe | hold a pinch and swipe |
| Drag with the mouse (sliders, scroll bars, selecting text) | hold the trigger still for about 0.5 s, then drag | hold the pinch still for about 0.5 s, then drag |
| Turn kneeboard pages | trigger (right/left half = next/previous) | short pinch |
| Resize | thumbstick up/down while grabbing | grab with the second hand too and pull apart |
| Closer/farther | thumbstick left/right while grabbing | – |
| Two-handed scaling | both grips on the same window | pinch with both hands |

Every movable window has a grab margin around it that shows up as an orange frame while you point at it
(bright = this grabs). The margin never clicks. Windows that cannot be moved have no frame.

Usable program windows (`forwardClicks`, "clickable" in the VR menu) behave like the VR menu: trigger or
pinch is the left mouse button, you move them with grip or by the border. The mouse input goes to the
program as window messages: the game keeps the focus and the real mouse does not move. Works with
browsers, EDCoPilot and most Win32/WPF/WinForms programs; programs running as administrator do not
accept the input.

**Swipe to scroll** (`dragScroll`, on by default): hold the trigger/pinch and drag to scroll the content
like on a phone (also sideways); letting go while moving keeps it scrolling for a moment. Hold still for
about 0.5 s first, then drag, for a normal mouse drag. Speed: `swipeSpeed` (*General* tab). Some programs
(e.g. EDCoPilot) scroll in whole mouse-wheel notches, so swiping there is a bit steppier.

**PC keyboard into a VR window** (action `toggleKeyboard`, e.g. Ctrl+Alt+K, or the VR menu button): the
keys of the real keyboard go to the program window you clicked last in VR (blue frame); Elite keeps the
focus but does not get the keys. Trigger it again = back to Elite. Your own hotkeys keep working; keys
simulated by programs such as VoiceAttack or EDCoPilot still go to the game. Clicking another program
window sends the keyboard there. The mode only runs while the game runs in VR and ends by itself when
the game is closed. Typing text works in most programs; some programs do not recognize Ctrl/Alt
shortcuts this way.

Controllers work as soon as the game registers controller bindings (OpenComposite does). The layer
only adds its actions to controller profiles the game itself uses and does not change the game
controls. Hand tracking is used when the runtime supports `XR_EXT_hand_tracking` (VDXR, Meta, …) and no
controller is active for that hand.

## Configuration (`config.json`)

Everything can be set in the settings window; you do not need to edit the file by hand.

```json
{
  "settings": { "language": "en", "showCursor": true, "haptics": true, "clickVolume": 0.5, "scaleSpeed": 1.0,
                "pushPullSpeed": 0.75, "swipeSpeed": 1.0, "lookToShow": true, "lookMarginDeg": 10,
                "sharpness": 1.0 },
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
    { "action": "toggleMenu", "joystick": "T.Flight", "buttons": [5, 12] },
    { "action": "toggleKeyboard", "key": "Ctrl+Alt+K" },
    { "action": "toggleWindow", "window": "Kneeboard", "key": "Ctrl+Alt+Numpad1" }
  ]
}
```

### General (`settings`)

| Field | Meaning |
|---|---|
| `language` | `de`, `en` or empty = Windows display language |
| `showCursor` | show the pointer dot on the windows |
| `haptics` | controller vibration |
| `clickVolume` | click sound volume, 0 = off |
| `scaleSpeed` / `pushPullSpeed` | speed of resizing / changing the distance with the thumbstick |
| `swipeSpeed` | swipe scrolling speed |
| `lookToShow` / `lookMarginDeg` | only show windows when looking at them / extra angle around a window |
| `sharpness` | 1.0 = calm picture, 1.5–2 = sharper, may shimmer |
| `menuWidth` | width of the VR menu in meters |

### Windows (`windows`, max. 15)

| Field | Meaning |
|---|---|
| `name` | display name, referenced by bindings |
| `type` | `window` or `images` |
| `processName` | (window) process name, e.g. `EDDiscovery.exe`, `EDMarketConnector.exe`, `msedge.exe` |
| `titleContains` | (window) part of the window title (case-insensitive) |
| `hideTitleBar` | (window) show only the content area – without title bar and border |
| `trim` | (window) additionally cut off edges `[left, top, right, bottom]` in pixels |
| `crop` | (window) region `[x, y, width, height]` in pixels, e.g. to cut off browser bars |
| `captureCursor` | (window) include the mouse cursor |
| `forwardClicks` | (window) usable in VR with trigger/pinch/thumbstick – click, drag, scroll (default `true`) |
| `dragScroll` | (window) hold and swipe scrolls like on a touch screen (default `true`) |
| `folder` / `images` | (images) picture folder or file list, relative to config.json |
| `position` | meters relative to the recentered head position: x = right, y = up, −z = forward |
| `rotation` | degrees: `[pitch, yaw, roll]` |
| `width` | width in meters (the height follows from the aspect ratio) |
| `opacity` | 0.05 – 1.0 (if the runtime supports `XR_KHR_composition_layer_color_scale_bias`) |
| `tint` | filter color `[r, g, b]` (0 – 1), multiplied onto the content; white = unchanged, e.g. `[1.0, 0.62, 0.25]` = amber |
| `brightness` | 0.1 – 1.0, dims the content further (works with every runtime, including VDXR) |
| `effect` | show effect: `none`, `hologram`, `zoom` or `tv` (also runs when hiding and when looking towards/away) |
| `visible` | visible at start |
| `locked` | the window cannot be grabbed (no frame; a pinch clicks immediately) |
| `lookToShow` | `true` = only visible and usable when looking towards it; `false` = always visible (default: `settings.lookToShow`) |

Position, rotation, size, visibility and page are written back automatically when you move things.
The mirrored window may be behind the game but must **not be minimized**.

### Bindings (`bindings`)

| `action` | Effect |
|---|---|
| `recenter` | realign the windows in front of the current view direction |
| `toggleAll` | show/hide all windows |
| `toggleWindow` | show/hide one window (`window`) |
| `nextPage` / `prevPage` | turn a kneeboard page (`window`, empty = all) |
| `toggleInteraction` | lock/unlock grabbing (against moving things by accident) |
| `toggleMenu` | show/hide the VR menu |
| `toggleKeyboard` | PC keyboard to the program window clicked last in VR / back to the game |
| `openSettings` | open the settings window on the PC |

- **Keyboard:** `"key": "Ctrl+Alt+Numpad5"` – modifiers `Ctrl`, `Alt`, `Shift`, `Win`; keys `A`–`Z`, `0`–`9`,
  `F1`–`F24`, `Numpad0`–`Numpad9`, `NumpadAdd`, `Home`, `End`, `PageUp`, `Insert`, `Pause`, …
  Registered system-wide, so it also works while Elite has the focus.
- **Joystick:** `"joystick": "<part of the device name>", "button": <number>`. To show names and numbers:
  ```
  EDOverlayBoard.exe --joysticks
  ```
  Buttons are not read exclusively: the same button can also be bound in Elite, e.g. to *"Reset HMD orientation"*.
  **Combinations** on one device: `"buttons": [5, 12]` (in the settings window: hold the buttons together,
  then release). If a button is bound alone and also in a combination, the single binding fires on
  release – and only if the combination was not used meanwhile (e.g. button 5 as a "shift" button).

## Troubleshooting

- App log: `EDOverlayBoard.log` in the data folder (next to the exe, or `%LOCALAPPDATA%\EDOverlayBoard`)
- Layer log (in the game): `%LOCALAPPDATA%\EDOverlayBoard\layer-<game>.log`
- No windows in the game → is there anything in the layer log at all? If not, the game does not run via
  OpenXR (check OpenComposite) or the layer is not registered (start the app once).
- App and layer do not connect → are both the same version? After an update, restart the app and the game.
- `cannot open shared texture … different GPU` → app and game run on different graphics cards
  (laptop with iGPU): assign both the same GPU in the Windows graphics settings.
- Scrolling/clicking does not reach a program → does the app log show "mouse wheel" or "click at"?
  Programs running as administrator do not accept the input.

## Building it yourself

Requirements: Visual Studio 2022 with "Desktop development with C++", Windows SDK 10.0.19041+,
CMake ≥ 3.20 (included in Visual Studio).

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

The OpenXR SDK headers and nlohmann/json are downloaded automatically (pinned versions) during configure.
Output in `build\bin\Release\`: `EDOverlayBoard.exe`, `EDOverlayBoard_Layer.dll`, `EDOverlayBoard_Layer.json`.

Note: while an OpenXR game is running, the layer DLL is locked and cannot be rebuilt.

**Building the installer** (also needs [Inno Setup 6](https://jrsoftware.org/isdl.php)):

```
powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
```

Output: `dist\EDOverlayBoard-Setup-<version>.exe`. A git tag `v*` makes the GitHub Action create a
release with the ZIP and the installer.

## Project structure

```
src/app/      companion app
  main.cpp                 command line, layer registration at start
  App.*                    main loop, shared textures, actions, saving
  WindowCaptureSource.*    window mirroring (Windows.Graphics.Capture), mouse input, swiping
  ImageSource.*            kneeboard pictures (WIC)
  HotkeyManager.*          global keyboard hotkeys
  KeyboardCapture.*        PC keyboard into a VR window (low-level keyboard hook)
  JoystickManager.*        DirectInput joystick buttons and combinations
  LayerRegistration.*      registers the OpenXR implicit layer in HKCU
  Config.*, D3D.*          config.json, D3D11, placeholder panels, filter color (shader)
  Gui.*                    settings window and VR menu (Dear ImGui)
  ClickSound.*             click sound
src/layer/    OpenXR API layer (DLL)
  Layer.cpp                loader negotiation, intercepted OpenXR functions
  OverlaySession.*         swapchains, quad layers, grabbing/scaling/recentering
  Input.*                  own actions (merged into the game's bindings), hand tracking
src/common/   Ipc.h (shared memory protocol), VrMath.h, Log
resources/    EDOverlayBoard_Layer.json (layer manifest), config.example.json, icon
installer/    Inno Setup script and installer build script
```

## Roadmap

- Direct3D 12 / Vulkan in the layer (for other games)
- Right-click and double-click in mirrored windows
- PDF pages as kneeboard
- Pin windows to the head/hand

## Legal

License: MIT (see [LICENSE](LICENSE)). Uses [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
[nlohmann/json](https://github.com/nlohmann/json) (MIT) and the OpenXR SDK headers (Apache 2.0).
Elite Dangerous is a trademark of Frontier Developments plc. This project is an unofficial fan tool and
is not affiliated with Frontier Developments, Valve, Khronos or Virtual Desktop.
Inspired by OpenKneeboard, but developed independently.

Made with the help of AI: the source code, the texts and the installer were written together with
Claude (Anthropic); idea, requirements, testing and publishing: Andreas Rettig.
