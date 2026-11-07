#include "ImageSource.h"

#include "Log.h"
#include "Util.h"

#include <wincodec.h>

#include <algorithm>

using namespace std::chrono_literals;

namespace {

constexpr UINT kMaxImageDim = 4096;

winrt::com_ptr<IWICImagingFactory> Wic() {
  static winrt::com_ptr<IWICImagingFactory> factory;
  if (!factory)
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                     factory.put_void());
  return factory;
}

bool IsImageFile(const std::filesystem::path& p) {
  static const wchar_t* exts[] = {L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff", L".webp"};
  auto ext = ToLower(p.extension().wstring());
  return std::any_of(std::begin(exts), std::end(exts), [&](const wchar_t* e) { return ext == e; });
}

}  // namespace

ImageSource::ImageSource(D3D& d3d, const WindowConfig& cfg, const std::filesystem::path& baseDir)
    : d3d_(d3d), name_(Utf8ToWide(cfg.name)), page_(cfg.page) {
  if (!cfg.folder.empty()) {
    folder_ = baseDir / std::filesystem::path(Utf8ToWide(cfg.folder));
    std::error_code ec;
    std::filesystem::create_directories(folder_, ec);
  }
  for (const auto& img : cfg.images) explicit_.push_back(baseDir / std::filesystem::path(Utf8ToWide(img)));
}

void ImageSource::Rescan() {
  std::vector<std::filesystem::path> found = explicit_;
  std::error_code ec;
  if (!folder_.empty() && std::filesystem::is_directory(folder_, ec)) {
    std::vector<std::filesystem::path> inFolder;
    for (const auto& e : std::filesystem::directory_iterator(folder_, ec))
      if (e.is_regular_file(ec) && IsImageFile(e.path())) inFolder.push_back(e.path());
    std::sort(inFolder.begin(), inFolder.end(), [](const auto& a, const auto& b) {
      return ToLower(a.filename().wstring()) < ToLower(b.filename().wstring());
    });
    found.insert(found.end(), inFolder.begin(), inFolder.end());
  }
  if (found != files_) {
    files_ = std::move(found);
    page_ = files_.empty() ? 0 : std::clamp(page_, 0, static_cast<int>(files_.size()) - 1);
    dirty_ = true;
  }
}

void ImageSource::LoadCurrent() {
  dirty_ = false;
  if (files_.empty()) {
    texture_ = d3d_.CreateTextTexture(
        name_, L"No images found.\n\nPut PNG/JPG files into:\n" + folder_.wstring() +
                   L"\n\nTrigger right/left half = next/previous page.");
    return;
  }

  const auto& path = files_[static_cast<size_t>(page_)];
  auto wic = Wic();
  winrt::com_ptr<IWICBitmapDecoder> decoder;
  winrt::com_ptr<IWICBitmapFrameDecode> frame;
  winrt::com_ptr<IWICFormatConverter> converter;
  UINT w = 0, h = 0;
  bool ok = wic &&
            SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, decoder.put())) &&
            SUCCEEDED(decoder->GetFrame(0, frame.put())) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0;

  winrt::com_ptr<IWICBitmapSource> src;
  if (ok) {
    src = frame.as<IWICBitmapSource>();
    if (std::max(w, h) > kMaxImageDim) {
      float s = static_cast<float>(kMaxImageDim) / static_cast<float>(std::max(w, h));
      UINT nw = std::max(1u, static_cast<UINT>(w * s)), nh = std::max(1u, static_cast<UINT>(h * s));
      winrt::com_ptr<IWICBitmapScaler> scaler;
      if (SUCCEEDED(wic->CreateBitmapScaler(scaler.put())) &&
          SUCCEEDED(scaler->Initialize(src.get(), nw, nh, WICBitmapInterpolationModeFant))) {
        src = scaler.as<IWICBitmapSource>();
        w = nw;
        h = nh;
      }
    }
    ok = SUCCEEDED(wic->CreateFormatConverter(converter.put())) &&
         SUCCEEDED(converter->Initialize(src.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                                         0.0, WICBitmapPaletteTypeCustom));
  }

  std::vector<uint32_t> pixels;
  if (ok) {
    pixels.resize(static_cast<size_t>(w) * h);
    ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size() * 4),
                                         reinterpret_cast<BYTE*>(pixels.data())));
  }

  if (ok) texture_ = d3d_.CreateTexture(w, h, pixels.data());
  if (!ok || !texture_) {
    Log("Images '{}': cannot load {}", WideToUtf8(name_), path.string());
    texture_ = d3d_.CreateTextTexture(name_, L"Cannot load image:\n" + path.filename().wstring());
  } else {
    Log("Images '{}': page {}/{} ({})", WideToUtf8(name_), page_ + 1, files_.size(), path.filename().string());
  }
}

ID3D11Texture2D* ImageSource::Update(bool& changed) {
  changed = false;
  auto now = std::chrono::steady_clock::now();
  if (now >= nextScan_) {
    nextScan_ = now + 3s;
    Rescan();
  }
  if (dirty_) {
    LoadCurrent();
    changed = true;
  }
  return texture_.get();
}

std::string ImageSource::Status() const {
  if (files_.empty()) return "keine Bilder in " + folder_.string();
  return std::format("Seite {}/{}: {}", page_ + 1, files_.size(), files_[static_cast<size_t>(page_)].filename().string());
}

void ImageSource::OnClick(float x, float /*y*/) {
  if (x >= 0.0f)
    NextPage();
  else
    PrevPage();
}

void ImageSource::NextPage() {
  if (files_.size() < 2) return;
  page_ = (page_ + 1) % static_cast<int>(files_.size());
  dirty_ = true;
}

void ImageSource::PrevPage() {
  if (files_.size() < 2) return;
  page_ = (page_ + static_cast<int>(files_.size()) - 1) % static_cast<int>(files_.size());
  dirty_ = true;
}
