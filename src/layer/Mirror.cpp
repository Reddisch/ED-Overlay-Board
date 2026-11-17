#include "Mirror.h"

#include "Ipc.h"
#include "Log.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace {

constexpr float kNear = 0.05f;  // meters: quad parts closer to the eye are cut off

constexpr char kHlsl[] = R"(
cbuffer Params : register(b0) {
  float4 corners[4];  // quad: clip-space corners (top-left, top-right, bottom-left, bottom-right)
  float4 uvRect;      // u0 v0 u1 v1 (quad: part of its texture, background: part of the eye image)
  float4 fov;         // background: tan of the eye's left, right, down, up angles
  float4 view;        // x, y = tan of the mirror's half angles; z = alpha; w = 1: ignore texture alpha
};
SamplerState smp : register(s0);
Texture2D quadTex : register(t0);
Texture2DArray eyeTex : register(t1);

struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VsOut vsFull(uint id : SV_VertexID) {
  VsOut o;
  float2 t = float2((id << 1) & 2, id & 2);
  o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
  o.uv = o.pos.xy;
  return o;
}
float4 psFull(VsOut i) : SV_Target {
  float2 tanXY = i.uv * view.xy;
  float u = (tanXY.x - fov.x) / (fov.y - fov.x);
  float v = (fov.w - tanXY.y) / (fov.w - fov.z);
  float2 st = lerp(uvRect.xy, uvRect.zw, float2(u, v));
  return float4(eyeTex.SampleLevel(smp, float3(st, 0), 0).rgb, 1);
}

VsOut vsQuad(uint id : SV_VertexID) {
  VsOut o;
  o.pos = corners[id];
  o.uv = lerp(uvRect.xy, uvRect.zw, float2(id & 1, id >> 1));
  return o;
}
float4 psQuad(VsOut i) : SV_Target {
  float4 c = quadTex.Sample(smp, i.uv);
  c.a = (view.w > 0.5 ? 1.0 : c.a) * view.z;
  return c;
}
)";

struct Params {
  float corners[4][4];
  float uvRect[4];
  float fov[4];
  float view[4];
};

ComPtr<ID3DBlob> Compile(const char* entry, const char* target) {
  ComPtr<ID3DBlob> code, errors;
  HRESULT hr = D3DCompile(kHlsl, sizeof(kHlsl) - 1, "mirror", nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf());
  if (FAILED(hr)) {
    Log("Mirror shader {}: compile failed 0x{:08X} {}", entry, static_cast<uint32_t>(hr),
        errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    return nullptr;
  }
  return code;
}

bool IsTypeless(DXGI_FORMAT f) {
  switch (f) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      return true;
    default:
      return false;
  }
}

}  // namespace

Mirror::Mirror(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t pid)
    : device_(device), context_(context), pid_(pid) {
  device_.As(&device1_);
  context_.As(&context1_);
}

Mirror::~Mirror() { Release(); }

void Mirror::Release() {
  rtv_.Reset();
  target_.Reset();
  mutex_.Reset();
  shared_.Reset();
  if (handle_) CloseHandle(handle_);
  handle_ = nullptr;
  width_ = height_ = 0;
  ForgetSourceViews();
}

void Mirror::ForgetSourceViews() {
  views_.clear();
  copy_.Reset();
  copySrv_.Reset();
}

bool Mirror::InitPipeline() {
  if (pipelineTried_) return pipelineOk_;
  pipelineTried_ = true;
  if (!device1_ || !context1_) {
    SetStatus("D3D11.1 not available");
    return false;
  }
  // Own pipeline state: the game's bindings stay untouched (swapped out and back in).
  const D3D_FEATURE_LEVEL level = device_->GetFeatureLevel();
  const UINT flags = (device_->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                         ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                         : 0;
  if (FAILED(device1_->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr,
                                                state_.GetAddressOf()))) {
    SetStatus("CreateDeviceContextState failed");
    return false;
  }
  auto vsF = Compile("vsFull", "vs_5_0"), psF = Compile("psFull", "ps_5_0");
  auto vsQ = Compile("vsQuad", "vs_5_0"), psQ = Compile("psQuad", "ps_5_0");
  if (!vsF || !psF || !vsQ || !psQ) {
    SetStatus("shader compile failed (d3dcompiler_47.dll missing?)");
    return false;
  }
  device_->CreateVertexShader(vsF->GetBufferPointer(), vsF->GetBufferSize(), nullptr, vsFull_.GetAddressOf());
  device_->CreatePixelShader(psF->GetBufferPointer(), psF->GetBufferSize(), nullptr, psFull_.GetAddressOf());
  device_->CreateVertexShader(vsQ->GetBufferPointer(), vsQ->GetBufferSize(), nullptr, vsQuad_.GetAddressOf());
  device_->CreatePixelShader(psQ->GetBufferPointer(), psQ->GetBufferSize(), nullptr, psQuad_.GetAddressOf());

  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = sizeof(Params);
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  device_->CreateBuffer(&bd, nullptr, cb_.GetAddressOf());

  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_ANISOTROPIC;
  sd.MaxAnisotropy = 8;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  device_->CreateSamplerState(&sd, sampler_.GetAddressOf());

  D3D11_BLEND_DESC bl{};
  bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  device_->CreateBlendState(&bl, opaque_.GetAddressOf());
  bl.RenderTarget[0].BlendEnable = TRUE;
  bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
  bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
  bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  device_->CreateBlendState(&bl, blend_.GetAddressOf());

  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  device_->CreateRasterizerState(&rd, raster_.GetAddressOf());

  pipelineOk_ = vsFull_ && psFull_ && vsQuad_ && psQuad_ && cb_ && sampler_ && blend_ && opaque_ && raster_;
  if (!pipelineOk_) SetStatus("cannot create the render pipeline");
  return pipelineOk_;
}

bool Mirror::EnsureTarget(uint32_t width, uint32_t height) {
  if (shared_ && width_ == width && height_ == height) return true;
  Release();

  D3D11_TEXTURE2D_DESC d{};
  d.Width = width;
  d.Height = height;
  d.MipLevels = 1;
  d.ArraySize = 1;
  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;  // blending in linear light, like the compositor
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(device_->CreateTexture2D(&d, nullptr, target_.GetAddressOf())) ||
      FAILED(device_->CreateRenderTargetView(target_.Get(), nullptr, rtv_.GetAddressOf()))) {
    SetStatus(std::format("cannot create render target {}x{}", width, height));
    Release();
    return false;
  }

  d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // same bytes; plain format for the app's swapchain copy
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  d.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  ComPtr<IDXGIResource1> res;
  const uint32_t gen = generation_ + 1;
  HRESULT hr = device_->CreateTexture2D(&d, nullptr, shared_.GetAddressOf());
  if (SUCCEEDED(hr)) hr = shared_.As(&res);
  if (SUCCEEDED(hr))
    hr = res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                 ipc::MirrorTextureName(pid_, gen).c_str(), &handle_);
  if (SUCCEEDED(hr)) hr = shared_.As(&mutex_);
  if (FAILED(hr)) {
    SetStatus(std::format("cannot create shared texture {}x{} (0x{:08X})", width, height, static_cast<uint32_t>(hr)));
    Release();
    return false;
  }
  generation_ = gen;
  width_ = width;
  height_ = height;
  Log("Mirror: shared texture {}x{} created (generation {})", width, height, gen);
  return true;
}

ID3D11ShaderResourceView* Mirror::SourceView(const Eye& eye) {
  D3D11_TEXTURE2D_DESC d{};
  eye.texture->GetDesc(&d);
  const DXGI_FORMAT format = eye.format != DXGI_FORMAT_UNKNOWN && !IsTypeless(eye.format) ? eye.format : d.Format;
  if (IsTypeless(format)) {
    SetStatus(std::format("unknown format of the game picture ({})", static_cast<int>(d.Format)));
    return nullptr;
  }
  const bool direct = (d.BindFlags & D3D11_BIND_SHADER_RESOURCE) && d.SampleDesc.Count == 1;
  const std::string info = std::format("{}x{} format {} (created as {}), {} slice(s), {} sample(s), bind 0x{:X} -> {}",
                                       d.Width, d.Height, static_cast<int>(format), static_cast<int>(d.Format),
                                       d.ArraySize, d.SampleDesc.Count, d.BindFlags, direct ? "sampled directly" : "copied");
  if (info != loggedSource_) {
    loggedSource_ = info;
    Log("Mirror: game picture {}", info);
  }

  if (direct) {
    for (const auto& v : views_)
      if (v.texture == eye.texture && v.arrayIndex == eye.arrayIndex && v.format == format) return v.srv.Get();
    if (views_.size() >= 16) views_.clear();
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = format;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sv.Texture2DArray.MostDetailedMip = 0;
    sv.Texture2DArray.MipLevels = 1;
    sv.Texture2DArray.FirstArraySlice = eye.arrayIndex;
    sv.Texture2DArray.ArraySize = 1;
    SourceViewEntry e{eye.texture, eye.arrayIndex, format, nullptr};
    if (FAILED(device_->CreateShaderResourceView(eye.texture, &sv, e.srv.GetAddressOf()))) {
      SetStatus("cannot read the game picture (CreateShaderResourceView)");
      return nullptr;
    }
    views_.push_back(std::move(e));
    return views_.back().srv.Get();
  }

  // Not readable by shaders (or multisampled): copy / resolve this eye's slice first.
  D3D11_TEXTURE2D_DESC cd{};
  if (copy_) copy_->GetDesc(&cd);
  if (!copy_ || cd.Width != d.Width || cd.Height != d.Height || cd.Format != format) {
    copy_.Reset();
    copySrv_.Reset();
    cd = {};
    cd.Width = d.Width;
    cd.Height = d.Height;
    cd.MipLevels = 1;
    cd.ArraySize = 1;
    cd.Format = format;
    cd.SampleDesc.Count = 1;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = format;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sv.Texture2DArray.MipLevels = 1;
    sv.Texture2DArray.ArraySize = 1;
    if (FAILED(device_->CreateTexture2D(&cd, nullptr, copy_.GetAddressOf())) ||
        FAILED(device_->CreateShaderResourceView(copy_.Get(), &sv, copySrv_.GetAddressOf()))) {
      copy_.Reset();
      copySrv_.Reset();
      SetStatus("cannot create a copy of the game picture");
      return nullptr;
    }
  }
  const UINT sub = D3D11CalcSubresource(0, eye.arrayIndex, d.MipLevels);
  if (d.SampleDesc.Count > 1)
    context_->ResolveSubresource(copy_.Get(), 0, eye.texture, sub, format);
  else
    context_->CopySubresourceRegion(copy_.Get(), 0, 0, 0, 0, eye.texture, sub, nullptr);
  return copySrv_.Get();
}

bool Mirror::Render(const Eye& eye, const std::vector<Quad>& quads, uint32_t width, uint32_t height, float zoom) {
  if (!eye.texture || eye.width <= 0 || eye.height <= 0) {
    SetStatus("no game picture");
    return false;
  }
  if (!InitPipeline()) return false;
  width = std::clamp(width, 320u, 3840u);
  height = std::clamp(height, 180u, 2160u);
  if (!EnsureTarget(width, height)) return false;
  if (mutex_->AcquireSync(0, 0) != S_OK) return false;  // the app is copying right now: next frame

  bool ok = false;
  if (ID3D11ShaderResourceView* src = SourceView(eye)) {
    D3D11_TEXTURE2D_DESC sd{};
    eye.texture->GetDesc(&sd);

    // View of the mirror: a symmetric frustum straight ahead of the eye, as wide as fits into
    // the (usually asymmetric) eye picture, narrowed by `zoom`.
    const float tl = std::tan(eye.fov.angleLeft), tr = std::tan(eye.fov.angleRight);
    const float td = std::tan(eye.fov.angleDown), tu = std::tan(eye.fov.angleUp);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float halfX = std::max(0.05f, std::min(std::min(-tl, tr), std::min(-td, tu) * aspect)) / std::max(zoom, 0.5f);
    const float halfY = halfX / aspect;

    Params p{};
    p.uvRect[0] = static_cast<float>(eye.x) / static_cast<float>(sd.Width);
    p.uvRect[1] = static_cast<float>(eye.y) / static_cast<float>(sd.Height);
    p.uvRect[2] = static_cast<float>(eye.x + eye.width) / static_cast<float>(sd.Width);
    p.uvRect[3] = static_cast<float>(eye.y + eye.height) / static_cast<float>(sd.Height);
    p.fov[0] = tl;
    p.fov[1] = tr;
    p.fov[2] = td;
    p.fov[3] = tu;
    p.view[0] = halfX;
    p.view[1] = halfY;
    p.view[2] = 1.0f;
    p.view[3] = 1.0f;

    auto upload = [&](const Params& params) {
      D3D11_MAPPED_SUBRESOURCE m{};
      if (FAILED(context_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
      std::memcpy(m.pData, &params, sizeof(params));
      context_->Unmap(cb_.Get(), 0);
      return true;
    };

    ComPtr<ID3DDeviceContextState> previous;
    context1_->SwapDeviceContextState(state_.Get(), previous.GetAddressOf());

    ID3D11RenderTargetView* rtv = rtv_.Get();
    ID3D11Buffer* cb = cb_.Get();
    ID3D11SamplerState* smp = sampler_.Get();
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->RSSetViewports(1, &vp);
    context_->RSSetState(raster_.Get());
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetConstantBuffers(0, 1, &cb);
    context_->PSSetConstantBuffers(0, 1, &cb);
    context_->PSSetSamplers(0, 1, &smp);

    // 1) the game's picture
    if (upload(p)) {
      context_->OMSetBlendState(opaque_.Get(), nullptr, 0xFFFFFFFF);
      context_->VSSetShader(vsFull_.Get(), nullptr, 0);
      context_->PSSetShader(psFull_.Get(), nullptr, 0);
      context_->PSSetShaderResources(1, 1, &src);
      context_->Draw(3, 0);
      ID3D11ShaderResourceView* none = nullptr;
      context_->PSSetShaderResources(1, 1, &none);
    }

    // 2) the overlay quads, in the order the compositor draws them
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(vsQuad_.Get(), nullptr, 0);
    context_->PSSetShader(psQuad_.Get(), nullptr, 0);
    const Pose toEye = eye.pose.Inverse();
    for (const Quad& q : quads) {
      if (!q.srv || q.width <= 0 || q.height <= 0) continue;
      const float hw = q.width * 0.5f, hh = q.height * 0.5f;
      const Vec3 local[4] = {{-hw, hh, 0}, {hw, hh, 0}, {-hw, -hh, 0}, {hw, -hh, 0}};
      Params qp = p;
      bool visible = false;
      for (int c = 0; c < 4; ++c) {
        const Vec3 e = toEye.Apply(q.pose.Apply(local[c]));
        const float w = -e.z;  // distance in front of the eye
        qp.corners[c][0] = e.x / halfX;
        qp.corners[c][1] = e.y / halfY;
        qp.corners[c][2] = w - kNear;  // < 0 (cut off) closer than kNear and behind the eye
        qp.corners[c][3] = w;
        visible |= w > kNear;
      }
      if (!visible) continue;
      for (int k = 0; k < 4; ++k) qp.uvRect[k] = q.uv[k];
      qp.view[2] = q.alpha;
      qp.view[3] = q.blend ? 0.0f : 1.0f;
      if (!upload(qp)) continue;
      context_->OMSetBlendState(q.blend || q.alpha < 0.999f ? blend_.Get() : opaque_.Get(), nullptr, 0xFFFFFFFF);
      ID3D11ShaderResourceView* srv = q.srv;
      context_->PSSetShaderResources(0, 1, &srv);
      context_->Draw(4, 0);
    }
    ID3D11ShaderResourceView* none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
    context_->OMSetRenderTargets(0, nullptr, nullptr);

    context1_->SwapDeviceContextState(previous.Get(), nullptr);

    context_->CopyResource(shared_.Get(), target_.Get());
    ok = true;
    SetStatus(std::format("running ({}x{})", width, height));
  }
  mutex_->ReleaseSync(0);
  return ok;
}
