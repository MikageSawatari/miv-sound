#define CHOC_FIND_WEBVIEW2LOADER_DLL \
  choc::file::DynamicLibrary(EFFETUNE_WEBVIEW_LOADER_FIXTURE)
#include "choc/gui/choc_WebView.h"
#include "bridge/data_directory.h"
#include "webview_loader_fixture_api.h"

#include <iostream>
#include <stdexcept>
#include <string>

#include "../support/webview_test_lifetime.h"

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

class ScopedSTA {
public:
  ScopedSTA() {
    expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
           "initialize the WebView owner STA");
  }
  ~ScopedSTA() { CoUninitialize(); }
};

struct QueuedControllerCompletion {
  HRESULT (*complete)() = nullptr;
  bool delivered = false;
  HRESULT result = S_OK;
};

LRESULT CALLBACK completionWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_APP + 1) {
    auto &completion = *reinterpret_cast<QueuedControllerCompletion *>(lParam);
    completion.result = completion.complete();
    completion.delivered = true;
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int main() {
  effetune::vst::testing::suppressCrtModalDialogs();
  try {
    ScopedSTA apartment;
    choc::file::DynamicLibrary loader(EFFETUNE_WEBVIEW_LOADER_FIXTURE);
    const auto setStage = reinterpret_cast<void (*)(int)>(loader.findFunction("SetFailureStage"));
    const auto complete = reinterpret_cast<HRESULT (*)()>(loader.findFunction("CompleteEnvironment"));
    const auto completeController = reinterpret_cast<HRESULT (*)()>(loader.findFunction("CompleteController"));
    const auto lastFolder = reinterpret_cast<const wchar_t *(*)()>(loader.findFunction("GetLastUserDataFolder"));
    const auto stats = reinterpret_cast<const WebViewControllerFixtureStats *(*)()>(
        loader.findFunction("GetControllerStats"));
    expect(setStage != nullptr && complete != nullptr && completeController != nullptr && stats != nullptr,
           "load the WebView2 API fixture");
    const auto directory = effetune::vst::soundDataDirectory();
    expect(!directory.empty() && lastFolder != nullptr, "isolated WebView test storage is configured");
    const auto userDataFolder = (directory / "webview").wstring();

    for (int stage = 0; stage < 5; ++stage) {
      setStage(stage);
      int errors = 0;
      int ready = 0;
      choc::ui::WebView::Options options;
      options.userDataFolder = userDataFolder;
      options.webviewInitialisationError = [&](const std::string &error) {
        expect(!error.empty(), "preserve the native creation diagnostic");
        ++errors;
      };
      options.webviewIsReady = [&](choc::ui::WebView &) { ++ready; };
      choc::ui::WebView view(options);
      expect(lastFolder() == userDataFolder,
             "explicit userDataFolder overrides the host APPDATA default at the WebView2 API");
      if (stage == 0) {
        expect(!view.loadedOK() && errors == 1, "report a synchronous loader failure");
      } else {
        expect(view.loadedOK() && !view.isReady() && errors == 0,
               "keep a pending creation distinct from a failure");
        (void)complete();
        if (stage >= 3) {
          expect(errors == 0 && !view.isReady(), "wait for controller completion");
          (void)completeController();
        }
        expect(errors == 1, "report environment or controller failure exactly once");
      }
      expect(!view.isReady() && ready == 0, "a failed creation must never report ready");
    }

    setStage(1);
    int retiredErrors = 0;
    {
      choc::ui::WebView::Options options;
      options.userDataFolder = userDataFolder;
      options.webviewInitialisationError = [&](const std::string &) { ++retiredErrors; };
      choc::ui::WebView retired(options);
      expect(retired.loadedOK(), "start a creation that outlives its WebView");
    }
    (void)complete();
    expect(retiredErrors == 0, "ignore completion after the WebView is destroyed");
    setStage(3);
    {
      choc::ui::WebView::Options options;
      options.userDataFolder = userDataFolder;
      options.webviewInitialisationError = [&](const std::string &) { ++retiredErrors; };
      choc::ui::WebView retired(options);
      (void)complete();
    }
    (void)completeController();
    expect(retiredErrors == 0, "ignore controller completion after destruction");

    const auto expectClosedOnce = [&](int coreViewCalls) {
      const auto &snapshot = *stats();
      expect(snapshot.closeCalls == 1, "Close is called exactly once for the controller");
      expect(snapshot.references == 1 && !snapshot.releasedBeforeClose,
             "Close precedes release of the owned controller reference");
      expect(snapshot.closedOnOwnerSTA && snapshot.closeThread == GetCurrentThreadId(),
             "Close executes on the creating owner STA");
      expect(snapshot.coreViewCalls == coreViewCalls,
             "a retired callback must not initialize the deleted WebView");
      expect(completeController() == E_UNEXPECTED && stats()->closeCalls == 1,
             "completion is consumed once and cannot close the controller twice");
    };

    // A real controller COM double reaches the ready path without starting any
    // browser process. Its reference must be closed before Pimpl releases it.
    setStage(5);
    int ready = 0;
    int errors = 0;
    choc::ui::WebView::Options options;
    options.userDataFolder = userDataFolder;
    options.webviewIsReady = [&](choc::ui::WebView &) { ++ready; };
    options.webviewInitialisationError = [&](const std::string &) { ++errors; };
    {
      choc::ui::WebView view(options);
      expect(complete() == S_OK && completeController() == S_OK && view.isReady(),
             "complete a working controller on its owner STA");
      expect(ready == 1 && errors == 0 && stats()->closeCalls == 0 && stats()->references == 2,
             "the live view owns an open controller");
    }
    expectClosedOnce(1);

    // Destruction during environment creation has no controller to close. A
    // later environment must not start controller creation for the dead owner.
    setStage(5);
    {
      choc::ui::WebView retired(options);
      expect(retired.loadedOK() && !retired.isReady(), "environment creation is pending");
    }
    expect(complete() == E_FAIL && stats()->createCalls == 0 && stats()->closeCalls == 0,
           "late environment completion creates no controller after destruction");

    // The controller may arrive after Pimpl has already gone away. The callback
    // still runs on the creating STA, and must close it without using Pimpl.
    setStage(5);
    {
      choc::ui::WebView retired(options);
      expect(complete() == S_OK && stats()->createCalls == 1 && !retired.isReady(),
             "controller creation is pending at destruction");
    }
    expect(stats()->closeCalls == 0 && completeController() == E_FAIL,
           "late non-null controller completion is rejected and closed");
    expectClosedOnce(0);
    expect(ready == 1 && errors == 0, "retired callbacks never notify the destroyed owner");

    // Simulate asynchronous delivery through the actual owner-STA message pump.
    // Keep the fixture DLL and callback state alive through this drain.
    setStage(5);
    {
      choc::ui::WebView retired(options);
      expect(complete() == S_OK, "start a controller whose completion will be queued");
    }
    QueuedControllerCompletion queued{completeController};
    WNDCLASSW windowClass{};
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpfnWndProc = completionWindowProc;
    windowClass.lpszClassName = L"MivSoundQueuedControllerCompletion";
    expect(RegisterClassW(&windowClass) != 0, "register the queued completion test window");
    const auto window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    expect(window != nullptr, "create an owner-STA completion queue");
    const effetune::vst::testing::TestCleanup destroyWindow([&] {
      DestroyWindow(window);
      UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    });
    expect(PostMessageW(window, WM_APP + 1, 0, reinterpret_cast<LPARAM>(&queued)) != FALSE,
           "queue completion after the view has been destroyed");
    std::chrono::steady_clock::time_point now{};
    effetune::vst::testing::WebViewExitWaitHooks hooks;
    hooks.now = [&] { return now; };
    hooks.pause = [&] { now += std::chrono::milliseconds(100); };
    hooks.scan = [](const auto &) { return std::vector<DWORD>{}; }; // fixture has no browser
    expect(effetune::vst::testing::waitForWebViewsExit({directory}, std::chrono::seconds(2), hooks) &&
               queued.delivered && queued.result == E_FAIL,
           "drain dispatches the late callback while its DLL and owner state are still alive");
    expectClosedOnce(0);

    setStage(5);
    bool unwound = false;
    try {
      choc::ui::WebView view(options);
      expect(complete() == S_OK && completeController() == S_OK && view.isReady(),
             "prepare exception-path destruction");
      throw std::runtime_error("synthetic test failure");
    } catch (const std::runtime_error &error) {
      expect(std::string(error.what()) == "synthetic test failure", "only catch the injected failure");
      unwound = true;
    }
    expect(unwound, "exercise exception-path destruction");
    expectClosedOnce(1);
    std::cout << "WebView2 initialization, owner-STA Close-before-release, and late callbacks verified\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
