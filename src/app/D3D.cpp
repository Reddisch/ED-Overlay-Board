#include "D3D.h"

#include "Log.h"

#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <windows.graphics.directx.direct3d11.interop.h>

bool D3D::Init(int adapterIndex) {
  winrt::com_ptr<IDXGIAdapter1> adapter;
  if (adapterIndex >= 0) {
    winrt::com_ptr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void())))
      factory->EnumAdapters1(static_cast<UINT>(adapterIndex), adapter.put());
  }
  if (adapter) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    Log("D3D11: using adapter {} ({})", adapterIndex, [&] {
      std::string s;
      for (wchar_t c : std::wstring(desc.Description)) s += c < 128 ? static_cast<char>(c) : '?';
      return s;
    }());
  }

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(adapter.get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, static_cast<UINT>(std::size(levels)),
                                 D3D11_SDK_VERSION, device_.put(), nullptr, context_.put());
  if (FAILED(hr)) {
    Log("D3D11CreateDevice failed: 0x{:08X}", static_cast<uint32_t>(hr));
    return false;
  }

  if (auto mt = device_.try_as<ID3D11Multithread>()) mt->SetMultithreadProtected(TRUE);

  auto dxgiDevice = device_.as<IDXGIDevice>();
  winrt::com_ptr<::IInspectable> inspectable;
  hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put());
  if (FAILED(hr)) {
    Log("CreateDirect3D11DeviceFromDXGIDevice failed: 0x{:08X}", static_cast<uint32_t>(hr));
    return false;
  }
  winrtDevice_ = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put());
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.put()));
  return true;
}

winrt::com_ptr<ID3D11Texture2D> D3D::CreateTexture(uint32_t w, uint32_t h, const void* bgraPixels, bool renderTarget) {
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w;
  d.Height = h;
  d.MipLevels = 1;
  d.ArraySize = 1;
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (renderTarget ? D3D11_BIND_RENDER_TARGET : 0u);
  d.MiscFlags = 0;

  D3D11_SUBRESOURCE_DATA init{bgraPixels, w * 4, 0};
  winrt::com_ptr<ID3D11Texture2D> tex;
  HRESULT hr = device_->CreateTexture2D(&d, bgraPixels ? &init : nullptr, tex.put());
  if (FAILED(hr)) {
    Log("CreateTexture2D {}x{} failed: 0x{:08X}", w, h, static_cast<uint32_t>(hr));
    return nullptr;
  }
  return tex;
}

namespace {
// Full-screen triangle; the pixel shader multiplies the color channels, alpha stays.
constexpr char kTintHlsl[] = R"(
cbuffer Tint : register(b0) { float4 tint; };
Texture2D<float4> src : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ps(float4 pos : SV_Position) : SV_Target {
  float4 c = src.Load(int3(pos.xy, 0));
  return float4(c.rgb * tint.rgb, c.a);
}
)";

winrt::com_ptr<ID3DBlob> Compile(const char* entry, const char* target) {
  winrt::com_ptr<ID3DBlob> code, errors;
  HRESULT hr = D3DCompile(kTintHlsl, sizeof(kTintHlsl) - 1, "tint", nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
  if (FAILED(hr)) {
    Log("Tint shader {}: compile failed 0x{:08X} {}", entry, static_cast<uint32_t>(hr),
        errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    return nullptr;
  }
  return code;
}
}  // namespace

bool D3D::CopyTinted(ID3D11Texture2D* dst, ID3D11Texture2D* src, const float rgb[3]) {
  if (!tintInit_) {
    tintInit_ = true;
    auto vs = Compile("vs", "vs_5_0");
    auto ps = Compile("ps", "ps_5_0");
    if (vs) device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, tintVs_.put());
    if (ps) device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, tintPs_.put());
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device_->CreateBuffer(&bd, nullptr, tintCb_.put());
  }
  if (!tintVs_ || !tintPs_ || !tintCb_) return false;

  winrt::com_ptr<ID3D11ShaderResourceView> srv;
  winrt::com_ptr<ID3D11RenderTargetView> rtv;
  if (FAILED(device_->CreateShaderResourceView(src, nullptr, srv.put()))) return false;
  if (FAILED(device_->CreateRenderTargetView(dst, nullptr, rtv.put()))) return false;

  D3D11_TEXTURE2D_DESC d{};
  dst->GetDesc(&d);
  const float cb[4] = {rgb[0], rgb[1], rgb[2], 1.0f};
  auto* ctx = context_.get();
  ctx->UpdateSubresource(tintCb_.get(), 0, nullptr, cb, 0, 0);

  D3D11_VIEWPORT vp{0, 0, static_cast<float>(d.Width), static_cast<float>(d.Height), 0, 1};
  ID3D11RenderTargetView* rtvs[] = {rtv.get()};
  ID3D11ShaderResourceView* srvs[] = {srv.get()};
  ID3D11Buffer* cbs[] = {tintCb_.get()};
  ctx->ClearState();
  ctx->OMSetRenderTargets(1, rtvs, nullptr);
  ctx->RSSetViewports(1, &vp);
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(tintVs_.get(), nullptr, 0);
  ctx->PSSetShader(tintPs_.get(), nullptr, 0);
  ctx->PSSetShaderResources(0, 1, srvs);
  ctx->PSSetConstantBuffers(0, 1, cbs);
  ctx->Draw(3, 0);
  ctx->ClearState();  // unbind src/dst (ImGui sets its whole state itself)
  return true;
}

winrt::com_ptr<ID3D11Texture2D> D3D::CreateTextTexture(const std::wstring& title, const std::wstring& body, uint32_t w,
                                                       uint32_t h) {
  auto tex = CreateTexture(w, h, nullptr, true);
  if (!tex || !d2d_ || !dwrite_) return tex;

  auto surface = tex.as<IDXGISurface>();
  auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  winrt::com_ptr<ID2D1RenderTarget> rt;
  if (FAILED(d2d_->CreateDxgiSurfaceRenderTarget(surface.get(), &props, rt.put()))) return tex;

  winrt::com_ptr<ID2D1SolidColorBrush> orange, text;
  rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.55f, 0.1f), orange.put());
  rt->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.88f, 0.9f), text.put());

  winrt::com_ptr<IDWriteTextFormat> titleFmt, bodyFmt;
  dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, h * 0.11f, L"", titleFmt.put());
  dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, h * 0.065f, L"", bodyFmt.put());

  const float fw = static_cast<float>(w), fh = static_cast<float>(h);
  rt->BeginDraw();
  rt->Clear(D2D1::ColorF(0.05f, 0.05f, 0.07f, 1.0f));
  rt->DrawRectangle(D2D1::RectF(4, 4, fw - 4, fh - 4), orange.get(), 4.0f);
  if (titleFmt)
    rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), titleFmt.get(),
                  D2D1::RectF(fw * 0.04f, fh * 0.06f, fw * 0.96f, fh * 0.3f), orange.get());
  if (bodyFmt)
    rt->DrawTextW(body.c_str(), static_cast<UINT32>(body.size()), bodyFmt.get(),
                  D2D1::RectF(fw * 0.04f, fh * 0.32f, fw * 0.96f, fh * 0.94f), text.get());
  rt->EndDraw();
  return tex;
}
