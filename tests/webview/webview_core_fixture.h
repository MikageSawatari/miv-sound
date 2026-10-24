// Minimal in-process COM double; never launches the WebView2 runtime.
#pragma once

class FixtureWebView final : public ICoreWebView2 {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override {
    if (object != nullptr) *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings **settings) override {
    *settings = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE get_Source(LPWSTR*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE NavigateToString(LPCWSTR) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_NavigationStarting(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_NavigationStarting(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_ContentLoading(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_ContentLoading(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_SourceChanged(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_SourceChanged(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_HistoryChanged(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_HistoryChanged(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_NavigationCompleted(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_NavigationCompleted(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_FrameNavigationStarting(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_FrameNavigationCompleted(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_FrameNavigationCompleted(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_ScriptDialogOpening(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_ScriptDialogOpening(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_PermissionRequested(ICoreWebView2PermissionRequestedEventHandler*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_PermissionRequested(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_ProcessFailed(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_ProcessFailed(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(LPCWSTR, void*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE RemoveScriptToExecuteOnDocumentCreated(LPCWSTR) override { return E_NOTIMPL; }
  ICoreWebView2ExecuteScriptCompletedHandler *pendingScript = nullptr;
  HRESULT STDMETHODCALLTYPE ExecuteScript(LPCWSTR, void *handler) override {
    if (pendingScript != nullptr) return E_UNEXPECTED;
    pendingScript = static_cast<ICoreWebView2ExecuteScriptCompletedHandler *>(handler);
    if (pendingScript != nullptr) pendingScript->AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT, void*, void*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Reload() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE PostWebMessageAsJson(LPCWSTR) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE PostWebMessageAsString(LPCWSTR) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_WebMessageReceived(ICoreWebView2WebMessageReceivedEventHandler*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_WebMessageReceived(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CallDevToolsProtocolMethod(LPCWSTR, LPCWSTR, void*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_BrowserProcessId(UINT32*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_CanGoBack(BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_CanGoForward(BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GoBack() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GoForward() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetDevToolsProtocolEventReceiver(LPCWSTR, void**) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Stop() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_NewWindowRequested(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_NewWindowRequested(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_DocumentTitleChanged(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_DocumentTitleChanged(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_DocumentTitle(LPWSTR*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE AddHostObjectToScript(LPCWSTR, VARIANT*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE RemoveHostObjectFromScript(LPCWSTR) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE OpenDevToolsWindow() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_ContainsFullScreenElementChanged(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_ContainsFullScreenElementChanged(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE get_ContainsFullScreenElement(BOOL*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_WebResourceRequested(ICoreWebView2WebResourceRequestedEventHandler*, EventRegistrationToken*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE remove_WebResourceRequested(EventRegistrationToken) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE AddWebResourceRequestedFilter(const LPCWSTR, const COREWEBVIEW2_WEB_RESOURCE_CONTEXT) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE RemoveWebResourceRequestedFilter(const LPCWSTR, const COREWEBVIEW2_WEB_RESOURCE_CONTEXT) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE add_WindowCloseRequested(void*, EventRegistrationToken*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE remove_WindowCloseRequested(EventRegistrationToken) override { return E_NOTIMPL; }
};
