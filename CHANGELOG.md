# Changelog

## Unreleased

- New program **ED Overlay Board Starter** (`EDOverlayBoardStarter.exe`, own Start menu entry and optional
  desktop shortcut in the installer): starts Elite, the VR mode and any number of helper programs in one click.
  VR modes (Desktop, Virtual Desktop/VDXR, SteamVR, Meta Horizon Link, other OpenXR runtimes) switch the active
  OpenXR runtime when needed (one UAC prompt, done on a worker thread) and start the VR program; programs have
  path, arguments, working folder, delay, timeout, "wait for VR" and running detection by process name(s) or window
  title. Detects Steam, Elite, runtimes, VoiceAttack and EDCoPilot on the first start (`starter.json`). Desktop
  shortcut per VR mode, command line `--start --mode <name>`

## 0.8.0

- Transparent windows that also work with VDXR: the opacity is now baked into the alpha of the
  texture (same shader as the filter color) instead of relying on
  `XR_KHR_composition_layer_color_scale_bias`. New per-window switch `transparent`, action
  `toggleTransparency` (one window or all) and a button in the VR menu
- No pointer dot on program windows that are not clickable (only moved with the controller /
  joystick); kneeboard pages keep it (where you click turns the page). IPC v11: update app and
  restart Elite
- Kneeboard pages in a panel frame like the VR menu: dark panel with chamfered corners, orange outline
  and a header bar with the window name and the page number. Per-window switch `frame` (settings, VR
  menu "Window look"), action `toggleFrame`

## 0.7.0

- Spectator window for streaming / recording (OBS): the overlay windows reach the headset as separate
  quad layers that only the VR runtime composes, so Elite's mirror window (and OBS) never showed them.
  The layer now renders the left eye of the game with all overlay windows on top into a shared texture,
  the app shows it in the window "ED Overlay Board - Mirror" (add it in OBS as Window Capture).
  Settings `mirror` (off by default), `mirrorWidth` / `mirrorHeight` (1920 x 1080), `mirrorZoom`;
  *General* tab. The game's D3D11 state is saved / restored around the extra pass. IPC v10: update app
  and restart Elite

## 0.6.1

- Aim with the head: the VR menu now checks horizontal and vertical separately whether you are still
  looking at it (width / height of the menu). Before, the smaller side (the width) was used for both, so
  looking down at the bottom row ("Close menu") made the menu run away. Only the layer changes (no IPC
  change): restart Elite after updating

## 0.6.0

- Layout per ship type: window positions, size and visibility are remembered per ship type
  (and separately for SRV, fighter, on foot and taxi). The ship comes from the Elite journal
  (`LoadGame` / `Loadout` / `ShipyardSwap` / `ShipyardNew`), the vehicle from `Status.json`.
  A ship seen for the first time starts with the layout of the last ship. Setting
  `perShipLayouts` (on by default), layouts in `shipLayouts`; *General* tab shows the detected
  ship and lets you delete saved layouts. Only the app changes
- Sounds when windows appear / disappear, matching the show effect (hologram shimmer, zoom whoosh,
  CRT power-on / power-off, a short blip without effect). Settings `showSoundVolume` (0 = off) and
  `showSoundOnLook` (also when looking at / away, off by default); preview when choosing an effect.
  Sounds are now mixed (waveOut), so a click no longer cuts off another sound. Only the app changes
- Pointer smoothing for controllers and hand tracking (setting `pointerSmoothing`, 0-1, default 0.5,
  slider in *General*): adaptive "One Euro" filter (calm while aiming, little lag when moving fast) and
  click steadying: a trigger press / pinch uses the ray from just before it and holds it until the hand
  moves a few degrees, so clicks land where you aimed. Also helps with Virtual Desktop, which turns
  hand tracking into simulated controllers. IPC v8: restart the app and Elite
- Aim with the head (setting `headAim`, checkbox in *General* and in the VR menu): the pointer follows
  the center of the view, trigger / pinch clicks; grabbing and moving windows still uses the hand.
  The dot shows while a hand / controller is tracked. The VR menu then holds still while you look
  around on it and only catches up after looking clearly beside it. IPC v9: restart the app and Elite
- Windows only in the game (setting `onlyInGame`, on by default, checkbox in *General* and in the VR
  menu): in the main menu and while loading only the VR menu is shown. From the journal: `LoadGame` =
  in the game; a new journal, main menu music or `Shutdown` = not. The windows appear with their show
  effect and sound. Only the app changes
- App and layer must both be 0.6.0 (IPC v9): restart the app and Elite after updating

## 0.5.0

- VR menu follows the head: always in the middle of the view at about 55 cm, upright, slightly
  smoothed (1° dead zone, so the text stays calm). It can no longer be grabbed, moved or resized in VR
  (no grab border, header bar is only a title); its size comes from `menuWidth`. Layer + app change
- Gaze information for other programs: the layer publishes per window whether the view direction
  hits it, in the read-only shared memory `Local\EDOverlayBoard.Gaze.v1` (`src/common/GazeIpc.h`).
  Independent of the app/layer protocol (IPC version stays 7, only the layer changes)

## 0.4.0

- Swipe to scroll in program windows (trigger or pinch, like a touch screen, with fling);
  hold still for about 0.5 s first for a normal mouse drag. Per window `dragScroll`, speed `swipeSpeed`
- Horizontal scrolling with the thumbstick
- PC keyboard into a VR window (`toggleKeyboard`, VR menu button): keys go to the program window
  clicked last (blue border), Elite keeps the focus; hotkeys and injected keys still work
- Joystick bindings can be button combinations on one device (`"buttons": [5, 12]`, learned by
  holding the buttons together); a button that is also part of a combination fires alone on release
- Scrolling in SDL/Kivy programs (EDCoPilot) and Tk programs
- IPC version 7 (app and layer must match)

## 0.3.0

- Installer (Inno Setup, per user, no admin rights): registers the OpenXR layer, optional desktop
  shortcut and autostart; uninstalling removes the layer registration
- App runs without a console window, has its own icon and version info
- Data folder falls back to `%LOCALAPPDATA%\EDOverlayBoard` when the program folder is read-only
- Per-window filter color and brightness (dims bright pictures, works with every runtime incl. VDXR)
- Per-window show/hide effects: hologram, zoom, CRT TV
- Click sound for clicks in VR (volume adjustable, 0 = off)
- VR menu: window look section (filter color, brightness, effect); height follows its content
- Mouse input for program windows (click, drag, wheel), German/English UI
- Example config: first window renamed to "Klemmbrett"
- Note: this program was created with the help of AI (Claude by Anthropic)

## 0.2.0

- First version: OpenXR API layer + companion app, window capture, image pages, grabbing with
  controllers and hand tracking, hotkeys and joystick buttons, VR menu
