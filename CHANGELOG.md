# Changelog

## Unreleased

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
