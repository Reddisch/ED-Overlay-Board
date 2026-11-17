#pragma once

#include "XrIncludes.h"

#include <string>
#include <vector>

// Spectator mirror for streaming / recording (OBS): the game's picture of one eye with the
// overlay windows drawn on top, the way they appear in the headset. The overlays reach the
// headset as separate quad layers that only the runtime composes, so the game's own mirror
// window never shows them. This renders a flat picture into a shared texture (keyed mutex)
// that the companion app shows in a desktop window.
//
// Runs on the game's immediate context; the game's pipeline state is saved and restored with
// SwapDeviceContextState.
class Mirror {
 public:
  struct Eye {
    ID3D11Texture2D* texture = nullptr;  // swapchain image of the game
    uint32_t arrayIndex = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // format the game created the swapchain with (typed)
    int32_t x = 0, y = 0, width = 0, height = 0;  // part of the image with this eye's picture
    XrFovf fov{};
    Pose pose;  // eye in LOCAL space
  };
  struct Quad {
    ID3D11ShaderResourceView* srv = nullptr;
    Pose pose;  // LOCAL space
    float width = 0, height = 0;
    float uv[4]{0, 0, 1, 1};  // u0 v0 u1 v1
    bool blend = false;       // use the texture's alpha
    float alpha = 1.0f;
  };

  Mirror(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t pid);
  ~Mirror();
  Mirror(const Mirror&) = delete;
  Mirror& operator=(const Mirror&) = delete;

  // Renders one picture. Returns true when a new picture is in the shared texture.
  bool Render(const Eye& eye, const std::vector<Quad>& quads, uint32_t width, uint32_t height, float zoom);
  void Release();             // mirror switched off: free the shared texture
  void ForgetSourceViews();   // game swapchain destroyed: drop views onto its images

  uint32_t Generation() const { return shared_ ? generation_ : 0; }
  uint32_t Width() const { return width_; }
  uint32_t Height() const { return height_; }
  const std::string& Status() const { return status_; }
  void SetStatus(std::string s) { status_ = std::move(s); }

 private:
  bool InitPipeline();
  bool EnsureTarget(uint32_t width, uint32_t height);
  ID3D11ShaderResourceView* SourceView(const Eye& eye);

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11Device1> device1_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11DeviceContext1> context1_;
  uint32_t pid_;

  bool pipelineTried_ = false, pipelineOk_ = false;
  ComPtr<ID3DDeviceContextState> state_;
  ComPtr<ID3D11VertexShader> vsFull_, vsQuad_;
  ComPtr<ID3D11PixelShader> psFull_, psQuad_;
  ComPtr<ID3D11Buffer> cb_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11BlendState> blend_, opaque_;
  ComPtr<ID3D11RasterizerState> raster_;

  // render target (sRGB view) and the shared copy the app reads
  ComPtr<ID3D11Texture2D> target_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  ComPtr<ID3D11Texture2D> shared_;
  ComPtr<IDXGIKeyedMutex> mutex_;
  HANDLE handle_ = nullptr;
  uint32_t width_ = 0, height_ = 0;
  uint32_t generation_ = 0;

  // views onto the game's swapchain images (or a copy, if the images cannot be sampled)
  struct SourceViewEntry {
    ID3D11Texture2D* texture;
    uint32_t arrayIndex;
    DXGI_FORMAT format;
    ComPtr<ID3D11ShaderResourceView> srv;
  };
  std::vector<SourceViewEntry> views_;
  ComPtr<ID3D11Texture2D> copy_;
  ComPtr<ID3D11ShaderResourceView> copySrv_;
  std::string loggedSource_;

  std::string status_;
};
