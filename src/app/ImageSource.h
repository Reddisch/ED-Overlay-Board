#pragma once

#include "Config.h"
#include "ContentSource.h"
#include "D3D.h"

#include <chrono>
#include <filesystem>
#include <vector>

// Kneeboard pages: shows one image at a time from a folder and/or an explicit file list.
// Trigger on the right half = next page, left half = previous page.
class ImageSource : public ContentSource {
 public:
  ImageSource(D3D& d3d, const WindowConfig& cfg, const std::filesystem::path& baseDir);

  ID3D11Texture2D* Update(bool& changed) override;
  void OnClick(float x, float y) override;
  void NextPage() override;
  void PrevPage() override;
  int Page() const override { return page_; }
  std::string Status() const override;

 private:
  void Rescan();
  void LoadCurrent();

  D3D& d3d_;
  std::wstring name_;
  std::filesystem::path folder_;
  std::vector<std::filesystem::path> explicit_;
  std::vector<std::filesystem::path> files_;
  int page_ = 0;
  bool dirty_ = true;
  winrt::com_ptr<ID3D11Texture2D> texture_;
  std::chrono::steady_clock::time_point nextScan_{};
};
