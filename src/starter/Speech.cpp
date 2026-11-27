#include "Speech.h"

#include "Log.h"
#include "Util.h"

#include <sapiddk.h>

using Microsoft::WRL::ComPtr;

bool Speech::Init() {
  if (voice_) return true;
  if (tried_) return false;
  tried_ = true;
  HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice_));
  if (FAILED(hr)) {
    Log("Speech: no SAPI voice (0x{:08X})", static_cast<unsigned>(hr));
    voice_.Reset();
    return false;
  }
  return true;
}

void Speech::SetEnabled(bool on) {
  enabled_ = on;
  if (!on && voice_) voice_->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);  // stop talking right away
}

void Speech::SetLanguage(bool english) {
  const int lang = english ? 1 : 0;
  if (lang == language_ || !Init()) return;
  language_ = lang;
  ComPtr<ISpObjectTokenCategory> cat;
  ComPtr<IEnumSpObjectTokens> tokens;
  ComPtr<ISpObjectToken> token;
  // SAPI language attribute: hex LANGID (409 = English US, 407 = German)
  const wchar_t* want = english ? L"Language=409" : L"Language=407";
  if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&cat))) &&
      SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE)) && SUCCEEDED(cat->EnumTokens(want, nullptr, &tokens)) &&
      tokens->Next(1, &token, nullptr) == S_OK) {
    voice_->SetVoice(token.Get());
  } else {
    Log("Speech: no {} voice installed, using the default voice", english ? "English" : "German");
  }
}

void Speech::Say(const std::string& utf8) {
  if (!enabled_ || utf8.empty() || !Init()) return;
  std::wstring text = Utf8ToWide(utf8);
  voice_->Speak(text.c_str(), SPF_ASYNC | SPF_IS_NOT_XML, nullptr);
}
