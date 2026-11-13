## Neu in 0.5.0

- **VR-Menü geht mit dem Kopf mit:** immer in der Mitte des Blickfelds, etwa 55 cm entfernt, aufrecht und
  leicht geglättet (ruhiger Text). Es lässt sich nicht mehr greifen, verschieben oder in der Größe ändern –
  die Größe kommt aus `menuWidth` in den Einstellungen.
- **Blickinformation für andere Programme:** Der Layer meldet je Fenster, ob es gerade angesehen wird
  (Shared Memory `Local\EDOverlayBoard.Gaze.v1`, siehe `src/common/GazeIpc.h`). Das nutzt z. B.
  [ED Road to Riches](https://github.com/Reddisch/ED-Road-to-Riches) für die Joystick-Steuerung seines VR-Fensters.

App und Layer müssen beide auf 0.5.0 sein: nach dem Update App und Elite neu starten.

## New in 0.5.0

- **VR menu follows the head:** always in the middle of the view at about 55 cm, upright and slightly smoothed
  (calm text). It can no longer be grabbed, moved or resized – its size comes from `menuWidth` in the settings.
- **Gaze information for other programs:** the layer publishes per window whether it is looked at
  (shared memory `Local\EDOverlayBoard.Gaze.v1`, see `src/common/GazeIpc.h`), used e.g. by
  [ED Road to Riches](https://github.com/Reddisch/ED-Road-to-Riches) for the joystick control of its VR window.

App and layer must both be 0.5.0: restart the app and Elite after updating.
