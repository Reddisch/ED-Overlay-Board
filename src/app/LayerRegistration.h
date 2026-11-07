#pragma once

#include <filesystem>

// Registers the OpenXR API layer as an implicit layer for the current user
// (HKCU\Software\Khronos\OpenXR\1\ApiLayers\Implicit, no admin rights needed).
namespace LayerRegistration {
std::filesystem::path ManifestPath();  // EDOverlayBoard_Layer.json next to the exe
bool IsRegistered();
bool Register();    // also removes stale entries from other install locations
bool Unregister();
}  // namespace LayerRegistration
