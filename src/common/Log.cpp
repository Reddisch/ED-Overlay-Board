#include "Log.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace {
std::mutex g_mutex;
std::ofstream g_file;
bool g_console = false;
}  // namespace

void LogInit(const std::filesystem::path& file, bool console) {
  std::lock_guard lock(g_mutex);
  g_console = console;
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  g_file.open(file, std::ios::out | std::ios::trunc);
}

void LogLine(const std::string& line) {
  auto now = std::chrono::zoned_time{std::chrono::current_zone(),
                                     std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())};
  std::string full = std::format("[{:%H:%M:%S}] {}\n", now, line);

  std::lock_guard lock(g_mutex);
  if (g_console) {
    std::fputs(full.c_str(), stdout);
    std::fflush(stdout);
  }
  OutputDebugStringA(("[EDOverlayBoard] " + full).c_str());
  if (g_file.is_open()) {
    g_file << full;
    g_file.flush();
  }
}
