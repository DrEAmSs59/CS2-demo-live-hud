#include "common/paths.hpp"
#include "common/version.hpp"
#include "launcher/cs2_locate.hpp"
#include "launcher/launch_command.hpp"
#include "launcher/process.hpp"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace {

constexpr wchar_t kWindowClass[] = L"CS2DemoLiveHudLauncherWindow";
constexpr wchar_t kAppTitle[] =
    L"CS2 Demo Live HUD v" LIVE_HUD_VERSION_W;
constexpr UINT kLaunchFinished = WM_APP + 1;
constexpr int kDemoEdit = 1001;
constexpr int kBrowseButton = 1002;
constexpr int kLaunchDemoButton = 1003;
constexpr int kLaunchManualButton = 1004;
constexpr int kStatusText = 1005;
constexpr int kCommandText = 1006;

struct AppState {
  HWND window = nullptr;
  HWND demo_edit = nullptr;
  HWND browse_button = nullptr;
  HWND launch_demo_button = nullptr;
  HWND launch_manual_button = nullptr;
  HWND status_text = nullptr;
  HWND command_text = nullptr;
  HFONT ui_font = nullptr;
  bool launching = false;
};

struct LaunchRequest {
  HWND notify = nullptr;
  std::optional<std::filesystem::path> cs2_root;
  std::optional<std::filesystem::path> demo;
};

struct LaunchResult {
  int code = 4;
  std::wstring message;
};

AppState g_app;

HMENU control_id(int id) {
  return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::filesystem::path dirname_of_exe() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  buffer.resize(length);
  return std::filesystem::path(buffer).parent_path();
}

void set_font(HWND control) {
  SendMessageW(control, WM_SETFONT,
               reinterpret_cast<WPARAM>(g_app.ui_font ? g_app.ui_font
                                                      : GetStockObject(DEFAULT_GUI_FONT)),
               TRUE);
}

void set_launching(bool launching, const wchar_t* status) {
  g_app.launching = launching;
  EnableWindow(g_app.demo_edit, !launching);
  EnableWindow(g_app.browse_button, !launching);
  EnableWindow(g_app.launch_demo_button, !launching);
  EnableWindow(g_app.launch_manual_button, !launching);
  SetWindowTextW(g_app.status_text, status);
}

LaunchResult launch(const LaunchRequest& request) {
  if (request.demo && !live_hud::demo_path_ok(*request.demo)) {
    return {1, L"Demo 路径无效，请选择一个现有的 .dem 文件。"};
  }
  if (live_hud::is_cs2_running()) {
    return {3, L"检测到 cs2.exe 正在运行，请完全退出游戏后重试。"};
  }

  const auto cs2 = live_hud::locate_cs2_exe(request.cs2_root);
  if (!cs2) {
    return {2, L"未找到 CS2。请设置 CS2_DEMO_LIVE_HUD_CS2_ROOT，或在命令行使用 --cs2-root。"};
  }

  const auto dll = dirname_of_exe() / L"live_hud.dll";
  std::error_code error;
  if (!std::filesystem::is_regular_file(dll, error)) {
    return {4, L"启动器同目录中缺少 live_hud.dll。"};
  }

  if (!live_hud::start_and_inject(*cs2, request.demo, dll)) {
    return {4, L"CS2 启动或 DLL 注入失败，请查看日志。"};
  }

  std::wstring message = L"CS2 已启动，Pipeline V2 已注入。";
  if (!request.demo) {
    message.append(
        L" 请打开 CS2 控制台并执行：playdemo \"完整路径\\match.dem\"");
  }
  message.append(L"\r\n日志：");
  message.append(live_hud::temp_log_path().wstring());
  return {0, std::move(message)};
}

void run_async(std::optional<std::filesystem::path> demo) {
  set_launching(true, L"正在启动 CS2，并等待 engine2.dll 加载……");
  LaunchRequest request{.notify = g_app.window, .demo = std::move(demo)};
  std::thread([request = std::move(request)]() mutable {
    auto result = std::make_unique<LaunchResult>(launch(request));
    if (IsWindow(request.notify)) {
      auto* raw_result = result.release();
      if (!PostMessageW(request.notify, kLaunchFinished, 0,
                        reinterpret_cast<LPARAM>(raw_result))) {
        delete raw_result;
      }
    }
  }).detach();
}

std::optional<std::filesystem::path> demo_from_edit() {
  const int length = GetWindowTextLengthW(g_app.demo_edit);
  if (length <= 0) {
    return std::nullopt;
  }
  std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(g_app.demo_edit, value.data(), length + 1);
  value.resize(static_cast<std::size_t>(length));
  return live_hud::normalize_demo_path(std::filesystem::path(value));
}

void browse_demo(HWND owner) {
  std::wstring file(32768, L'\0');
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter = L"Counter-Strike 2 demos (*.dem)\0*.dem\0All files\0*.*\0";
  dialog.lpstrFile = file.data();
  dialog.nMaxFile = static_cast<DWORD>(file.size());
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  dialog.lpstrDefExt = L"dem";
  if (GetOpenFileNameW(&dialog)) {
    SetWindowTextW(g_app.demo_edit, file.c_str());
  }
}

void create_controls(HWND window) {
  g_app.window = window;
  HWND title = CreateWindowExW(
      0, L"STATIC", L"CS2 Demo Live HUD v" LIVE_HUD_VERSION_W L" - Pipeline V2",
      WS_CHILD | WS_VISIBLE, 20, 16, 660, 24, window, nullptr, nullptr, nullptr);
  set_font(title);

  HWND warning = CreateWindowExW(
      0, L"STATIC",
      L"仅限本地 Demo。启动器始终使用 -insecure；请勿连接 VAC 服务器。",
      WS_CHILD | WS_VISIBLE, 20, 44, 660, 36, window, nullptr, nullptr,
      nullptr);
  set_font(warning);

  HWND demo_label = CreateWindowExW(
      0, L"STATIC", L"Demo 文件（可选）：", WS_CHILD | WS_VISIBLE, 20, 91,
      180, 22, window, nullptr, nullptr, nullptr);
  set_font(demo_label);

  g_app.demo_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
      20, 115, 550, 27, window, control_id(kDemoEdit), nullptr,
      nullptr);
  set_font(g_app.demo_edit);

  g_app.browse_button = CreateWindowExW(
      0, L"BUTTON", L"浏览……", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 580,
      114, 100, 29, window, control_id(kBrowseButton), nullptr,
      nullptr);
  set_font(g_app.browse_button);

  g_app.launch_demo_button = CreateWindowExW(
      0, L"BUTTON", L"启动所选 Demo",
      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 20, 154, 210, 34, window,
      control_id(kLaunchDemoButton), nullptr, nullptr);
  set_font(g_app.launch_demo_button);

  g_app.launch_manual_button = CreateWindowExW(
      0, L"BUTTON", L"仅启动 CS2",
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 240, 154, 220, 34, window,
      control_id(kLaunchManualButton), nullptr, nullptr);
  set_font(g_app.launch_manual_button);

  g_app.status_text = CreateWindowExW(
      0, L"STATIC", L"就绪。", WS_CHILD | WS_VISIBLE, 20, 199, 660, 42,
      window, control_id(kStatusText), nullptr, nullptr);
  set_font(g_app.status_text);

  HWND command_label = CreateWindowExW(
      0, L"STATIC", L"固定启动参数与控制台指令：",
      WS_CHILD | WS_VISIBLE, 20, 242, 400, 22, window, nullptr, nullptr,
      nullptr);
  set_font(command_label);

  g_app.command_text = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", live_hud::fixed_launch_command_summary().c_str(),
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
          ES_AUTOVSCROLL | ES_READONLY,
      20, 267, 660, 320, window, control_id(kCommandText),
      nullptr, nullptr);
  set_font(g_app.command_text);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      create_controls(window);
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kBrowseButton:
          browse_demo(window);
          return 0;
        case kLaunchDemoButton: {
          const auto demo = demo_from_edit();
          if (!demo) {
            MessageBoxW(window, L"请先选择一个 .dem 文件。", L"需要 Demo",
                        MB_OK | MB_ICONINFORMATION);
            return 0;
          }
          run_async(demo);
          return 0;
        }
        case kLaunchManualButton:
          run_async(std::nullopt);
          return 0;
        default:
          break;
      }
      break;
    case kLaunchFinished: {
      std::unique_ptr<LaunchResult> result(
          reinterpret_cast<LaunchResult*>(lparam));
      set_launching(false, result->message.c_str());
      MessageBoxW(window, result->message.c_str(),
                  result->code == 0 ? L"启动完成" : L"启动失败",
                  MB_OK | (result->code == 0 ? MB_ICONINFORMATION
                                             : MB_ICONERROR));
      return 0;
    }
    case WM_CLOSE:
      if (g_app.launching) {
        MessageBoxW(window, L"请等待当前启动过程完成。",
                    L"正在启动", MB_OK | MB_ICONINFORMATION);
        return 0;
      }
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      if (g_app.ui_font) {
        DeleteObject(g_app.ui_font);
        g_app.ui_font = nullptr;
      }
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

int run_gui(HINSTANCE instance, int show_command) {
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);

  NONCLIENTMETRICSW metrics{};
  metrics.cbSize = sizeof(metrics);
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                            0)) {
    g_app.ui_font = CreateFontIndirectW(&metrics.lfMessageFont);
  }

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = window_proc;
  window_class.hInstance = instance;
  window_class.hCursor =
      LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  window_class.hIcon =
      LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
  window_class.hbrBackground =
      reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&window_class)) {
    if (g_app.ui_font) {
      DeleteObject(g_app.ui_font);
      g_app.ui_font = nullptr;
    }
    return 4;
  }

  HWND window = CreateWindowExW(
      0, kWindowClass, kAppTitle, WS_OVERLAPPED | WS_CAPTION |
          WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 720, 640, nullptr, nullptr, instance,
      nullptr);
  if (!window) {
    if (g_app.ui_font) {
      DeleteObject(g_app.ui_font);
      g_app.ui_font = nullptr;
    }
    return 4;
  }
  ShowWindow(window, show_command);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

int run_command_line(int argc, wchar_t** argv) {
  std::optional<std::filesystem::path> root;
  std::wstring demo_joined;
  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    if (argument == L"--help" || argument == L"-h") {
      MessageBoxW(nullptr, live_hud::fixed_launch_command_summary().c_str(),
                  kAppTitle, MB_OK | MB_ICONINFORMATION);
      return 0;
    }
    if (argument == L"--cs2-root") {
      if (index + 1 >= argc) {
        MessageBoxW(nullptr, L"--cs2-root 后必须提供目录。",
                    L"命令行参数无效", MB_OK | MB_ICONERROR);
        return 1;
      }
      root = argv[++index];
      continue;
    }
    if (argument == L"--no-demo") {
      continue;
    }
    if (!demo_joined.empty()) {
      demo_joined.push_back(L' ');
    }
    demo_joined.append(argument);
  }

  std::optional<std::filesystem::path> demo;
  if (!demo_joined.empty()) {
    demo = live_hud::normalize_demo_path(std::filesystem::path(demo_joined));
  }
  const auto result = launch({.cs2_root = root, .demo = demo});
  MessageBoxW(nullptr, result.message.c_str(),
              result.code == 0 ? L"启动完成" : L"启动失败",
              MB_OK | (result.code == 0 ? MB_ICONINFORMATION : MB_ICONERROR));
  return result.code;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    return 4;
  }
  if (argc > 1) {
    const int result = run_command_line(argc, argv);
    LocalFree(argv);
    return result;
  }
  LocalFree(argv);
  return run_gui(instance, show_command);
}
