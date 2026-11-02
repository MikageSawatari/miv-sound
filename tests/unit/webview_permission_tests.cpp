#include "bridge/webview_permission_policy.h"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

// A COM argument double only. No WebView, controller, loader or window exists.
class PermissionArgs final : public ICoreWebView2PermissionRequestedEventArgs {
public:
  COREWEBVIEW2_PERMISSION_KIND kind = COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
  COREWEBVIEW2_PERMISSION_STATE state = COREWEBVIEW2_PERMISSION_STATE_DEFAULT;
  HRESULT readResult = S_OK, writeResult = S_OK;
  int writes = 0;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override {
    if (object) *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR *) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_PermissionKind(COREWEBVIEW2_PERMISSION_KIND *value) override {
    *value = kind;
    return readResult;
  }
  HRESULT STDMETHODCALLTYPE get_IsUserInitiated(BOOL *) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_State(COREWEBVIEW2_PERMISSION_STATE *value) override { *value = state; return S_OK; }
  HRESULT STDMETHODCALLTYPE put_State(COREWEBVIEW2_PERMISSION_STATE value) override {
    state = value;
    ++writes;
    return writeResult;
  }
  HRESULT STDMETHODCALLTYPE GetDeferral(ICoreWebView2Deferral **) override { return E_NOTIMPL; }
};
} // namespace

int main() {
  try {
    const auto policy = effetune::vst::detail::denyWebViewCapturePermission;
    for (const auto kind : {COREWEBVIEW2_PERMISSION_KIND_MICROPHONE, COREWEBVIEW2_PERMISSION_KIND_CAMERA}) {
      PermissionArgs args;
      args.kind = kind;
      expect(choc::ui::mivSoundHandlePermissionRequest(&args, policy) == S_OK &&
                 args.state == COREWEBVIEW2_PERMISSION_STATE_DENY && args.writes == 1,
             "capture permission is synchronously denied without using the default prompt");
    }
    for (const auto kind : {COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION,
                           COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION,
                           COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS,
                           COREWEBVIEW2_PERMISSION_KIND_OTHER_SENSORS}) {
      PermissionArgs args;
      args.kind = kind;
      expect(choc::ui::mivSoundHandlePermissionRequest(&args, policy) == S_OK && args.writes == 0,
             "other permissions retain their current behavior");
    }
    PermissionArgs clipboard;
    clipboard.kind = COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ;
    expect(choc::ui::mivSoundHandlePermissionRequest(&clipboard, policy) == S_OK &&
               clipboard.state == COREWEBVIEW2_PERMISSION_STATE_ALLOW, "retain CHOC clipboard-read behavior");
    clipboard.writeResult = E_FAIL;
    expect(choc::ui::mivSoundHandlePermissionRequest(&clipboard, policy) == S_OK,
           "retain CHOC's existing return value even when clipboard allowance fails");
    PermissionArgs unreadable;
    unreadable.readResult = E_FAIL;
    expect(choc::ui::mivSoundHandlePermissionRequest(&unreadable, policy) == S_OK && unreadable.writes == 0,
           "unreadable request preserves the existing behavior");
    PermissionArgs failedWrite;
    failedWrite.kind = COREWEBVIEW2_PERMISSION_KIND_MICROPHONE;
    failedWrite.writeResult = E_FAIL;
    expect(choc::ui::mivSoundHandlePermissionRequest(&failedWrite, policy) == E_FAIL,
           "denial failure is returned to WebView2");
    expect(choc::ui::mivSoundHandlePermissionRequest(nullptr, policy) == S_OK, "null argument is harmless");
    std::cout << "11 WebView permission cases passed without creating WebView2\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
