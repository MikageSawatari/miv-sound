#pragma once

#include "crt_dialog_suppression.h"

#include <shellapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace effetune::vst::testing {

inline std::atomic_bool webViewTestWorkerFailed{false};

inline void pumpWebViewTestMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

// Assertion failures in worker threads must not call std::terminate and skip
// every WebView/COM destructor in the process. The host reports them as failure.
class TestThread {
public:
  TestThread() = default;
  template <class Body>
  explicit TestThread(Body body, std::function<void()> beforeJoin = {})
      : beforeJoin_(std::move(beforeJoin)),
        thread_([body = std::move(body)](std::stop_token stop) mutable {
    (void)stop;
    try {
      if constexpr (std::is_invocable_v<Body, std::stop_token>) body(stop);
      else body();
    } catch (const std::exception &error) {
      std::cerr << "Test worker failed: " << error.what() << '\n';
      webViewTestWorkerFailed.store(true);
    } catch (...) {
      std::cerr << "Test worker failed: unknown exception\n";
      webViewTestWorkerFailed.store(true);
    }
  }) {}
  ~TestThread() { releaseBeforeJoin(); }
  TestThread(TestThread &&other) noexcept
      : beforeJoin_(std::exchange(other.beforeJoin_, {})), thread_(std::move(other.thread_)) {}
  TestThread &operator=(TestThread &&other) noexcept {
    if (this != &other) {
      releaseBeforeJoin();
      thread_ = std::move(other.thread_);
      beforeJoin_ = std::exchange(other.beforeJoin_, {});
    }
    return *this;
  }
  void join() { releaseBeforeJoin(); thread_.join(); }
  void joinPumping() {
    releaseBeforeJoin();
    while (WaitForSingleObject(thread_.native_handle(), 0) == WAIT_TIMEOUT) {
      pumpWebViewTestMessages();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    thread_.join();
  }
private:
  void releaseBeforeJoin() noexcept {
    if (!beforeJoin_) return;
    try { std::exchange(beforeJoin_, {})(); }
    catch (...) {
      std::cerr << "Test worker pause/hook release failed\n";
      webViewTestWorkerFailed.store(true);
    }
  }
  std::function<void()> beforeJoin_;
  std::jthread thread_;
};

class TestCleanup {
public:
  explicit TestCleanup(std::function<void()> cleanup) : cleanup_(std::move(cleanup)) {}
  ~TestCleanup() { run(); }
  void run() { if (cleanup_) std::exchange(cleanup_, {})(); }
  TestCleanup(const TestCleanup &) = delete;
  TestCleanup &operator=(const TestCleanup &) = delete;
private:
  std::function<void()> cleanup_;
};

// Compare complete Windows path arguments, never substrings or directory prefixes.
inline std::wstring normaliseProfilePath(const std::filesystem::path &path) {
  auto result = path.lexically_normal().wstring();
  for (auto &character : result) if (character == L'/') character = L'\\';
  while (result.size() > 3 && result.back() == L'\\') result.pop_back();
  return result;
}

inline bool commandUsesProfile(const std::wstring &command,
                               const std::filesystem::path &profile) {
  int count = 0;
  auto **arguments = CommandLineToArgvW(command.c_str(), &count);
  if (arguments == nullptr) throw std::runtime_error("Cannot parse WebView2 command line");
  bool matches = false;
  unsigned switches = 0;
  const auto expected = normaliseProfilePath(profile);
  for (int index = 1; index < count; ++index) {
    const std::wstring argument(arguments[index]);
    constexpr wchar_t prefix[] = L"--user-data-dir=";
    if (argument.starts_with(prefix)) {
      ++switches;
      const auto value = normaliseProfilePath(argument.substr(std::size(prefix) - 1));
      matches = _wcsicmp(value.c_str(), expected.c_str()) == 0;
    } else if (argument == L"--user-data-dir" && index + 1 < count) {
      ++switches;
      const auto value = normaliseProfilePath(arguments[++index]);
      matches = _wcsicmp(value.c_str(), expected.c_str()) == 0;
    }
  }
  LocalFree(arguments);
  return switches == 1 && matches;
}

class ProcessHandle {
public:
  explicit ProcessHandle(HANDLE value = nullptr) : value_(value) {}
  ~ProcessHandle() { if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  ProcessHandle(const ProcessHandle &) = delete;
  ProcessHandle &operator=(const ProcessHandle &) = delete;
  ProcessHandle(ProcessHandle &&other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  HANDLE get() const { return value_; }
private:
  HANDLE value_;
};

// A terminating process can lose its command line long before its handle becomes
// signalled. Once termination is known, keep the SAME handle and pump the owner
// STA until exit is confirmed. Other query failures retain bounded retries and
// must not silently exclude a live process with unknown profile ownership.
template <class Query, class Wait>
std::wstring readProcessCommandLine(DWORD processId, Query query, Wait wait,
                                   std::function<DWORD()> exitCode = [] { return STILL_ACTIVE; }) {
  const auto exited = [&](DWORD timeout) {
    const auto state = wait(timeout);
    if (state == WAIT_OBJECT_0) return true;
    if (state != WAIT_TIMEOUT)
      throw std::runtime_error("Cannot wait for msedgewebview2.exe PID " +
                               std::to_string(processId));
    return false;
  };
  if (exited(0)) return {};
  NTSTATUS status = 0;
  const char *stage = "size";
  for (unsigned attempt = 0; attempt < 4; ++attempt) {
    ULONG size = 0;
    stage = "size";
    status = query(nullptr, 0, &size);
    // The sizing call normally returns STATUS_INFO_LENGTH_MISMATCH together
    // with the required size. Keep its status for diagnostics if no size exists.
    constexpr auto processIsTerminating = static_cast<NTSTATUS>(0xc000010aUL);
    if (status != processIsTerminating && size >= sizeof(UNICODE_STRING) && size <= 1024 * 1024) {
      std::vector<unsigned char> buffer(size);
      stage = "read";
      status = query(buffer.data(), size, &size);
      if (status >= 0) {
        const auto &command = *reinterpret_cast<const UNICODE_STRING *>(buffer.data());
        const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
        const auto end = begin + buffer.size();
        const auto text = reinterpret_cast<std::uintptr_t>(command.Buffer);
        // Do not interpret an empty or malformed result as evidence of another
        // profile. It can also be a process whose parameters are disappearing.
        if (command.Length != 0 && command.Length % sizeof(wchar_t) == 0 &&
            command.Length <= command.MaximumLength && text >= begin + sizeof(UNICODE_STRING) &&
            text <= end && command.Length <= end - text && text % alignof(wchar_t) == 0)
          return std::wstring(command.Buffer, command.Length / sizeof(wchar_t));
        stage = "empty or malformed result";
      }
    }
    // Neither a terminating NTSTATUS nor a non-STILL_ACTIVE exit code alone
    // proves that all process cleanup is complete. Wait for the handle to signal,
    // without converting slow termination into a sticky test failure. CTest's
    // finite outer TIMEOUT remains the last resort for a stuck termination.
    if (status == processIsTerminating || exitCode() != STILL_ACTIVE) {
      while (!exited(50)) pumpWebViewTestMessages();
      return {};
    }
    // Other query failures (including command-line size races) may recover, but
    // a live unreadable process cannot be excluded on ownership assumptions.
    if (exited(50)) return {};
  }
  std::ostringstream error;
  error << "Cannot query msedgewebview2.exe PID " << processId
        << " command line (" << stage << ", NTSTATUS=0x" << std::hex
        << static_cast<std::uint32_t>(status)
        << "); process exit not confirmed, profile ownership unknown";
  throw std::runtime_error(error.str());
}

inline std::wstring processCommandLine(HANDLE process, DWORD processId) {
  // ProcessCommandLineInformation is available on the Windows versions supported
  // by WebView2. Unlike reading the remote PEB this also works across bitness.
  using Query = NTSTATUS(NTAPI *)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
  const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                          "NtQueryInformationProcess"));
  if (query == nullptr) throw std::runtime_error("NtQueryInformationProcess is unavailable");
  return readProcessCommandLine(processId,
      [&](void *buffer, ULONG size, ULONG *required) {
        return query(process, static_cast<PROCESSINFOCLASS>(60), buffer, size, required);
      }, [&](DWORD timeout) { return WaitForSingleObject(process, timeout); }, [&] {
        DWORD code = STILL_ACTIVE;
        if (!GetExitCodeProcess(process, &code))
          throw std::runtime_error("Cannot read msedgewebview2.exe PID " +
                                   std::to_string(processId) + " exit code; profile ownership unknown");
        return code;
      });
}

inline std::vector<DWORD> profileProcesses(const std::filesystem::path &profile) {
  ProcessHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (snapshot.get() == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate processes");
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot.get(), &entry)) throw std::runtime_error("Cannot read process snapshot");
  std::vector<DWORD> matches;
  do {
    if (_wcsicmp(entry.szExeFile, L"msedgewebview2.exe") != 0) continue;
    ProcessHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                      FALSE, entry.th32ProcessID));
    if (process.get() == nullptr) {
      const auto error = GetLastError();
      if (error == ERROR_INVALID_PARAMETER) continue; // exited since the snapshot
      throw std::runtime_error("Cannot inspect msedgewebview2.exe PID " +
                               std::to_string(entry.th32ProcessID) + " (Win32=" +
                               std::to_string(error) + "); profile ownership unknown");
    }
    const auto command = processCommandLine(process.get(), entry.th32ProcessID);
    if (!command.empty() && commandUsesProfile(command, profile))
      matches.push_back(entry.th32ProcessID);
  } while (Process32NextW(snapshot.get(), &entry));
  if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("Incomplete process snapshot");
  return matches;
}

struct WebViewExitWaitHooks {
  std::function<std::vector<DWORD>(const std::filesystem::path &)> scan = profileProcesses;
  std::function<std::chrono::steady_clock::time_point()> now = std::chrono::steady_clock::now;
  std::function<void()> pump = pumpWebViewTestMessages;
  std::function<void()> pause = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };
};

// Failure and permission to exit are separate. A failed query or deadline makes
// the verdict sticky, but NEVER ends the owner STA or skips the other folders.
// Only CTest's finite outer TIMEOUT may stop an irrecoverably broken run.
inline bool waitForWebViewsExit(const std::vector<std::filesystem::path> &directories,
                               std::chrono::milliseconds timeout = std::chrono::seconds(120),
                               WebViewExitWaitHooks hooks = {}) {
  struct Profile {
    std::filesystem::path path;
    std::chrono::steady_clock::time_point quietSince;
    std::string lastError;
  };
  const auto start = hooks.now();
  std::vector<Profile> profiles;
  for (const auto &directory : directories)
    profiles.push_back({directory / "webview" / "EBWebView", start, {}});
  bool passed = true;
  bool deadlineReported = false;
  for (;;) {
    hooks.pump();
    bool allExited = true;
    for (auto &profile : profiles) {
      try {
        const auto remaining = hooks.scan(profile.path);
        const auto now = hooks.now();
        if (!remaining.empty()) profile.quietSince = now;
        if (!remaining.empty() || now - profile.quietSince < std::chrono::milliseconds(500))
          allExited = false;
      } catch (const std::exception &error) {
        passed = false;
        allExited = false;
        profile.quietSince = hooks.now();
        if (profile.lastError != error.what()) {
          std::wcerr << L"WebView2 exit query failed for " << profile.path.wstring() << L"; ";
          std::cerr << error.what() << "; test FAILED, continuing to wait\n";
          profile.lastError = error.what();
        }
      }
    }
    if (allExited) {
      for (const auto &profile : profiles)
        std::wcout << L"WebView2 exit verified: " << profile.path.wstring() << L'\n';
      return passed;
    }
    if (!deadlineReported && hooks.now() - start >= timeout) {
      passed = false;
      deadlineReported = true;
      std::cerr << "WebView2 exit deadline exceeded; test FAILED, continuing to wait for all profiles\n";
    }
    hooks.pause();
  }
}

inline bool waitForWebViewExit(const std::filesystem::path &directory,
                              std::chrono::milliseconds timeout = std::chrono::seconds(120),
                              std::function<std::vector<DWORD>(const std::filesystem::path &)> scan =
                                  profileProcesses) {
  WebViewExitWaitHooks hooks;
  hooks.scan = std::move(scan);
  return waitForWebViewsExit({directory}, timeout, std::move(hooks));
}

inline thread_local std::chrono::steady_clock::time_point testDeadline =
    (std::chrono::steady_clock::time_point::max)();
inline thread_local std::vector<std::filesystem::path> webViewTestDirectories;
inline thread_local bool webViewCleanupFailed = false;

inline void rememberWebViewTestDirectory(const std::filesystem::path &directory) {
  webViewTestDirectories.push_back(directory);
}

// Use before ExitDll/FreeLibrary and before CoUninitialize on an owner STA.
inline void drainTrackedWebViews() {
  if (!waitForWebViewsExit(webViewTestDirectories)) webViewCleanupFailed = true;
}

inline void checkWebViewTestDeadline() {
  if (std::chrono::steady_clock::now() >= testDeadline)
    throw std::runtime_error("WebView test deadline exceeded; unwinding before browser exit wait");
}

template <class Rep, class Period>
auto webViewTestDeadlineAfter(std::chrono::duration<Rep, Period> duration) {
  const auto local = std::chrono::steady_clock::now() + duration;
  return local < testDeadline ? local : testDeadline;
}

template <class Body>
int runWebViewTest(Body &&body, std::chrono::seconds budget = std::chrono::seconds(120)) {
  suppressCrtModalDialogs();
  std::wstring directory(32768, L'\0');
  const auto length = GetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", directory.data(),
                                             static_cast<DWORD>(directory.size()));
  if (length == 0 || length >= directory.size()) {
    std::cerr << "WebView test requires MIV_SOUND_DATA_DIR\n";
    return 1;
  }
  directory.resize(length);
  if (!std::filesystem::path(directory).is_absolute()) return 1;
  webViewTestDirectories.clear();
  webViewCleanupFailed = false;
  webViewTestWorkerFailed.store(false);
  rememberWebViewTestDirectory(directory);
  testDeadline = std::chrono::steady_clock::now() + budget;
  int result = 1;
  try {
    result = body(); // DLL owners also drain before ExitDll/FreeLibrary during unwind.
    checkWebViewTestDeadline();
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    result = 1;
  } catch (...) {
    std::cerr << "Test failure: unknown exception\n";
    result = 1;
  }
  testDeadline = (std::chrono::steady_clock::time_point::max)();
  drainTrackedWebViews();
  return webViewCleanupFailed || webViewTestWorkerFailed.load() ? 1 : result;
}

} // namespace effetune::vst::testing
