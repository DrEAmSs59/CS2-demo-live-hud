#include "dll/hooks.hpp"

#include <windows.h>

namespace {

DWORD WINAPI install_worker(LPVOID) {
  live_hud::install_hooks();
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(self);
    HANDLE th = CreateThread(nullptr, 0, install_worker, nullptr, 0, nullptr);
    if (th) {
      CloseHandle(th);
    }
  } else if (reason == DLL_PROCESS_DETACH) {
    live_hud::remove_hooks();
  }
  return TRUE;
}
