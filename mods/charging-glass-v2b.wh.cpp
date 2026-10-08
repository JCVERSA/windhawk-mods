// ==WindhawkMod==
// @id              charging-glass-v2b
// @name            Charging Glass v2B
// @description     An iOS-style Liquid Glass charging animation: a glowing battery ring with your percentage appears when you plug in, then fades away on its own.
// @version         1.0.0
// @author          JCVERSA
// @github          https://github.com/JCVERSA
// @license         MIT
// @include         windhawk.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*

# Charging Glass v2B

Plug your laptop in and a small piece of iOS appears on your Windows desktop:
a translucent Liquid Glass circle blooms in the middle of the screen with a
glowing battery ring, your current percentage, and a charging bolt — then it
fades away by itself after a few seconds, like on an iPhone.

Part of the community **v2B** series by [JCVERSA](https://github.com/JCVERSA),
built with the same rendering ideas as Dynamic Island for Windows v2B
(Direct2D, spring animations, Liquid Glass surfaces).

---

## ✨ Features

- **Triggered by real power events** — the animation appears when Windows
  reports that AC power was connected, and fades away on its own after the
  configured duration. If you unplug while it is showing, it dismisses itself.
- **Liquid Glass surface** — a translucent disc with a light-catching specular
  edge and top sheen, over a soft accent glow.
- **Battery ring + percentage** — the ring sweeps up to your current charge
  level while the overlay appears; the percentage is shown in large, calm
  type. When Windows cannot read the battery level, a charging glyph is shown
  instead.
- **Zero CPU while idle** — the overlay window is fully hidden and parked when
  nothing is charging; no polling, no timers, no background rendering.
- **Never in the way** — the window is topmost, non-activating and
  click-through: it cannot steal focus or swallow a single click.
- **Reduce motion** — one setting turns the animation into a simple fade.

## ⚙️ Settings

| Setting | What it does |
|---|---|
| Display duration | How long the overlay stays before fading away (1–15 s). |
| Show battery percentage | Large percentage figure inside the ring. |
| Glow color | Ring and glow color (default: iOS green `#34C759`). |
| Size scale | Overall size of the overlay. |
| Reduce motion | Fade only, no scale or ring sweep. |

## ℹ️ Notes

- The mod reacts to Windows power events (`PBT_APMPOWERSTATUSCHANGE`), so it
  works with any charger recognized by the system.
- On a desktop PC without a battery, the charging event never fires and the
  overlay simply never appears.
- The overlay is shown centered on the primary monitor.

## 📝 License and credits

Released under the MIT license. Rendering approach inspired by the open-source
Dynamic Island for Windows project by Himanshu (devcode90) and contributors.

*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- DurationSeconds: 3
  $name: Display duration (seconds)
  $description: How long the charging animation stays on screen before fading away. 1 to 15 seconds.
- ShowPercentage: true
  $name: Show battery percentage
  $description: Displays the current battery percentage inside the ring. When Windows cannot read the battery level, a charging glyph is shown instead.
- GlowColorHex: "#34C759"
  $name: Glow color
  $description: Hex color of the battery ring and background glow. Defaults to the iOS green.
- SizeScale: 100
  $name: Size scale
  $description: 50 to 200. Overall size of the charging overlay.
- ReduceMotion: false
  $name: Reduce motion
  $description: Skips the scale and ring sweep animations; the overlay simply fades in and out.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWindowClass[] = L"Windhawk.ChargingGlassV2B";
constexpr UINT WM_APP_POWER_CHECK = WM_APP + 1;
constexpr UINT WM_APP_DISMISS = WM_APP + 2;

double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <typename T>
T ClampValue(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float SmoothStep(float t) {
    t = ClampValue(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Parses #RGB, #RRGGBB and #RRGGBBAA hex colors.
bool ColorFromHex(const std::wstring& hex, D2D1_COLOR_F* out) {
    std::wstring s;
    for (wchar_t ch : hex) {
        if (ch != L'#' && ch != L' ' && ch != L'"' && ch != L'\'') {
            s.push_back(ch);
        }
    }
    auto nibble = [](wchar_t ch, int* value) -> bool {
        if (ch >= L'0' && ch <= L'9') { *value = ch - L'0'; return true; }
        if (ch >= L'a' && ch <= L'f') { *value = ch - L'a' + 10; return true; }
        if (ch >= L'A' && ch <= L'F') { *value = ch - L'A' + 10; return true; }
        return false;
    };
    auto byteAt = [&](size_t i, float* outByte) -> bool {
        int hi = 0, lo = 0;
        if (i + 1 >= s.size() || !nibble(s[i], &hi) || !nibble(s[i + 1], &lo)) {
            return false;
        }
        *outByte = static_cast<float>(hi * 16 + lo) / 255.0f;
        return true;
    };

    D2D1_COLOR_F c = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    if (s.size() == 3) {
        int r = 0, g = 0, b = 0;
        if (!nibble(s[0], &r) || !nibble(s[1], &g) || !nibble(s[2], &b)) return false;
        c.r = static_cast<float>(r * 17) / 255.0f;
        c.g = static_cast<float>(g * 17) / 255.0f;
        c.b = static_cast<float>(b * 17) / 255.0f;
    } else if (s.size() == 6 || s.size() == 8) {
        if (!byteAt(0, &c.r) || !byteAt(2, &c.g) || !byteAt(4, &c.b)) return false;
        if (s.size() == 8 && !byteAt(6, &c.a)) return false;
    } else {
        return false;
    }
    *out = c;
    return true;
}

struct Settings {
    int durationSeconds = 3;
    bool showPercentage = true;
    D2D1_COLOR_F glowColor = D2D1::ColorF(0.204f, 0.780f, 0.349f);  // #34C759
    float sizeScale = 1.0f;
    bool reduceMotion = false;
};

Settings g_settings;
CRITICAL_SECTION g_settingsCs;
std::atomic<bool> g_settingsDirty{false};

Settings GetSettingsCopy() {
    EnterCriticalSection(&g_settingsCs);
    Settings copy = g_settings;
    LeaveCriticalSection(&g_settingsCs);
    return copy;
}

void LoadSettings() {
    Settings next;
    next.durationSeconds = ClampValue(Wh_GetIntSetting(L"DurationSeconds"), 1, 15);
    next.showPercentage = Wh_GetIntSetting(L"ShowPercentage") != 0;
    next.reduceMotion = Wh_GetIntSetting(L"ReduceMotion") != 0;
    next.sizeScale = ClampValue(Wh_GetIntSetting(L"SizeScale") / 100.0f, 0.5f, 2.0f);

    PCWSTR hex = Wh_GetStringSetting(L"GlowColorHex");
    if (hex) {
        D2D1_COLOR_F parsed;
        if (ColorFromHex(hex, &parsed)) {
            next.glowColor = parsed;
        }
        Wh_FreeStringSetting(hex);
    }

    EnterCriticalSection(&g_settingsCs);
    g_settings = next;
    LeaveCriticalSection(&g_settingsCs);
    g_settingsDirty = true;
}

// ---------------------------------------------------------------------------
// Power state
// ---------------------------------------------------------------------------

// Returns the AC line status: 0 = offline, 1 = online, 255 = unknown.
BYTE ReadAcLineStatus() {
    SYSTEM_POWER_STATUS ps = {};
    if (!GetSystemPowerStatus(&ps)) {
        return 255;
    }
    return ps.ACLineStatus;
}

// Returns battery percent 0..100, or 255 when unknown.
BYTE ReadBatteryPercent() {
    SYSTEM_POWER_STATUS ps = {};
    if (!GetSystemPowerStatus(&ps)) {
        return 255;
    }
    return ps.BatteryLifePercent;
}

BYTE g_prevAcStatus = 255;
std::atomic<HWND> g_hwnd{nullptr};
HANDLE g_stopEvent = nullptr;

// ---------------------------------------------------------------------------
// Animation state
// ---------------------------------------------------------------------------

enum class Phase {
    Idle,
    Enter,
    Hold,
    Exit,
};

struct AnimState {
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    double holdUntil = 0.0;
    int batteryPercent = -1;  // -1 = unknown
    bool unplugged = false;
};

// The backing bitmap is a raw GDI DIB (the DC render target draws into it and
// UpdateLayeredWindow publishes it), so it lives outside the ComPtr world.
static HBITMAP g_dib = nullptr;
static HDC g_memDc = nullptr;
static int g_bitmapSide = 0;

// ---------------------------------------------------------------------------
// Overlay window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMPOWERSTATUSCHANGE) {
                PostMessageW(hwnd, WM_APP_POWER_CHECK, 0, 0);
                return TRUE;
            }
            if (wParam == PBT_APMSUSPEND) {
                PostMessageW(hwnd, WM_APP_DISMISS, 0, 0);
                return TRUE;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

DWORD WINAPI OverlayThreadProc(void*) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kWindowClass, L"Charging Glass v2B", WS_POPUP, 0, 0, 440, 440,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd) {
        Wh_Log(L"ChargingGlass: failed to create overlay window.");
        return 0;
    }
    g_hwnd = hwnd;
    g_prevAcStatus = ReadAcLineStatus();

    // --- D2D setup (DC render target + DIB backing store) ---
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> dwriteFactory;
    ComPtr<ID2D1DCRenderTarget> target;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                                 reinterpret_cast<void**>(factory.GetAddressOf())))) {
        Wh_Log(L"ChargingGlass: D2D1CreateFactory failed.");
        DestroyWindow(hwnd);
        return 0;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())))) {
        // Text will not draw, but the glass overlay still works.
        Wh_Log(L"ChargingGlass: DWriteCreateFactory failed; text disabled.");
    }
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
    if (FAILED(factory->CreateDCRenderTarget(&props, &target))) {
        Wh_Log(L"ChargingGlass: CreateDCRenderTarget failed.");
        DestroyWindow(hwnd);
        return 0;
    }

    // One consistent copy per loop iteration: LoadSettings() runs on the
    // Windhawk settings thread, and the renderer must never observe a
    // half-updated struct. Declared ahead of the lambdas below so they all
    // render from the same snapshot; refreshed at the top of each iteration.
    Settings settings = GetSettingsCopy();

    float textScaleCache = -1.0f;
    ComPtr<IDWriteTextFormat> bigFormat;
    ComPtr<IDWriteTextFormat> smallFormat;
    auto ensureTextFormats = [&]() {
        if (!dwriteFactory ||
            (bigFormat && smallFormat && textScaleCache == settings.sizeScale)) {
            return;
        }
        bigFormat.Reset();
        smallFormat.Reset();
        dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        46.0f * settings.sizeScale, L"", &bigFormat);
        dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        16.0f * settings.sizeScale, L"", &smallFormat);
        if (bigFormat) {
            bigFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (smallFormat) {
            smallFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        textScaleCache = settings.sizeScale;
    };

    auto ensureBitmap = [&](int side) -> bool {
        if (g_dib && g_bitmapSide == side) {
            return true;
        }
        if (g_memDc) {
            DeleteDC(g_memDc);
            g_memDc = nullptr;
        }
        if (g_dib) {
            DeleteObject(g_dib);
            g_dib = nullptr;
        }
        HDC screenDc = GetDC(nullptr);
        if (!screenDc) {
            return false;
        }
        g_memDc = CreateCompatibleDC(screenDc);
        ReleaseDC(nullptr, screenDc);
        if (!g_memDc) {
            return false;
        }
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = side;
        bmi.bmiHeader.biHeight = -side;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        g_dib = CreateDIBSection(g_memDc, &bmi, DIB_RGB_COLORS, nullptr, nullptr, 0);
        if (!g_dib) {
            return false;
        }
        SelectObject(g_memDc, g_dib);
        g_bitmapSide = side;
        return true;
    };

    auto positionWindow = [&](int side) {
        POINT center = {0, 0};
        HMONITOR mon = MonitorFromPoint(center, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(mon, &mi)) {
            return;
        }
        const int x = mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - side) / 2;
        const int y = mi.rcMonitor.top + (mi.rcMonitor.bottom - mi.rcMonitor.top - side) / 2;
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, side, side, SWP_NOACTIVATE);
    };

    AnimState anim;

    auto drawFrame = [&](float alpha, float scale, float sweep, int percent) -> bool {
        const int side = static_cast<int>(std::ceil(440.0f * settings.sizeScale));
        if (!ensureBitmap(side)) {
            return false;
        }
        positionWindow(side);
        ensureTextFormats();

        RECT rc = {0, 0, side, side};
        if (FAILED(target->BindDC(g_memDc, &rc))) {
            return false;
        }
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.0f, 0.0f));

        const float S = static_cast<float>(side);
        const D2D1_POINT_2F c = D2D1::Point2F(S * 0.5f, S * 0.5f);
        const float ss = settings.sizeScale;
        const float R = 150.0f * ss * scale;
        const D2D1_COLOR_F glow = settings.glowColor;

        auto withAlpha = [](D2D1_COLOR_F color, float a) {
            color.a = ClampValue(a, 0.0f, 1.0f);
            return color;
        };

        if (alpha > 0.01f) {
            // Ambient glow.
            D2D1_GRADIENT_STOP glowStops[2] = {};
            glowStops[0].position = 0.0f;
            glowStops[0].color = withAlpha(glow, 0.30f * alpha);
            glowStops[1].position = 1.0f;
            glowStops[1].color = withAlpha(glow, 0.0f);
            ComPtr<ID2D1GradientStopCollection> glowCol;
            if (SUCCEEDED(factory->CreateGradientStopCollection(glowStops, 2, D2D1_GAMMA_2_2,
                                                                D2D1_EXTEND_MODE_CLAMP,
                                                                &glowCol)) &&
                glowCol) {
                ComPtr<ID2D1RadialGradientBrush> glowBrush;
                if (SUCCEEDED(target->CreateRadialGradientBrush(
                        D2D1::RadialGradientBrushProperties(c, D2D1::Point2F(0, 0), R * 1.45f,
                                                            R * 1.45f),
                        glowCol.Get(), &glowBrush)) &&
                    glowBrush) {
                    target->FillEllipse(D2D1::Ellipse(c, R * 1.45f, R * 1.45f), glowBrush.Get());
                }
            }

            // Glass disc.
            ComPtr<ID2D1SolidColorBrush> fill;
            if (SUCCEEDED(target->CreateSolidColorBrush(
                    withAlpha(D2D1::ColorF(0.078f, 0.078f, 0.094f, 1.0f), 0.55f * alpha),
                    &fill)) &&
                fill) {
                target->FillEllipse(D2D1::Ellipse(c, R, R), fill.Get());
            }

            // Specular rim.
            D2D1_GRADIENT_STOP rimStops[2] = {};
            rimStops[0].position = 0.0f;
            rimStops[0].color = withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.50f * alpha);
            rimStops[1].position = 1.0f;
            rimStops[1].color = withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.07f * alpha);
            ComPtr<ID2D1GradientStopCollection> rimCol;
            if (SUCCEEDED(factory->CreateGradientStopCollection(rimStops, 2, D2D1_GAMMA_2_2,
                                                                D2D1_EXTEND_MODE_CLAMP, &rimCol)) &&
                rimCol) {
                ComPtr<ID2D1LinearGradientBrush> rim;
                if (SUCCEEDED(target->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties(D2D1::Point2F(c.x, c.y - R),
                                                            D2D1::Point2F(c.x, c.y + R)),
                        rimCol.Get(), &rim)) &&
                    rim) {
                    target->DrawEllipse(D2D1::Ellipse(c, R - 0.7f, R - 0.7f), rim.Get(), 1.4f);
                }
            }

            // Top sheen, clipped to the disc.
            ComPtr<ID2D1Geometry> disc;
            ComPtr<ID2D1Layer> sheenLayer;
            if (SUCCEEDED(factory->CreateEllipseGeometry(D2D1::Ellipse(c, R, R), &disc)) && disc &&
                SUCCEEDED(target->CreateLayer(&sheenLayer)) && sheenLayer) {
                D2D1_RECT_F discRect = D2D1::RectF(c.x - R, c.y - R, c.x + R, c.y + R);
                target->PushLayer(
                    D2D1::LayerParameters(discRect, disc.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                    sheenLayer.Get());
                D2D1_GRADIENT_STOP sheenStops[3] = {};
                sheenStops[0].position = 0.0f;
                sheenStops[0].color = withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.14f * alpha);
                sheenStops[1].position = 0.5f;
                sheenStops[1].color = withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.03f * alpha);
                sheenStops[2].position = 1.0f;
                sheenStops[2].color = withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.0f);
                ComPtr<ID2D1GradientStopCollection> sheenCol;
                if (SUCCEEDED(factory->CreateGradientStopCollection(
                        sheenStops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sheenCol)) &&
                    sheenCol) {
                    ComPtr<ID2D1LinearGradientBrush> sheen;
                    if (SUCCEEDED(target->CreateLinearGradientBrush(
                            D2D1::LinearGradientBrushProperties(D2D1::Point2F(c.x, c.y - R),
                                                                D2D1::Point2F(c.x, c.y + R)),
                            sheenCol.Get(), &sheen)) &&
                        sheen) {
                        target->FillRectangle(discRect, sheen.Get());
                    }
                }
                target->PopLayer();
            }

            // Ring track + sweep.
            const float ringR = R - 14.0f * ss;
            const float ringW = 9.0f * ss;
            ComPtr<ID2D1SolidColorBrush> track;
            if (SUCCEEDED(target->CreateSolidColorBrush(withAlpha(D2D1::ColorF(1, 1, 1, 1),
                                                                  0.12f * alpha), &track)) &&
                track) {
                target->DrawEllipse(D2D1::Ellipse(c, ringR, ringR), track.Get(), ringW);
            }

            const int shownPercent = ClampValue(percent, -1, 100);
            const float fraction =
                shownPercent >= 0 ? (static_cast<float>(shownPercent) / 100.0f) : 0.85f;
            const float sweepAngle = fraction * sweep * 360.0f;
            if (sweepAngle > 1.0f) {
                ComPtr<ID2D1SolidColorBrush> arc;
                if (SUCCEEDED(target->CreateSolidColorBrush(withAlpha(glow, alpha), &arc)) && arc) {
                    ComPtr<ID2D1PathGeometry> path;
                    ComPtr<ID2D1GeometrySink> sink;
                    if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path &&
                        SUCCEEDED(path->Open(&sink)) && sink) {
                        const float pi = 3.14159265f;
                        const float startRad = -90.0f * pi / 180.0f;
                        const float endRad = (-90.0f + sweepAngle) * pi / 180.0f;
                        sink->BeginFigure(
                            D2D1::Point2F(c.x + ringR * std::cos(startRad),
                                          c.y + ringR * std::sin(startRad)),
                            D2D1_FIGURE_BEGIN_FILLED);
                        sink->AddArc(D2D1::ArcSegment(
                            D2D1::Point2F(c.x + ringR * std::cos(endRad),
                                          c.y + ringR * std::sin(endRad)),
                            D2D1::SizeF(ringR, ringR), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                            sweepAngle > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        if (SUCCEEDED(sink->Close())) {
                            D2D1_STROKE_STYLE_PROPERTIES strokeProps = {};
                            strokeProps.startCap = D2D1_CAP_STYLE_ROUND;
                            strokeProps.endCap = D2D1_CAP_STYLE_ROUND;
                            ComPtr<ID2D1StrokeStyle> strokeStyle;
                            if (SUCCEEDED(factory->CreateStrokeStyle(strokeProps, nullptr, 0,
                                                                     &strokeStyle))) {
                                target->DrawGeometry(path.Get(), arc.Get(), ringW,
                                                     strokeStyle.Get());
                            }
                        }
                    }
                }
            }

            // Battery glyph.
            const float gw = 54.0f * ss;
            const float gh = 26.0f * ss;
            const float gy = c.y - 44.0f * ss;
            const D2D1_RECT_F body = D2D1::RectF(c.x - gw * 0.5f, gy - gh * 0.5f,
                                                 c.x + gw * 0.5f, gy + gh * 0.5f);
            const float rad = 7.0f * ss;
            ComPtr<ID2D1SolidColorBrush> outline;
            if (SUCCEEDED(target->CreateSolidColorBrush(withAlpha(D2D1::ColorF(1, 1, 1, 1),
                                                                  0.9f * alpha), &outline)) &&
                outline) {
                target->DrawRoundedRectangle(D2D1::RoundedRect(body, rad, rad), outline.Get(),
                                             2.4f * ss);
                const D2D1_RECT_F cap = D2D1::RectF(body.right + 2.0f * ss, gy - 5.0f * ss,
                                                    body.right + 7.0f * ss, gy + 5.0f * ss);
                target->FillRoundedRectangle(D2D1::RoundedRect(cap, 2.0f * ss, 2.0f * ss),
                                             outline.Get());
            }
            const float inset = 4.0f * ss;
            if (shownPercent >= 0) {
                const float fillW =
                    (body.right - body.left - inset * 2.0f) * fraction * sweep;
                if (fillW > 1.0f) {
                    ComPtr<ID2D1SolidColorBrush> fillBar;
                    if (SUCCEEDED(target->CreateSolidColorBrush(withAlpha(glow, alpha),
                                                                &fillBar)) &&
                        fillBar) {
                        target->FillRoundedRectangle(
                            D2D1::RoundedRect(
                                D2D1::RectF(body.left + inset, body.top + inset,
                                            body.left + inset + fillW, body.bottom - inset),
                                3.5f * ss, 3.5f * ss),
                            fillBar.Get());
                    }
                }
            } else {
                ComPtr<ID2D1SolidColorBrush> fillBar;
                if (SUCCEEDED(target->CreateSolidColorBrush(withAlpha(glow, 0.5f * alpha),
                                                            &fillBar)) &&
                    fillBar) {
                    target->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(body.left + inset, body.top + inset,
                                                      body.right - inset, body.bottom - inset),
                                          3.5f * ss, 3.5f * ss),
                        fillBar.Get());
                }
            }

            // Percentage figure.
            if (settings.showPercentage && shownPercent >= 0 && bigFormat) {
                wchar_t buf[16] = {};
                wsprintfW(buf, L"%d%%", shownPercent);
                ComPtr<ID2D1SolidColorBrush> text;
                if (SUCCEEDED(target->CreateSolidColorBrush(
                        withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.96f * alpha), &text)) &&
                    text) {
                    IDWriteTextLayout* layout = nullptr;
                    if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                            buf, static_cast<UINT32>(wcslen(buf)), bigFormat.Get(), 400.0f, 80.0f,
                            &layout)) &&
                        layout) {
                        DWRITE_TEXT_METRICS m = {};
                        layout->GetMetrics(&m);
                        target->DrawTextLayout(D2D1::Point2F(c.x - m.width * 0.5f,
                                                             c.y + 4.0f * ss),
                                               layout, text.Get());
                        layout->Release();
                    }
                }
            }

            // Caption.
            if (smallFormat) {
                const wchar_t* caption = L"Charging";
                ComPtr<ID2D1SolidColorBrush> text;
                if (SUCCEEDED(target->CreateSolidColorBrush(
                        withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.60f * alpha), &text)) &&
                    text) {
                    IDWriteTextLayout* layout = nullptr;
                    if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                            caption, static_cast<UINT32>(wcslen(caption)), smallFormat.Get(),
                            400.0f, 40.0f, &layout)) &&
                        layout) {
                        DWRITE_TEXT_METRICS m = {};
                        layout->GetMetrics(&m);
                        const float capY = c.y + (settings.showPercentage && shownPercent >= 0
                                                      ? 68.0f
                                                      : 34.0f) *
                                                     ss;
                        target->DrawTextLayout(D2D1::Point2F(c.x - m.width * 0.5f, capY), layout,
                                               text.Get());
                        layout->Release();
                    }
                }
            }
        }

        if (FAILED(target->EndDraw())) {
            return false;
        }

        POINT src = {0, 0};
        SIZE size = {side, side};
        POINT dst = {};
        RECT winRect = {};
        GetWindowRect(hwnd, &winRect);
        dst.x = winRect.left;
        dst.y = winRect.top;
        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        return UpdateLayeredWindow(hwnd, nullptr, &dst, &size, g_memDc, &src, 0, &blend,
                                   ULW_ALPHA) != FALSE;
    };

    auto hideNow = [&]() {
        if (IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_HIDE);
        }
        anim.phase = Phase::Idle;
    };

    auto startShow = [&](int percent) {
        anim.phase = Phase::Enter;
        anim.phaseStart = NowSeconds();
        anim.batteryPercent = percent;
        anim.unplugged = false;
        anim.holdUntil = 0.0;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        Wh_Log(L"ChargingGlass: AC connected, showing overlay (%d%%).", percent);
    };

    MSG msg;
    bool quit = false;
    while (!quit) {
        settings = GetSettingsCopy();
        DWORD timeout = INFINITE;
        if (anim.phase != Phase::Idle) {
            timeout = 16;
        }
        DWORD wait = MsgWaitForMultipleObjects(1, &g_stopEvent, FALSE, timeout, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) {
            break;
        }

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            if (msg.message == WM_APP_POWER_CHECK) {
                const BYTE ac = ReadAcLineStatus();
                const bool wasPlugged = (g_prevAcStatus == 1);
                const bool isPlugged = (ac == 1);
                if (isPlugged && !wasPlugged && anim.phase == Phase::Idle) {
                    startShow(static_cast<int>(ReadBatteryPercent()));
                } else if (!isPlugged && wasPlugged && anim.phase != Phase::Idle &&
                           anim.phase != Phase::Exit) {
                    anim.phase = Phase::Exit;
                    anim.phaseStart = NowSeconds();
                    anim.unplugged = true;
                    Wh_Log(L"ChargingGlass: AC disconnected, dismissing.");
                }
                g_prevAcStatus = ac;
                continue;
            }
            if (msg.message == WM_APP_DISMISS) {
                hideNow();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) {
            break;
        }

        if (g_settingsDirty.exchange(false)) {
            textScaleCache = -1.0f;  // rebuild text formats with the new scale
        }

        // Advance the animation state machine.
        if (anim.phase != Phase::Idle) {
            const double now = NowSeconds();
            const double elapsed = now - anim.phaseStart;
            const bool reduce = settings.reduceMotion;

            float alpha = 0.0f;
            float scale = 1.0f;
            float sweep = 1.0f;

            if (anim.phase == Phase::Enter) {
                const double dur = reduce ? 0.18 : 0.40;
                const float t = SmoothStep(static_cast<float>(elapsed / dur));
                alpha = t;
                scale = reduce ? 1.0f : (0.92f + 0.08f * t);
                sweep = reduce ? 1.0f : t;
                if (elapsed >= dur) {
                    anim.phase = Phase::Hold;
                    anim.phaseStart = now;
                    anim.holdUntil = now + settings.durationSeconds;
                }
            } else if (anim.phase == Phase::Hold) {
                alpha = 1.0f;
                // Refresh the battery level while we are up; charging moves.
                const int p = static_cast<int>(ReadBatteryPercent());
                if (p != anim.batteryPercent) {
                    anim.batteryPercent = p;
                }
                if (now >= anim.holdUntil) {
                    anim.phase = Phase::Exit;
                    anim.phaseStart = now;
                }
            } else {  // Exit
                const double dur = reduce ? 0.18 : 0.30;
                const float t = SmoothStep(static_cast<float>(elapsed / dur));
                alpha = 1.0f - t;
                scale = reduce ? 1.0f : (1.0f + 0.03f * t);
                if (elapsed >= dur) {
                    hideNow();
                    continue;
                }
            }

            drawFrame(alpha, scale, sweep, anim.batteryPercent);
        }
    }

    hideNow();
    if (g_memDc) {
        DeleteDC(g_memDc);
        g_memDc = nullptr;
    }
    if (g_dib) {
        DeleteObject(g_dib);
        g_dib = nullptr;
    }
    DestroyWindow(hwnd);
    g_hwnd = nullptr;
    return 0;
}

HANDLE g_thread = nullptr;

}  // namespace

BOOL WhTool_ModInit() {
    InitializeCriticalSection(&g_settingsCs);
    LoadSettings();
    Wh_Log(L"Charging Glass v2B initialized.");
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        return FALSE;
    }
    g_thread = CreateThread(nullptr, 0, OverlayThreadProc, nullptr, 0, nullptr);
    if (!g_thread) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        return FALSE;
    }
    return TRUE;
}

void WhTool_ModSettingsChanged() {
    LoadSettings();
}

void WhTool_ModUninit() {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    HWND hwnd = g_hwnd.load();
    if (hwnd) {
        PostMessageW(hwnd, WM_APP_DISMISS, 0, 0);
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, 4000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    DeleteCriticalSection(&g_settingsCs);
    Wh_Log(L"Charging Glass v2B unloaded.");
}
