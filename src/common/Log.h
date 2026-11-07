#pragma once

#include <filesystem>
#include <format>
#include <string>

// console = also print to stdout (the companion app); the layer DLL only writes its file.
void LogInit(const std::filesystem::path& file, bool console);
void LogLine(const std::string& line);

template <class... Args>
void Log(std::format_string<Args...> fmt, Args&&... args) {
  LogLine(std::format(fmt, std::forward<Args>(args)...));
}
