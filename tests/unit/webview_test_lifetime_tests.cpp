#include "../support/webview_test_lifetime.h"
#include "../support/scoped_test_environment.h"

#include <cstring>
#include <fstream>

using namespace effetune::vst::testing;

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void testModuleCallbackObserver(const char *path) {
  const auto module = LoadLibraryA(path);
  expect(module != nullptr, "load the plugin to verify its read-only counter without creating any WebView");
  TestCleanup unload([&] { FreeLibrary(module); });
  const auto observer = moduleWebViewCallbackCounter(module);
  expect(observer() == 0, "the actual plugin DLL exports a working zero-valued callback observer");
  const auto previous = localWebViewCallbackCounter;
  TestCleanup restore([&] { localWebViewCallbackCounter = previous; });
  localWebViewCallbackCounter = [] { return std::uint64_t{3}; };
  expect(observer() == 3, "DLL observation includes callbacks from the EXE image as well");
}

void testPauseReleaseBeforeJoin() {
  std::atomic_bool paused{true}, entered{false}, finished{false};
  int releases = 0;
  bool injected = false;
  try {
    TestThread worker([&] {
      entered.store(true);
      while (paused.load()) std::this_thread::yield();
      finished.store(true);
    }, [&] { ++releases; paused.store(false); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!entered.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    expect(entered.load(), "reach the worker pause before injecting an assertion failure");
    throw std::runtime_error("injected paused-worker assertion");
  } catch (const std::runtime_error &error) {
    expect(std::string(error.what()) == "injected paused-worker assertion", "only catch the injected assertion");
    injected = true;
  }
  expect(injected && finished.load() && releases == 1,
         "unwind releases the pause exactly once BEFORE jthread destruction joins");
  paused.store(true);
  finished.store(false);
  {
    TestThread first([&] { while (paused.load()) std::this_thread::yield(); finished.store(true); },
                     [&] { ++releases; paused.store(false); });
    TestThread moved(std::move(first));
    moved.join();
  }
  expect(finished.load() && releases == 2, "moving and explicit join preserve one pause release");
}

void testFailedWaitStillDrainsEveryProfile() {
  std::chrono::steady_clock::time_point now{};
  const std::filesystem::path a = LR"(G:\profile-a)", b = LR"(G:\profile-b)";
  unsigned callsA = 0, callsB = 0;
  std::string order;
  WebViewExitWaitHooks hooks;
  hooks.now = [&] { return now; };
  hooks.pause = [&] { now += std::chrono::milliseconds(100); };
  hooks.pump = [] {};
  hooks.scan = [&](const auto &path) -> std::vector<DWORD> {
    if (path == a / "webview/EBWebView") {
      order += 'A';
      if (++callsA <= 2) throw std::runtime_error("injected unreadable process");
      return callsA < 5 ? std::vector<DWORD>{123} : std::vector<DWORD>{};
    }
    expect(path == b / "webview/EBWebView", "scan only the registered profiles");
    order += 'B';
    return ++callsB < 3 ? std::vector<DWORD>{456} : std::vector<DWORD>{};
  };
  expect(!waitForWebViewsExit({a, b}, std::chrono::milliseconds(100), hooks),
         "query error and elapsed deadline remain test failures after eventual clean exit");
  expect(callsA == callsB && callsA == 5 && now == std::chrono::milliseconds(400) +
             std::chrono::steady_clock::time_point{},
         "wait continues beyond its deadline until BOTH profiles have exited, without a quiet-time guess");
  expect(order.find("AA") == std::string::npos && order.find("BB") == std::string::npos,
         "a query exception never skips later folders in the round");
}

void testLongQueryCannotBypassDeadline() {
  std::chrono::steady_clock::time_point now{};
  unsigned scans = 0, pauses = 0;
  WebViewExitWaitHooks hooks;
  hooks.now = [&] { return now; };
  hooks.pump = [] {};
  hooks.pause = [&] { ++pauses; };
  hooks.scan = [&](const auto &) -> std::vector<DWORD> {
    ++scans;
    now += std::chrono::seconds(121);
    return {};
  };
  expect(!waitForWebViewsExit({LR"(G:\slow-query)"}, std::chrono::seconds(120), hooks) &&
             scans == 1 && pauses == 0,
         "an empty result after a slow query still records the exceeded deadline before returning");
}

void testCallbackCompletionIsIndependentOfProcessVisibility() {
  std::chrono::steady_clock::time_point now{};
  bool complete = false;
  unsigned scans = 0;
  WebViewExitWaitHooks hooks;
  hooks.now = [&] { return now; };
  hooks.pause = [&] { now += std::chrono::milliseconds(100); };
  hooks.pump = [&] {
    if (now >= std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(600)) complete = true;
  };
  hooks.scan = [&](const auto &) { ++scans; return std::vector<DWORD>{}; };
  hooks.pendingCallbacks = [&] { return complete ? 0u : 1u; };
  expect(waitForWebViewsExit({LR"(G:\callback-result)"}, std::chrono::seconds(2), hooks) &&
             now == std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(600) && scans == 7,
         "pending completion prevents exit even after 500ms without any browser process");
  complete = false;
  hooks.pump = [&] {
    if (now >= std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(900)) complete = true;
  };
  expect(!waitForWebViewsExit({LR"(G:\callback-result)"}, std::chrono::milliseconds(200), hooks) && complete,
         "a callback exceeding the deadline records failure but is still awaited");

  scans = 0;
  complete = false;
  hooks.pump = [] {};
  hooks.scan = [&](const auto &) {
    ++scans;
    if (scans == 1) complete = true; // callback starts a browser AFTER this empty scan
    return scans == 2 ? std::vector<DWORD>{123} : std::vector<DWORD>{};
  };
  expect(waitForWebViewsExit({LR"(G:\scan-race)"}, std::chrono::seconds(2), hooks) && scans == 3,
         "pending before a scan forces another round even if the callback completed during that scan");

  bool missingExportRejected = false;
  try { (void)moduleWebViewCallbackCounter(GetModuleHandleW(L"kernel32.dll"))(); }
  catch (const std::runtime_error &) { missingExportRejected = true; }
  expect(missingExportRejected, "a DLL lacking the required observer is never silently treated as drained");
}

void testWorkerUnwindPumpsOwnerSTA() {
  struct State { DWORD owner = GetCurrentThreadId(); bool delivered = false; bool ownerThread = false; } state;
  WNDCLASSW windowClass{};
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"mIVSoundWorkerJoinPumpTest";
  windowClass.lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
    if (message == WM_APP + 1) {
      auto &state = *reinterpret_cast<State *>(lParam);
      state.delivered = true;
      state.ownerThread = state.owner == GetCurrentThreadId();
      return 1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  };
  expect(RegisterClassW(&windowClass) != 0, "register the join-pump window");
  TestCleanup unregister([&] { UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance); });
  const auto window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
  expect(window != nullptr, "create the join-pump window");
  TestCleanup destroy([&] { DestroyWindow(window); });
  for (int mode = 0; mode < 3; ++mode) {
    state.delivered = state.ownerThread = false;
    std::atomic_bool completed{false};
    bool injected = false;
    try {
      TestThread worker([&] {
        DWORD_PTR result = 0;
        completed.store(SendMessageTimeoutW(window, WM_APP + 1, 0, reinterpret_cast<LPARAM>(&state),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &result) != 0 && result == 1);
      });
      if (mode == 0) worker.joinPumping();
      else if (mode == 1) throw std::runtime_error("injected owner unwind");
      else worker = TestThread([] {});
    } catch (const std::runtime_error &error) {
      expect(std::string(error.what()) == "injected owner unwind", "only catch the owner assertion");
      injected = true;
    }
    expect(injected == (mode == 1) && state.delivered && state.ownerThread && completed.load(),
           "explicit join, exception destruction and move assignment all pump the owner STA before join");
  }
}

void testDrainKeepsModuleAndOwnerStateAlive() {
  std::chrono::steady_clock::time_point now{};
  bool moduleLoaded = true, viewAlive = true, callbackPending = false, callbackDelivered = false;
  bool ordered = true, drained = false;
  std::vector<std::string> events;
  WebViewExitWaitHooks hooks;
  hooks.now = [&] { return now; };
  hooks.pause = [&] { now += std::chrono::milliseconds(100); };
  hooks.pump = [&] {
    ordered = ordered && moduleLoaded && !viewAlive;
    if (callbackPending) {
      callbackPending = false;
      callbackDelivered = true;
      events.push_back("late controller Close");
    }
  };
  hooks.scan = [&](const auto &) {
    return callbackPending ? std::vector<DWORD>{123} : std::vector<DWORD>{};
  };
  try {
    TestCleanup module([&] {
      drained = waitForWebViewsExit({LR"(G:\module-profile)"}, std::chrono::seconds(2), hooks);
      moduleLoaded = false;
      events.push_back("FreeLibrary");
    });
    TestCleanup view([&] { viewAlive = false; callbackPending = true; events.push_back("destroy view"); });
    throw std::runtime_error("injected body failure");
  } catch (const std::runtime_error &) {}
  expect(ordered && drained && callbackDelivered && !moduleLoaded && events == std::vector<std::string>{
             "destroy view", "late controller Close", "FreeLibrary"},
         "failure unwind preserves destroy -> pumping drain -> module unload ordering");
}

void testScopedStorageEnvironment() {
  const std::initializer_list<const wchar_t *> names{
      L"MIV_SOUND_DATA_DIR", L"APPDATA", L"HOME", L"USERPROFILE", L"WEBVIEW2_USER_DATA_FOLDER"};
  const auto snapshot = [&] {
    std::vector<std::optional<std::wstring>> values;
    for (const auto *name : names) values.push_back(testEnvironmentValue(name));
    return values;
  };
  const auto original = snapshot();
  const auto directory = std::filesystem::current_path();
  const auto dataDirectory = testEnvironmentValue(L"MIV_SOUND_DATA_DIR");
  expect(dataDirectory.has_value(), "CTest provides a disposable data root for the environment test");
  const std::filesystem::path root(*dataDirectory);
  auto sandbox = recreateTestSandbox(root, L"environment-scope");
  bool cleanupFailed = false;
  {
    ScopedTestEnvironment baseline(names, cleanupFailed);
    baseline.set(L"APPDATA", nullptr);
    baseline.set(L"HOME", L"");
    baseline.set(L"USERPROFILE", L"baseline user profile");
    baseline.set(L"WEBVIEW2_USER_DATA_FOLDER", nullptr);
    const auto before = snapshot();
    expect(!testEnvironmentValue(L"APPDATA") && testEnvironmentValue(L"HOME") == std::wstring{},
           "capture absent and empty variables distinctly");
    for (bool fail : {false, true}) {
      // Emulate the residue reported after a previous invocation, then repeat
      // both normal and failing cases in this SAME process.
      const auto residue = sandbox / "home/AppData/Local/Microsoft/Feeds Cache";
      std::filesystem::create_directories(residue);
      std::ofstream(residue / "previous-run.txt") << "old Windows cache";
      sandbox = recreateTestSandbox(root, L"environment-scope");
      expect(std::filesystem::is_empty(sandbox), "each invocation starts with a clean sandbox");
      bool injectedFailure = false;
      try {
        ScopedTestEnvironment isolated(names, cleanupFailed);
        for (const auto *name : names) isolated.set(name, L"temporary test value");
        isolated.set(L"HOME", nullptr);
        std::filesystem::current_path(sandbox);
        if (fail) throw std::runtime_error("injected environment test failure");
      } catch (const std::runtime_error &error) {
        expect(std::string(error.what()) == "injected environment test failure", "only catch injected failure");
        injectedFailure = true;
      }
      expect(injectedFailure == fail && snapshot() == before &&
                 std::filesystem::current_path() == directory && !cleanupFailed,
             "normal and failing exits restore every variable and the original current directory");
    }
  }
  expect(snapshot() == original && std::filesystem::current_path() == directory && !cleanupFailed,
         "nested scopes preserve the process environment for subsequent tests");
  bool unsafePathRejected = false;
  try { (void)recreateTestSandbox(root, L".."); }
  catch (const std::runtime_error &) { unsafePathRejected = true; }
  expect(unsafePathRejected && std::filesystem::exists(sandbox), "sandbox cleanup cannot delete its parent");
}

constexpr auto kLengthMismatch = static_cast<NTSTATUS>(0xc0000004UL);
constexpr auto kAccessDenied = static_cast<NTSTATUS>(0xc0000022UL);
constexpr auto kTerminating = static_cast<NTSTATUS>(0xc000010aUL);

NTSTATUS commandLineReply(const std::wstring &command, void *buffer, ULONG capacity, ULONG *size) {
  const auto bytes = (command.size() + 1) * sizeof(wchar_t);
  *size = static_cast<ULONG>(sizeof(UNICODE_STRING) + bytes);
  if (capacity < *size) return kLengthMismatch;
  auto &result = *static_cast<UNICODE_STRING *>(buffer);
  result.Buffer = reinterpret_cast<wchar_t *>(static_cast<unsigned char *>(buffer) + sizeof(result));
  result.Length = static_cast<USHORT>(bytes - sizeof(wchar_t));
  result.MaximumLength = static_cast<USHORT>(bytes);
  std::memcpy(result.Buffer, command.c_str(), bytes);
  return 0;
}

void testTerminatingProcessPumpsOwnerSTA() {
  struct State { DWORD owner = GetCurrentThreadId(); bool delivered = false; bool ownerThread = false; } state;
  WNDCLASSW windowClass{};
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"mIVSoundTerminatingProcessWaitTest";
  windowClass.lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
    if (message == WM_APP + 1) {
      auto &state = *reinterpret_cast<State *>(lParam);
      state.delivered = true;
      state.ownerThread = state.owner == GetCurrentThreadId();
      return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  };
  expect(RegisterClassW(&windowClass) != 0, "register the owner STA message window");
  TestCleanup unregister([&] { UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance); });
  const auto window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
  expect(window != nullptr, "create the owner STA message window");
  TestCleanup destroy([&] { DestroyWindow(window); });
  expect(PostMessageW(window, WM_APP + 1, 0, reinterpret_cast<LPARAM>(&state)) != 0,
         "queue an owner STA completion before waiting for termination");
  unsigned waits = 0;
  const auto result = readProcessCommandLine(42,
      [](void *, ULONG, ULONG *required) { *required = 0; return kTerminating; },
      [&](DWORD) { return ++waits == 9 ? WAIT_OBJECT_0 : WAIT_TIMEOUT; });
  expect(result.empty() && waits == 9 && state.delivered && state.ownerThread,
         "the slow terminating-process wait dispatches completions on the owner STA");
}

void testCommandLineQueryRaces() {
  unsigned queries = 0;
  const auto unavailable = [&](void *, ULONG, ULONG *size) {
    ++queries;
    *size = 0;
    return kTerminating;
  };
  expect(readProcessCommandLine(42, unavailable, [](DWORD) { return WAIT_OBJECT_0; }).empty() &&
             queries == 0, "a process already exited after enumeration needs no command line query");

  unsigned waits = 0;
  const auto terminating = readProcessCommandLine(42, unavailable, [&](DWORD timeout) {
    expect(timeout == (waits == 0 ? 0u : 50u), "exit checks use a bounded wait on the same handle");
    return ++waits == 9 ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
  });
  expect(terminating.empty() && queries == 1 && waits == 9,
         "known termination during sizing waits past the old retry limit until the SAME handle signals");

  const std::wstring command = LR"(msedgewebview2.exe --user-data-dir="G:\test space\webview\EBWebView")";
  queries = 0;
  waits = 0;
  const auto duringRead = readProcessCommandLine(42,
      [&](void *buffer, ULONG size, ULONG *required) {
        ++queries;
        if (buffer == nullptr) return commandLineReply(command, buffer, size, required);
        return kTerminating;
      }, [&](DWORD) { return ++waits == 9 ? WAIT_OBJECT_0 : WAIT_TIMEOUT; });
  expect(duringRead.empty() && queries == 2 && waits == 9,
         "known termination during reading also waits past the old retry limit without failing");

  for (DWORD code : {0u, 1u, 0xc0000005u}) {
    queries = 0;
    waits = 0;
    unsigned exitChecks = 0;
    const auto exitedWithUnreadableCommand = readProcessCommandLine(42,
        [&](void *, ULONG, ULONG *required) { ++queries; *required = 0; return kAccessDenied; },
        [&](DWORD) { return ++waits == 9 ? WAIT_OBJECT_0 : WAIT_TIMEOUT; },
        [&] { ++exitChecks; return code; });
    expect(exitedWithUnreadableCommand.empty() && queries == 1 && waits == 9 && exitChecks == 1,
           "a non-STILL_ACTIVE exit code waits for handle signalling even with an unrelated query error");
  }

  queries = 0;
  const auto resized = readProcessCommandLine(42,
      [&](void *buffer, ULONG size, ULONG *required) {
        ++queries;
        if (queries == 1) {
          *required = sizeof(UNICODE_STRING);
          return kLengthMismatch;
        }
        return commandLineReply(command, buffer, size, required);
      }, [](DWORD) { return WAIT_TIMEOUT; });
  expect(resized == command && queries == 4,
         "a command line growing between sizing and reading is resized and read again");

  for (const bool failureDuringRead : {false, true}) {
    bool rejected = false;
    waits = 0;
    try {
      (void)readProcessCommandLine(42,
          [&](void *buffer, ULONG size, ULONG *required) {
            if (failureDuringRead && buffer == nullptr)
              return commandLineReply(command, buffer, size, required);
            *required = 0;
            return kAccessDenied;
          }, [&](DWORD) { ++waits; return WAIT_TIMEOUT; }, [] { return STILL_ACTIVE; });
    } catch (const std::runtime_error &error) {
      const std::string message(error.what());
      rejected = message.find("PID 42") != std::string::npos &&
                 message.find("NTSTATUS=0xc0000022") != std::string::npos &&
                 message.find("profile ownership unknown") != std::string::npos;
    }
    expect(rejected && waits == 5,
           "an unreadable live process fails after bounded retries, never silently disappears");
  }

  const auto deniedButExited = readProcessCommandLine(42,
      [](void *, ULONG, ULONG *required) { *required = 0; return kAccessDenied; },
      [](DWORD timeout) { return timeout == 0 ? WAIT_TIMEOUT : WAIT_OBJECT_0; });
  expect(deniedButExited.empty(), "even an unreadable process is excluded once its handle signals exit");

  bool invalidResultRejected = false;
  try {
    (void)readProcessCommandLine(42,
        [&](void *buffer, ULONG size, ULONG *required) {
          const auto status = commandLineReply(command, buffer, size, required);
          if (status >= 0) static_cast<UNICODE_STRING *>(buffer)->Buffer = nullptr;
          return status;
        }, [](DWORD) { return WAIT_TIMEOUT; });
  } catch (const std::runtime_error &) { invalidResultRejected = true; }
  expect(invalidResultRejected, "malformed query output never proves that a profile has exited");

  bool waitFailureRejected = false;
  try {
    (void)readProcessCommandLine(42, unavailable, [](DWORD) { return WAIT_FAILED; });
  } catch (const std::runtime_error &) { waitFailureRejected = true; }
  expect(waitFailureRejected, "a failed exit check cannot be treated as a finished process");

  waitFailureRejected = false;
  waits = 0;
  try {
    (void)readProcessCommandLine(42, unavailable,
        [&](DWORD) { return ++waits == 1 ? WAIT_TIMEOUT : WAIT_FAILED; });
  } catch (const std::runtime_error &) { waitFailureRejected = true; }
  expect(waitFailureRejected && waits == 2,
         "known termination still requires a successful handle wait, never just its NTSTATUS");

  expect(processCommandLine(GetCurrentProcess(), GetCurrentProcessId()) == GetCommandLineW(),
         "the native query adapter reads this test process without starting a WebView");
}
}

int main(int argc, char **argv) {
  suppressCrtModalDialogs();
  try {
    expect(argc == 2, "expected the plugin DLL path for read-only observer verification");
    testModuleCallbackObserver(argv[1]);
    testScopedStorageEnvironment();
    testPauseReleaseBeforeJoin();
    testWorkerUnwindPumpsOwnerSTA();
    testLongQueryCannotBypassDeadline();
    testCallbackCompletionIsIndependentOfProcessVisibility();
    testFailedWaitStillDrainsEveryProfile();
    testDrainKeepsModuleAndOwnerStateAlive();
    testTerminatingProcessPumpsOwnerSTA();
    testCommandLineQueryRaces();
    const std::filesystem::path folder = LR"(G:\test space\webview\EBWebView)";
    expect(commandUsesProfile(LR"(msedgewebview2.exe --user-data-dir="G:\test space\webview\EBWebView")", folder),
           "quoted complete profile matches");
    expect(commandUsesProfile(LR"(msedgewebview2.exe --type=renderer --user-data-dir "g:/test space/webview/EBWebView")", folder),
           "child command line and Windows path spelling match");
    for (const auto *command : {
             LR"(msedgewebview2.exe --user-data-dir="G:\test space\webview\EBWebView-other")",
             LR"(msedgewebview2.exe --user-data-dir="G:\test space\webview")",
             LR"(msedgewebview2.exe --some-other-option="G:\test space\webview\EBWebView")",
             LR"(msedgewebview2.exe --user-data-dir="G:\test space\webview\EBWebView" --user-data-dir=G:\other)"})
      expect(!commandUsesProfile(command, folder), "never select another profile or ambiguous command line");

    bool destroyed = false;
    try {
      TestCleanup cleanup([&] { destroyed = true; });
      throw std::runtime_error("synthetic assertion failure");
    } catch (const std::runtime_error &) {}
    expect(destroyed, "assertion failure runs cleanup before the process wait");
    {
      TestThread worker([] { throw std::runtime_error("synthetic worker assertion"); });
      worker.join();
    }
    expect(webViewTestWorkerFailed.load(), "worker failure is recorded instead of terminating the host");
    webViewTestWorkerFailed.store(false);
    std::atomic_bool workerStopped{false};
    {
      TestThread worker([&](std::stop_token stop) {
        while (!stop.stop_requested()) std::this_thread::yield();
        workerStopped.store(true);
      });
    }
    expect(workerStopped.load(), "failure unwind stops and joins a cooperative worker");
    int polls = 0;
    expect(waitForWebViewExit(folder.parent_path().parent_path(), std::chrono::seconds(2),
               [&](const auto &path) {
                 expect(path == folder, "wait targets exactly webview/EBWebView");
                 return ++polls < 3 ? std::vector<DWORD>{123, 456} : std::vector<DWORD>{};
               }), "wait includes browser and renderer until both have exited");
    testDeadline = std::chrono::steady_clock::now();
    bool timedOut = false;
    destroyed = false;
    try {
      TestCleanup cleanup([&] { destroyed = true; });
      checkWebViewTestDeadline();
    } catch (const std::runtime_error &) { timedOut = true; }
    expect(timedOut && destroyed, "deadline unwinds owned resources");
    testDeadline = std::chrono::steady_clock::time_point::max();
    std::cout << "WebView process query races, profile selection, failure cleanup and bounded wait verified\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
