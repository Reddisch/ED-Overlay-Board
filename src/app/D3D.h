#pragma once

#include <Unknwn.h>
#include <d2d1.h>
#include <d3d11.h>
#include <dwrite.h>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <cstdint>
#include <string>

// Owns the D3D11 device used for capture copies and overlay textures, plus a tiny
// Direct2D/DirectWrite helper to render placeholder/status panels.
class D3D {
 public:
  bool Init(int adapterIndex);  // -1 = default adapter

  ID3D11Device* Device() const { return device_.get(); }
  ID3D11DeviceContext* Context() const { return context_.get(); }
  const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice& WinRTDevice() const { return winrtDevice_; }

  winrt::com_ptr<ID3D11Texture2D> CreateTexture(uint32_t w, uint32_t h, const void* bgraPixels = nullptr,
                                                bool renderTarget = false);
  winrt::com_ptr<ID3D11Texture2D> CreateTextTexture(const std::wstring& title, const std::wstring& body,
                                                    uint32_t w = 800, uint32_t h = 400);

 private:
  winrt::com_ptr<ID3D11Device> device_;
  winrt::com_ptr<ID3D11DeviceContext> context_;
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_{nullptr};
  winrt::com_ptr<ID2D1Factory> d2d_;
  winrt::com_ptr<IDWriteFactory> dwrite_;
};
