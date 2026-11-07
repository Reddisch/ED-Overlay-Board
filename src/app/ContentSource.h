#pragma once

#include <d3d11.h>

#include <string>

// Something that produces the picture shown on an overlay window.
class ContentSource {
 public:
  virtual ~ContentSource() = default;

  // Called once per frame. Returns the texture to show (never null once initialized);
  // sets `changed` when the texture or its contents changed and must be re-submitted.
  virtual ID3D11Texture2D* Update(bool& changed) = 0;

  // Trigger click on the overlay; x/y are in [-0.5, 0.5] (x right, y up).
  virtual void OnClick(float /*x*/, float /*y*/) {}

  // Mouse-like input from VR (sources with mouse input: VR menu, clickable program windows).
  // Coordinates 0..1, origin top-left. Called pointer first, then buttons, then wheel.
  virtual void SetPointer(bool /*active*/, float /*x*/, float /*y*/) {}
  virtual void AddButton(bool /*down*/) {}
  virtual void AddWheel(float /*steps*/) {}
  virtual void NextPage() {}
  virtual void PrevPage() {}
  virtual int Page() const { return 0; }
  virtual std::string Status() const { return {}; }  // human readable, for the settings window
};
