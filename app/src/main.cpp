// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "EditorWindow.hpp"

#include "Application.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <shellapi.h>
#include <string>
#include <vector>
// Opt in to DPI awareness so Windows doesn't bitmap-stretch the window
// (which makes it blurry); we draw at the real pixel density instead.
static void enableDpiAwareness()
{
    if (HMODULE shcore = LoadLibraryA("shcore.dll")) {
        typedef HRESULT(WINAPI * SetAwareness)(int);
        const auto fn = reinterpret_cast<SetAwareness>(reinterpret_cast<void*>(GetProcAddress(shcore, "SetProcessDpiAwareness")));
        const bool ok = fn != nullptr && SUCCEEDED(fn(1 /* PROCESS_SYSTEM_DPI_AWARE */));
        if (ok)
            return;
    }
    SetProcessDPIAware();
}

// Tell the UI toolkit the display scale. (Its own monitor query isn't
// reliable everywhere; the system DPI is, once we're DPI aware.)
static void exportScaleFactor()
{
    if (getenv("DPF_SCALE_FACTOR") != nullptr)
        return;
    HDC dc = GetDC(nullptr);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr)
        ReleaseDC(nullptr, dc);
    if (dpi > 96) {
        char v[32];
        snprintf(v, sizeof(v), "%.4f", dpi / 96.0);
        SetEnvironmentVariableA("DPF_SCALE_FACTOR", v);
        _putenv_s("DPF_SCALE_FACTOR", v);
    }
}
#endif

static int mendMain(int argc, char** argv)
{
    std::set_terminate([] {
        if (auto e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& ex) {
                fprintf(stderr, "Mend: unhandled exception: %s\n", ex.what());
            } catch (...) {
            }
        }
        std::abort();
    });
#ifdef _WIN32
    enableDpiAwareness();
    exportScaleFactor();
#endif
    DGL_NAMESPACE::Application app;
    DGL_NAMESPACE::EditorWindow window(app);
#ifdef _WIN32
    RECT wa;
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) {
        const int cap = GetSystemMetrics(SM_CYCAPTION) + 2 * GetSystemMetrics(SM_CYFRAME) + 8;
        window.setMaxInitialSize(static_cast<uint>(wa.right - wa.left - 16), static_cast<uint>(wa.bottom - wa.top - cap));
    }
#endif
    window.showAtDefaultSize();
    if (argc > 1) {
        try {
            window.openPath(argv[1]);
        } catch (const std::exception& e) {
            fprintf(stderr, "open failed: %s\n", e.what());
        }
    }
    app.exec();
    return 0;
}

#ifndef _WIN32
int main(int argc, char** argv)
{
    return mendMain(argc, argv);
}
#else
// GUI-subsystem entry point on Windows.
// Arguments are re-read as UTF-16 and passed on as UTF-8, so file names
// in any language open correctly.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; wargv != nullptr && i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string a(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
        if (n > 1)
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &a[0], n, nullptr, nullptr);
        args.push_back(a);
    }
    if (wargv != nullptr)
        LocalFree(wargv);
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(&a[0]);
    argv.push_back(nullptr);
    return mendMain(static_cast<int>(args.size()), argv.data());
}
#endif
