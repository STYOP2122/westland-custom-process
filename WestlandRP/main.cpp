#include <windows.h>

#include "peb.h"
#include "resource.h"

namespace
{
    constexpr wchar_t kProcessName[] = L"Westland RP";
    constexpr wchar_t kWindowTitle[] = L"Westland RP";

    HMODULE g_module = nullptr;
    HICON   g_bigIcon = nullptr;
    HICON   g_smallIcon = nullptr;

    HWND FindProcessMainWindow()
    {
        struct SearchContext
        {
            DWORD processId;
            HWND  result;
            LONG  bestArea;
        };

        SearchContext context{ GetCurrentProcessId(), nullptr, 0 };

        EnumWindows(
            [](HWND hwnd, LPARAM lParam) -> BOOL {
                auto* ctx = reinterpret_cast<SearchContext*>(lParam);

                DWORD windowProcessId = 0;
                GetWindowThreadProcessId(hwnd, &windowProcessId);
                if (windowProcessId != ctx->processId || !IsWindowVisible(hwnd))
                    return TRUE;

                if (GetWindow(hwnd, GW_OWNER) != nullptr)
                    return TRUE;

                RECT rect{};
                if (!GetWindowRect(hwnd, &rect))
                    return TRUE;

                const LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
                if (area > ctx->bestArea)
                {
                    ctx->bestArea = area;
                    ctx->result = hwnd;
                }

                return TRUE;
            },
            reinterpret_cast<LPARAM>(&context));

        return context.result;
    }

    void EnsureIconsLoaded()
    {
        if (!g_bigIcon)
        {
            g_bigIcon = static_cast<HICON>(
                LoadImageW(g_module, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
        }

        if (!g_smallIcon)
        {
            g_smallIcon = static_cast<HICON>(
                LoadImageW(g_module, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        }
    }

    void ApplyWindowBranding(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;

        SetWindowTextW(hwnd, kWindowTitle);
        EnsureIconsLoaded();

        if (g_bigIcon)
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_bigIcon));

        if (g_smallIcon)
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_smallIcon));
    }

    DWORD WINAPI BrandingThread(LPVOID)
    {
        // ASI loads before the game is fully initialized — wait to avoid PEB/LDR crashes.
        Sleep(5000);

        for (int attempt = 0; attempt < 120; ++attempt)
        {
            ApplyProcessBranding(kProcessName);

            const HWND hwnd = FindProcessMainWindow();
            if (hwnd)
            {
                ApplyWindowBranding(hwnd);
                break;
            }

            Sleep(500);
        }

        while (true)
        {
            const HWND hwnd = FindProcessMainWindow();
            if (hwnd)
                ApplyWindowBranding(hwnd);

            Sleep(2000);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, BrandingThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
