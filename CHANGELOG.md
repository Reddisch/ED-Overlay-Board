# Changelog

## Unreleased

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
