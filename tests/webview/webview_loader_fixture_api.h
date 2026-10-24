#pragma once

#include <Windows.h>

struct WebViewControllerFixtureStats {
  DWORD ownerThread = 0;
  DWORD closeThread = 0;
  ULONG references = 1; // the fixture retains one reference
  int createCalls = 0;
  int coreViewCalls = 0;
  int closeCalls = 0;
  bool closedOnOwnerSTA = false;
  bool releasedBeforeClose = false;
};
