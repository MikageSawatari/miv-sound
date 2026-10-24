#include "webview_test_lifetime.h"

// CMake renames only the SDK's entry point; its sources remain untouched.
int mivSoundValidatorMain(int argc, wchar_t *argv[]);

int wmain(int argc, wchar_t **argv) {
  return effetune::vst::testing::runWebViewTest(
      [&] { return mivSoundValidatorMain(argc, argv); });
}
