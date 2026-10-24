#include "choc/gui/choc_WebView.h"
#include "webview_loader_fixture_api.h"
#include "webview_core_fixture.h"

#include <utility>

namespace {

int failureStage = 0;
std::wstring lastUserDataFolder;
ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *pending = nullptr;
ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *pendingController = nullptr;

WebViewControllerFixtureStats controllerStats;
FixtureWebView coreView;

class FixtureController final : public ICoreWebView2Controller {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override {
    if (object != nullptr) *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++controllerStats.references; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (controllerStats.closeCalls != 1) controllerStats.releasedBeforeClose = true;
    return --controllerStats.references;
  }
  HRESULT STDMETHODCALLTYPE Close() override {
    ++controllerStats.closeCalls;
    controllerStats.closeThread = GetCurrentThreadId();
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    controllerStats.closedOnOwnerSTA =
        SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
        (apartment == APTTYPE_STA || apartment == APTTYPE_MAINSTA) &&
        controllerStats.closeThread == controllerStats.ownerThread;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2 **view) override {
    ++controllerStats.coreViewCalls;
    *view = &coreView;
    coreView.AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE get_Bounds(RECT*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE put_Bounds(RECT) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE get_ZoomFactor(double*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE put_ZoomFactor(double) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(void*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT, double) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(void*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE add_GotFocus(void*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE add_LostFocus(void*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(void*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() override { return S_OK; }
};

FixtureController controller;

class FailingEnvironment final : public ICoreWebView2Environment {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override {
    if (object != nullptr) *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(
      HWND, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *handler) override {
    ++controllerStats.createCalls;
    if (failureStage == 2) return E_ACCESSDENIED;
    pendingController = handler;
    handler->AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(
      IStream *, int, LPCWSTR, LPCWSTR, ICoreWebView2WebResourceResponse **) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR *) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(
      void *, EventRegistrationToken *) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(
      EventRegistrationToken) override { return E_NOTIMPL; }
};

FailingEnvironment environment;

} // namespace

extern "C" __declspec(dllexport) void SetFailureStage(const int stage) {
  failureStage = stage;
  controllerStats = {};
}

extern "C" __declspec(dllexport) const WebViewControllerFixtureStats *GetControllerStats() {
  return &controllerStats;
}

extern "C" __declspec(dllexport) const wchar_t *GetLastUserDataFolder() {
  return lastUserDataFolder.c_str();
}

extern "C" __declspec(dllexport) HRESULT CompleteEnvironment() {
  auto *handler = std::exchange(pending, nullptr);
  if (handler == nullptr) return E_UNEXPECTED;
  const auto result = failureStage == 1
                          ? handler->Invoke(E_FAIL, nullptr)
                          : handler->Invoke(S_OK, &environment);
  handler->Release();
  return result;
}

extern "C" __declspec(dllexport) HRESULT CompleteController() {
  auto *handler = std::exchange(pendingController, nullptr);
  if (handler == nullptr) return E_UNEXPECTED;
  const auto result = handler->Invoke(failureStage == 3 ? E_FAIL : S_OK,
                                     failureStage == 5 ? &controller : nullptr);
  handler->Release();
  return result;
}

extern "C" __declspec(dllexport) HRESULT CompleteScript() {
  auto *handler = std::exchange(coreView.pendingScript, nullptr);
  if (handler == nullptr) return E_UNEXPECTED;
  const auto result = handler->Invoke(S_OK, L"true");
  handler->Release();
  return result;
}

extern "C" __declspec(dllexport) void ReleasePendingCallbacks() {
  if (auto *handler = std::exchange(pending, nullptr)) handler->Release();
  if (auto *handler = std::exchange(pendingController, nullptr)) handler->Release();
  if (auto *handler = std::exchange(coreView.pendingScript, nullptr)) handler->Release();
}

STDAPI CreateCoreWebView2EnvironmentWithOptions(
    PCWSTR, PCWSTR userDataFolder, void *,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler) {
  lastUserDataFolder = userDataFolder != nullptr ? userDataFolder : L"";
  controllerStats.ownerThread = GetCurrentThreadId();
  if (failureStage == 0) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  if (pending != nullptr) return E_UNEXPECTED;
  pending = handler;
  handler->AddRef();
  return S_OK;
}
