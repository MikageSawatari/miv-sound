#include "../support/webview_test_lifetime.h"
#include "../support/scoped_test_environment.h"

#include <cstring>
#include <fstream>

using namespace effetune::vst::testing;

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
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
    return ++waits == 3 ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
  });
  expect(terminating.empty() && queries == 2,
         "termination during sizing can precede handle signalling and is retried");

  const std::wstring command = LR"(msedgewebview2.exe --user-data-dir="G:\test space\webview\EBWebView")";
  queries = 0;
  const auto duringRead = readProcessCommandLine(42,
      [&](void *buffer, ULONG size, ULONG *required) {
        ++queries;
        if (buffer == nullptr) return commandLineReply(command, buffer, size, required);
        return kTerminating;
      }, [](DWORD timeout) { return timeout == 0 ? WAIT_TIMEOUT : WAIT_OBJECT_0; });
  expect(duringRead.empty() && queries == 2,
         "a process exiting between sizing and reading is removed from the wait set");

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
          }, [&](DWORD) { ++waits; return WAIT_TIMEOUT; });
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

  expect(processCommandLine(GetCurrentProcess(), GetCurrentProcessId()) == GetCommandLineW(),
         "the native query adapter reads this test process without starting a WebView");
}
}

int main() {
  suppressCrtModalDialogs();
  try {
    testScopedStorageEnvironment();
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
    expect(!waitForWebViewExit(folder, std::chrono::milliseconds(10),
                [](const auto &) { return std::vector<DWORD>{123}; }),
           "process exit timeout is a failure, never a successful kill");
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
