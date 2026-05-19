// win7_spike - static-CRT WebView2 Windows 7 feasibility probe.
//
// Question it answers: does a binary built with the CURRENT MSVC
// toolset, with the C/C++ runtime linked STATICALLY (/MT, no VC++
// redist) and the WebView2 loader linked statically, actually
// launch AND fully drive WebView2 on a real Windows 7 machine -
// including the specific interfaces and host<->renderer bridge
// that mdview itself depends on?
//
// Everything is written to win7_spike_log.txt next to the exe and
// surfaced via a final MessageBox. Stages:
//   (1)  static-CRT .exe started on this OS
//   (2a) installed WebView2 runtime (Win7 caps at v109)
//   (2b) environment created (+ its BrowserVersionString)
//   (2c) controller created
//   (3)  INTERFACE MATRIX: which ICoreWebView2_* / Controller*
//        interfaces mdview uses are present on THIS runtime
//        (the key milestone unknown - newer ones must degrade
//        gracefully on v109)
//   (4)  nav lifecycle (NavigationStarting/Completed, ProcessFailed)
//   (5)  host<->renderer bridge: postMessage in, ExecuteScript out
//        (mdview's summary protocol relies on both)
//
// Correctness mirrors a known-good minimal host: COM is initialised
// STA on the UI thread; controller + webview are held in
// process-lifetime refs (callback-local refs tear down on return
// and nothing composites). Optional command-line URL overrides the
// default inline NavigateToString.
//
// Not shipped, not committed by default - throwaway research scaffold.

#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>

#include <string>
#include <fstream>
#include <filesystem>

using namespace Microsoft::WRL;

namespace {

wil::com_ptr<ICoreWebView2Controller> g_controller;
wil::com_ptr<ICoreWebView2>           g_webview;

std::filesystem::path g_log_path;
HWND                  g_hwnd = nullptr;
std::wstring          g_summary;
std::wstring          g_url;       // empty => NavigateToString
bool                  g_msg_seen = false;

std::wstring hr_hex(HRESULT hr) {
    wchar_t b[16];
    swprintf(b, 16, L"0x%08X", static_cast<unsigned>(hr));
    return b;
}

void log_line(const std::wstring& s) {
    std::wofstream f(g_log_path, std::ios::app);
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t ts[32];
        swprintf(ts, 32, L"[%02d:%02d:%02d] ",
                 st.wHour, st.wMinute, st.wSecond);
        f << ts << s << L"\n";
    }
}

void step(const std::wstring& s) {
    log_line(s);
    g_summary += s;
    g_summary += L"\n";
}

void show(const std::wstring& title) {
    MessageBoxW(g_hwnd, g_summary.c_str(), title.c_str(),
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

// QueryInterface probe: report whether an interface mdview uses is
// implemented by THIS runtime. iid_ppv via __uuidof on the type.
template <typename T>
void probe(const wchar_t* name, IUnknown* base) {
    void* p = nullptr;
    HRESULT hr = base ? base->QueryInterface(__uuidof(T), &p)
                      : E_POINTER;
    const bool ok = SUCCEEDED(hr) && p;
    if (p) reinterpret_cast<IUnknown*>(p)->Release();
    step(std::wstring(L"    ") + name + L" : "
         + (ok ? L"AVAILABLE"
               : L"absent (hr=" + hr_hex(hr) + L")"));
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SIZE && g_controller) {
        RECT rc; GetClientRect(h, &rc);
        g_controller->put_Bounds(rc);
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int nShow) {
    if (cmdLine && *cmdLine) g_url = cmdLine;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_log_path = std::filesystem::path(exe).parent_path()
                 / L"win7_spike_log.txt";
    { std::wofstream f(g_log_path, std::ios::trunc); }

    OSVERSIONINFOEXW os{ sizeof(os) };
#pragma warning(push)
#pragma warning(disable : 4996)
    GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&os));
#pragma warning(pop)
    step(L"(1) STATIC-CRT EXE LAUNCHED. OS reported "
         + std::to_wstring(os.dwMajorVersion) + L"."
         + std::to_wstring(os.dwMinorVersion)
         + L" build " + std::to_wstring(os.dwBuildNumber)
         + L" (Win7 = 6.1).");

    wil::unique_cotaskmem_string ver;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(
        nullptr, &ver);
    if (FAILED(hr) || !ver) {
        step(L"(2a) No usable WebView2 runtime. hr=" + hr_hex(hr)
             + L". The static .exe still RAN - key datapoint. "
               L"Install Evergreen v109 to test render.");
        show(L"win7_spike: launched, no WebView2 runtime");
        return 0;
    }
    step(std::wstring(L"(2a) WebView2 runtime present: ") + ver.get());

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0, 128, 128)); // teal tell
    wc.lpszClassName = L"Win7SpikeWnd";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"mdview Win7 spike",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1000, 700, nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hwnd, nShow ? nShow : SW_SHOW);
    UpdateWindow(g_hwnd);

    hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT ehr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(ehr) || !env) {
                    step(L"(2b) Environment FAILED hr=" + hr_hex(ehr));
                    show(L"win7_spike: env failed");
                    return S_OK;
                }
                wil::unique_cotaskmem_string ev;
                if (SUCCEEDED(env->get_BrowserVersionString(&ev)) && ev)
                    step(std::wstring(L"(2b) Environment OK. "
                         L"BrowserVersionString=") + ev.get());
                else
                    step(L"(2b) Environment created OK.");

                step(L"    [env interface matrix]");
                probe<ICoreWebView2Environment2>(
                    L"ICoreWebView2Environment2 ", env);
                probe<ICoreWebView2Environment5>(
                    L"ICoreWebView2Environment5 ", env);

                env->CreateCoreWebView2Controller(
                    g_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT chr,
                           ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(chr) || !ctrl) {
                                step(L"(2c) Controller FAILED hr="
                                     + hr_hex(chr));
                                show(L"win7_spike: controller failed");
                                return S_OK;
                            }
                            step(L"(2c) Controller created OK.");
                            g_controller = ctrl;
                            g_controller->get_CoreWebView2(&g_webview);
                            if (!g_webview) {
                                step(L"(2d) get_CoreWebView2 null.");
                                show(L"win7_spike: no CoreWebView2");
                                return S_OK;
                            }
                            RECT rc; GetClientRect(g_hwnd, &rc);
                            g_controller->put_Bounds(rc);
                            g_controller->put_IsVisible(TRUE);

                            // (3) Interface matrix - exactly the
                            // surface mdview touches. Newer ones
                            // absent on v109 must degrade (mdview
                            // already gates _22 behind try_query).
                            step(L"(3) INTERFACE MATRIX vs this runtime:");
                            probe<ICoreWebView2_2>(
                                L"ICoreWebView2_2          ", g_webview.get());
                            probe<ICoreWebView2_3>(
                                L"ICoreWebView2_3          ", g_webview.get());
                            probe<ICoreWebView2_13>(
                                L"ICoreWebView2_13(Profile)", g_webview.get());
                            probe<ICoreWebView2_22>(
                                L"ICoreWebView2_22(filter) ", g_webview.get());
                            probe<ICoreWebView2Controller2>(
                                L"ICoreWebView2Controller2 ", g_controller.get());
                            probe<ICoreWebView2Controller3>(
                                L"ICoreWebView2Controller3 ", g_controller.get());

                            // (4) Process + nav lifecycle.
                            EventRegistrationToken t{};
                            g_webview->add_ProcessFailed(
                                Callback<ICoreWebView2ProcessFailedEventHandler>(
                                    [](ICoreWebView2*,
                                       ICoreWebView2ProcessFailedEventArgs* a)
                                        -> HRESULT {
                                        COREWEBVIEW2_PROCESS_FAILED_KIND k{};
                                        if (a) a->get_ProcessFailedKind(&k);
                                        step(L"(4) ProcessFailed kind="
                                             + std::to_wstring(
                                                 static_cast<int>(k)));
                                        return S_OK;
                                    }).Get(), &t);
                            g_webview->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2*,
                                       ICoreWebView2NavigationStartingEventArgs*)
                                        -> HRESULT {
                                        step(L"(4) NavigationStarting");
                                        return S_OK;
                                    }).Get(), &t);
                            g_webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* w,
                                       ICoreWebView2NavigationCompletedEventArgs* a)
                                        -> HRESULT {
                                        BOOL ok = FALSE;
                                        COREWEBVIEW2_WEB_ERROR_STATUS st =
                                          COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                        if (a) {
                                            a->get_IsSuccess(&ok);
                                            a->get_WebErrorStatus(&st);
                                        }
                                        step(L"(4) NavigationCompleted "
                                             L"IsSuccess="
                                             + std::wstring(ok ? L"TRUE"
                                                               : L"FALSE")
                                             + L" WebErrorStatus="
                                             + std::to_wstring(
                                                 static_cast<int>(st)));
                                        // (5b) host<-renderer: read
                                        // navigator.userAgent back.
                                        if (w) w->ExecuteScript(
                                            L"navigator.userAgent",
                                            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                                [](HRESULT, LPCWSTR r)
                                                    -> HRESULT {
                                                    step(std::wstring(
                                                      L"(5b) ExecuteScript "
                                                      L"UA=") +
                                                      (r ? r : L"(null)"));
                                                    step(g_msg_seen
                                                      ? L"==> FULL STACK OK: "
                                                        L"render + JS + "
                                                        L"host<->renderer "
                                                        L"bridge all work "
                                                        L"on this runtime."
                                                      : L"==> render+JS OK; "
                                                        L"postMessage NOT "
                                                        L"yet seen (see 5a).");
                                                    show(L"win7_spike: result");
                                                    return S_OK;
                                                }).Get());
                                        return S_OK;
                                    }).Get(), &t);

                            // (5a) renderer->host bridge.
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*,
                                       ICoreWebView2WebMessageReceivedEventArgs* a)
                                        -> HRESULT {
                                        wil::unique_cotaskmem_string m;
                                        if (a) a->TryGetWebMessageAsString(&m);
                                        g_msg_seen = true;
                                        step(std::wstring(
                                             L"(5a) WebMessageReceived: ")
                                             + (m ? m.get() : L"(json)"));
                                        return S_OK;
                                    }).Get(), &t);

                            if (!g_url.empty()) {
                                step(L"(2d) Navigate -> " + g_url);
                                g_webview->Navigate(g_url.c_str());
                            } else {
                                step(L"(2d) NavigateToString (inline).");
                                g_webview->NavigateToString(
                                    L"<!doctype html><meta charset=utf-8>"
                                    L"<body style='font-family:Segoe UI,"
                                    L"sans-serif;margin:24px'>"
                                    L"<h1>WebView2 OK on this OS</h1>"
                                    L"<p>static-CRT spike rendered.</p>"
                                    L"<pre id=ua></pre><script>"
                                    L"document.getElementById('ua')"
                                    L".textContent=navigator.userAgent;"
                                    L"if(window.chrome&&chrome.webview)"
                                    L"chrome.webview.postMessage("
                                    L"'ping-from-win7-renderer');"
                                    L"</script></body>");
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        step(L"(2b) CreateCoreWebView2EnvironmentWithOptions hr="
             + hr_hex(hr) + L" (synchronous).");
        show(L"win7_spike: env call failed");
        return 0;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
