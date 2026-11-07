#pragma once
// User interface language (German / English). Texts are written inline as T("Deutsch", "English").

#include <windows.h>

#include <string>

namespace i18n {

inline bool g_english = false;

inline bool English() { return g_english; }
inline const char* T(const char* de, const char* en) { return g_english ? en : de; }
inline const wchar_t* T(const wchar_t* de, const wchar_t* en) { return g_english ? en : de; }

// "de", "en" or "" (= Windows display language: German -> "de", anything else -> "en")
inline void Apply(const std::string& language) {
  if (language == "de" || language == "en")
    g_english = language == "en";
  else
    g_english = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_GERMAN;
}

}  // namespace i18n

using i18n::T;
