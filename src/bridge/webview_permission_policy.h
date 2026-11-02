#pragma once

#include "choc/gui/choc_WebView.h"

#if defined(_WIN32)
namespace effetune::vst::detail {
// The VST processor supplies all audio. No page in this WebView needs capture.
inline bool denyWebViewCapturePermission(const int kind) noexcept {
  if (kind == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE) {
    OutputDebugStringA("mIV Sound denied WebView2 microphone permission\n");
    return true;
  }
  if (kind == COREWEBVIEW2_PERMISSION_KIND_CAMERA) {
    OutputDebugStringA("mIV Sound denied WebView2 camera permission\n");
    return true;
  }
  return false;
}
} // namespace effetune::vst::detail
#endif
