cmake_minimum_required(VERSION 3.20)
project(EDOverlayBoard VERSION 0.3.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "EDOverlayBoard is Windows-only (D3D11, Windows.Graphics.Capture, DirectInput).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Third-party dependencies (header-only, downloaded once at configure time, pinned versions)
# ---------------------------------------------------------------------------
set(OPENXR_VERSION "release-1.1.62" CACHE STRING "OpenXR-SDK tag")
set(JSON_VERSION   "v3.11.3"        CACHE STRING "nlohmann/json tag")
set(DEPS_DIR "${CMAKE_BINARY_DIR}/_deps")

function(fetch_file url dest)
  if(NOT EXISTS "${dest}")
    message(STATUS "Downloading ${url}")
    foreach(attempt 1 2 3)  # GitHub occasionally answers with 5xx
      file(DOWNLOAD "${url}" "${dest}.part" STATUS st TLS_VERIFY ON)
      list(GET st 0 code)
      if(code EQUAL 0)
        break()
      endif()
      file(REMOVE "${dest}.part")
      message(STATUS "  attempt ${attempt} failed (${st})")
      execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 5)
    endforeach()
    if(NOT code EQUAL 0)
      message(FATAL_ERROR "Download failed: ${url} (${st})")
    endif()
    file(RENAME "${dest}.part" "${dest}")
  endif()
endfunction()

set(OXR_RAW "https://raw.githubusercontent.com/KhronosGroup/OpenXR-SDK/${OPENXR_VERSION}/include/openxr")
set(OXR_DIR "${DEPS_DIR}/openxr-${OPENXR_VERSION}/include")
foreach(h openxr.h openxr_platform.h openxr_platform_defines.h openxr_loader_negotiation.h)
  fetch_file("${OXR_RAW}/${h}" "${OXR_DIR}/openxr/${h}")
endforeach()

set(JSON_DIR "${DEPS_DIR}/json-${JSON_VERSION}")
fetch_file("https://raw.githubusercontent.com/nlohmann/json/${JSON_VERSION}/single_include/nlohmann/json.hpp"
           "${JSON_DIR}/include/nlohmann/json.hpp")

# Dear ImGui (MIT) - settings window and VR quick menu
set(IMGUI_VERSION "v1.91.9b" CACHE STRING "Dear ImGui tag")
set(IMGUI_RAW "https://raw.githubusercontent.com/ocornut/imgui/${IMGUI_VERSION}")
set(IMGUI_DIR "${DEPS_DIR}/imgui-${IMGUI_VERSION}")
foreach(f imgui.cpp imgui.h imgui_internal.h imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imconfig.h
          imstb_rectpack.h imstb_textedit.h imstb_truetype.h LICENSE.txt
          backends/imgui_impl_win32.cpp backends/imgui_impl_win32.h
          backends/imgui_impl_dx11.cpp backends/imgui_impl_dx11.h
          misc/cpp/imgui_stdlib.cpp misc/cpp/imgui_stdlib.h)
  fetch_file("${IMGUI_RAW}/${f}" "${IMGUI_DIR}/${f}")
endforeach()
add_library(imgui STATIC
  "${IMGUI_DIR}/imgui.cpp" "${IMGUI_DIR}/imgui_draw.cpp" "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp" "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp" "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp")
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends" "${IMGUI_DIR}/misc/cpp")
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

set(COMMON_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN EDOB_VERSION="${PROJECT_VERSION}"
  EDOB_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} EDOB_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  EDOB_VERSION_PATCH=${PROJECT_VERSION_PATCH})
set(COMMON_OPTS /W4 /utf-8 /permissive- /bigobj /EHsc)

# ---------------------------------------------------------------------------
# Companion app: window capture, kneeboard images, hotkeys, joystick, config
# ---------------------------------------------------------------------------
add_executable(EDOverlayBoard
  src/app/main.cpp
  src/app/App.cpp
  src/app/Config.cpp
  src/app/D3D.cpp
  src/app/Gui.cpp
  src/app/HotkeyManager.cpp
  src/app/ImageSource.cpp
  src/app/JoystickManager.cpp
  src/app/LayerRegistration.cpp
  src/app/WindowCaptureSource.cpp
  src/app/ClickSound.cpp
  src/app/app.rc
  src/common/Log.cpp
)
# tray app: no console window (command line options attach to the calling console)
set_target_properties(EDOverlayBoard PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_options(EDOverlayBoard PRIVATE /ENTRY:wmainCRTStartup)
target_include_directories(EDOverlayBoard PRIVATE src/app src/common "${JSON_DIR}/include")
target_compile_definitions(EDOverlayBoard PRIVATE ${COMMON_DEFS} DIRECTINPUT_VERSION=0x0800)
target_compile_options(EDOverlayBoard PRIVATE ${COMMON_OPTS})
target_link_libraries(EDOverlayBoard PRIVATE imgui
  d3d11 dxgi d2d1 dwrite dwmapi dinput8 dxguid windowscodecs windowsapp advapi32 shell32 shcore d3dcompiler winmm)
install(FILES "${IMGUI_DIR}/LICENSE.txt" DESTINATION licenses RENAME imgui-LICENSE.txt)

# ---------------------------------------------------------------------------
# OpenXR API layer (loaded into the game process by the OpenXR loader)
# ---------------------------------------------------------------------------
add_library(EDOverlayBoard_Layer SHARED
  src/layer/Layer.cpp
  src/layer/Input.cpp
  src/layer/OverlaySession.cpp
  src/common/Log.cpp
)
target_include_directories(EDOverlayBoard_Layer PRIVATE src/layer src/common "${OXR_DIR}")
target_compile_definitions(EDOverlayBoard_Layer PRIVATE ${COMMON_DEFS}
  XR_NO_PROTOTYPES XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
target_compile_options(EDOverlayBoard_Layer PRIVATE ${COMMON_OPTS})
target_link_libraries(EDOverlayBoard_Layer PRIVATE d3d11 dxgi)

# Put the app, the layer DLL, its manifest and the resources into one folder
set_target_properties(EDOverlayBoard EDOverlayBoard_Layer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
add_custom_command(TARGET EDOverlayBoard POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/resources/EDOverlayBoard_Layer.json" "$<TARGET_FILE_DIR:EDOverlayBoard>"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:EDOverlayBoard>/resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/resources/config.example.json" "$<TARGET_FILE_DIR:EDOverlayBoard>/resources"
  VERBATIM)
add_dependencies(EDOverlayBoard EDOverlayBoard_Layer)

install(TARGETS EDOverlayBoard EDOverlayBoard_Layer RUNTIME DESTINATION .)
install(FILES resources/EDOverlayBoard_Layer.json README.md LICENSE DESTINATION .)
install(FILES resources/config.example.json DESTINATION resources)
