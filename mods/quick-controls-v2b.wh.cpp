// ==WindhawkMod==
// @id              quick-controls-v2b
// @name            Quick Controls v2B
// @description     An iOS-style Liquid Glass control center: one hotkey (or a hover in the top-right corner) opens a floating glass panel with volume, brightness, quick actions and battery.
// @version         1.0.0
// @author          JCVERSA
// @github          https://github.com/JCVERSA
// @license         MIT
// @compilerOptions -lole32 -loleaut32 -lwbemuuid -ld2d1 -ldwrite -lgdi32 -luser32 -lpowrprof
// @include         windhawk.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*

# Quick Controls v2B

A control center for Windows, designed like the one on your iPhone. Press the
hotkey (default **Win+Alt+C**) — or simply hover the mouse in the top-right
corner of the screen — and a translucent Liquid Glass panel glides down from
the corner with everything you reach for every day:

- 🔊 **Volume** — a big slider you can grab directly (1:1 tracking, instant
  response), with a mute button. The value stays in sync with changes made
  elsewhere while the panel is open.
- ☀️ **Brightness** — the same fluid slider for your internal display
  (hidden automatically on machines without one).
- 🔘 **Quick actions** — lock the session, sleep, turn the display off, and
  toggle dark/light mode.
- 🔋 **Battery header** — current level and charging state, like iOS.

Click anywhere outside the panel, press **Esc**, or press the hotkey again to
dismiss it. The panel animates with an interruptible materialize effect and
honors the *Reduce motion* setting.

Part of the community **v2B** series by [JCVERSA](https://github.com/JCVERSA),
built with the same rendering engine as Dynamic Island for Windows v2B and
Charging Glass v2B (Direct2D, per-pixel-alpha layered window, zero CPU while
hidden).

---

## Settings

- **Hotkey** — keys joined with `+`, e.g. `Win+Alt+C`, `Ctrl+Shift+F9`.
  Supported modifiers: `Win`, `Ctrl`, `Alt`, `Shift`. Supported keys: `A`–`Z`,
  `0`–`9`, `F1`–`F24`, `Space`, `Enter`, `Tab`, `Up`, `Down`, `Left`, `Right`.
  Falls back to `Win+Alt+C` when the string cannot be parsed.
- **Top-right corner trigger** — hover the mouse in the extreme top-right
  corner for a third of a second to open the panel (ignored while a
  fullscreen app is focused).
- **Modules** — show/hide the volume tile, brightness tile, quick action
  buttons, or the battery header.
- **Size scale** — overall panel size, 50–200%.
- **Reduce motion** — replaces the entrance animation with a simple fade.

## Version history

### 1.0.0

- Initial release: volume + mute, brightness, lock/sleep/display-off/dark-mode
  buttons, battery header, hotkey + hot-corner triggers, Liquid Glass panel.

*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- Hotkey: Win+Alt+C
  $name: Hotkey
  $description: >-
    Hotkey that toggles the panel. Modifiers: Win, Ctrl, Alt, Shift. Keys:
    letters, digits, F1-F24, Space, Enter, Tab, arrows.
- EnableTopRightCorner: 1
  $name: Top-right corner trigger
  $description: >-
    Open the panel by hovering the extreme top-right corner of the screen for
    a third of a second.
- ShowVolume: 1
  $name: Show volume slider
  $description: Master volume slider with a mute button.
- ShowBrightness: 1
  $name: Show brightness slider
  $description: Internal display brightness (auto-hidden when unavailable).
- ShowQuickButtons: 1
  $name: Show quick action buttons
  $description: Lock, sleep, display off, and dark/light mode buttons.
- ShowBatteryHeader: 1
  $name: Show battery header
  $description: Clock, battery level, and charging state at the top of the panel.
- SizeScale: 100
  $name: Size scale
  $description: Panel size in percent (50-200).
- ReduceMotion: 0
  $name: Reduce motion
  $description: Replace the entrance animation with a simple fade.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wbemidl.h>
#include <powrprof.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWindowClass[] = L"Windhawk.QuickControlsV2B";
constexpr UINT WM_APP_HIDE = WM_APP + 1;
constexpr UINT WM_APP_SETTINGS_APPLIED = WM_APP + 2;
constexpr UINT WM_APP_BRIGHTNESS_READ = WM_APP + 3;
constexpr int kHotkeyId = 0xB2C2;

double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <typename T>
T ClampValue(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float SmoothStep(float t) {
    t = ClampValue(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct Settings {
    std::wstring hotkey = L"Win+Alt+C";
    bool cornerEnabled = true;
    bool showVolume = true;
    bool showBrightness = true;
    bool showQuickButtons = true;
    bool showBatteryHeader = true;
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

std::wstring GetStringSettingCopy(PCWSTR name) {
    PCWSTR value = Wh_GetStringSetting(name);
    std::wstring result = value ? value : L"";
    if (value) {
        Wh_FreeStringSetting(value);
    }
    return result;
}

void LoadSettings() {
    Settings next;
    next.hotkey = GetStringSettingCopy(L"Hotkey");
    if (next.hotkey.empty()) {
        next.hotkey = L"Win+Alt+C";
    }
    next.cornerEnabled = Wh_GetIntSetting(L"EnableTopRightCorner") != 0;
    next.showVolume = Wh_GetIntSetting(L"ShowVolume") != 0;
    next.showBrightness = Wh_GetIntSetting(L"ShowBrightness") != 0;
    next.showQuickButtons = Wh_GetIntSetting(L"ShowQuickButtons") != 0;
    next.showBatteryHeader = Wh_GetIntSetting(L"ShowBatteryHeader") != 0;
    next.sizeScale = ClampValue(Wh_GetIntSetting(L"SizeScale") / 100.0f, 0.5f, 2.0f);
    next.reduceMotion = Wh_GetIntSetting(L"ReduceMotion") != 0;

    EnterCriticalSection(&g_settingsCs);
    g_settings = next;
    LeaveCriticalSection(&g_settingsCs);
    g_settingsDirty = true;
}

// ---------------------------------------------------------------------------
// Hotkey parsing
// ---------------------------------------------------------------------------

struct ParsedHotkey {
    UINT mods = 0;
    UINT vk = 0;
    bool ok = false;
};

std::wstring ToLowerCopy(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& ch : out) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return out;
}

std::wstring TrimCopy(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t')) --b;
    return s.substr(a, b - a);
}

ParsedHotkey ParseHotkey(const std::wstring& text) {
    ParsedHotkey result;
    size_t start = 0;
    while (start <= text.size()) {
        size_t plus = text.find(L'+', start);
        std::wstring token =
            TrimCopy(text.substr(start, plus == std::wstring::npos ? std::wstring::npos
                                                                   : plus - start));
        start = plus == std::wstring::npos ? text.size() + 1 : plus + 1;
        if (token.empty()) {
            continue;
        }
        std::wstring lower = ToLowerCopy(token);
        if (lower == L"win") {
            result.mods |= MOD_WIN;
        } else if (lower == L"ctrl" || lower == L"control") {
            result.mods |= MOD_CONTROL;
        } else if (lower == L"alt") {
            result.mods |= MOD_ALT;
        } else if (lower == L"shift") {
            result.mods |= MOD_SHIFT;
        } else if (token.size() == 1 &&
                   ((token[0] >= L'A' && token[0] <= L'Z') ||
                    (token[0] >= L'a' && token[0] <= L'z'))) {
            result.vk = static_cast<UINT>(token[0] & ~0x20);
            result.ok = true;
        } else if (token.size() == 1 && token[0] >= L'0' && token[0] <= L'9') {
            result.vk = static_cast<UINT>(token[0]);
            result.ok = true;
        } else if (lower == L"space") {
            result.vk = VK_SPACE;
            result.ok = true;
        } else if (lower == L"enter") {
            result.vk = VK_RETURN;
            result.ok = true;
        } else if (lower == L"tab") {
            result.vk = VK_TAB;
            result.ok = true;
        } else if (lower == L"up") {
            result.vk = VK_UP;
            result.ok = true;
        } else if (lower == L"down") {
            result.vk = VK_DOWN;
            result.ok = true;
        } else if (lower == L"left") {
            result.vk = VK_LEFT;
            result.ok = true;
        } else if (lower == L"right") {
            result.vk = VK_RIGHT;
            result.ok = true;
        } else if (lower.size() >= 2 && lower[0] == L'f') {
            int n = 0;
            bool digits = true;
            for (size_t i = 1; i < lower.size(); ++i) {
                if (lower[i] < L'0' || lower[i] > L'9') {
                    digits = false;
                    break;
                }
                n = n * 10 + (lower[i] - L'0');
            }
            if (digits && n >= 1 && n <= 24) {
                result.vk = static_cast<UINT>(VK_F1 + n - 1);
                result.ok = true;
            }
        }
    }
    if (result.ok) {
        result.mods |= MOD_NOREPEAT;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Audio (CoreAudio) — same GUID convention as other Windhawk volume mods.
// ---------------------------------------------------------------------------

const static GUID XIID_IMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const static GUID XIID_MMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const static GUID XIID_IAudioEndpointVolume = {
    0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};

struct AudioModule {
    IMMDeviceEnumerator* enumerator = nullptr;

    bool Ensure() {
        if (enumerator) {
            return true;
        }
        HRESULT hr = CoCreateInstance(XIID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
                                      XIID_IMMDeviceEnumerator,
                                      reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) {
            enumerator = nullptr;
            return false;
        }
        return true;
    }

    // Returns false when no default render device exists.
    bool ReadState(float* volume, bool* muted) {
        if (!Ensure()) {
            return false;
        }
        IMMDevice* device = nullptr;
        HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr) || !device) {
            return false;
        }
        bool ok = false;
        IAudioEndpointVolume* endpoint = nullptr;
        hr = device->Activate(XIID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, nullptr,
                              reinterpret_cast<void**>(&endpoint));
        if (SUCCEEDED(hr) && endpoint) {
            float v = 0.0f;
            BOOL m = FALSE;
            if (SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&v)) &&
                SUCCEEDED(endpoint->GetMute(&m))) {
                *volume = ClampValue(v, 0.0f, 1.0f);
                *muted = (m != FALSE);
                ok = true;
            }
            endpoint->Release();
        }
        device->Release();
        return ok;
    }

    bool SetVolume(float v) {
        if (!Ensure()) {
            return false;
        }
        IMMDevice* device = nullptr;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || !device) {
            return false;
        }
        bool ok = false;
        IAudioEndpointVolume* endpoint = nullptr;
        if (SUCCEEDED(device->Activate(XIID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, nullptr,
                                       reinterpret_cast<void**>(&endpoint))) &&
            endpoint) {
            ok = SUCCEEDED(endpoint->SetMasterVolumeLevelScalar(ClampValue(v, 0.0f, 1.0f),
                                                                nullptr));
            endpoint->Release();
        }
        device->Release();
        return ok;
    }

    bool ToggleMute() {
        if (!Ensure()) {
            return false;
        }
        IMMDevice* device = nullptr;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || !device) {
            return false;
        }
        bool ok = false;
        IAudioEndpointVolume* endpoint = nullptr;
        if (SUCCEEDED(device->Activate(XIID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, nullptr,
                                       reinterpret_cast<void**>(&endpoint))) &&
            endpoint) {
            BOOL m = FALSE;
            if (SUCCEEDED(endpoint->GetMute(&m))) {
                ok = SUCCEEDED(endpoint->SetMute(m ? FALSE : TRUE, nullptr));
            }
            endpoint->Release();
        }
        device->Release();
        return ok;
    }

    void Shutdown() {
        if (enumerator) {
            enumerator->Release();
            enumerator = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Brightness (WMI) — proven recipe used by existing brightness mods.
// ---------------------------------------------------------------------------

struct BrightnessModule {
    bool initialized = false;
    bool supported = false;
    int level = 50;
    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;

    void Init() {
        if (initialized) {
            return;
        }
        initialized = true;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IWbemLocator, reinterpret_cast<void**>(&locator)))) {
            locator = nullptr;
            return;
        }
        BSTR ns = SysAllocString(L"ROOT\\WMI");
        if (ns && SUCCEEDED(locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr,
                                                   nullptr, &services))) {
            CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                              RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                              EOAC_NONE);
        }
        if (ns) {
            SysFreeString(ns);
        }
        if (!services) {
            return;
        }
        // Probe: if there is no WmiMonitorBrightness instance, this machine has
        // no internal panel that supports WMI brightness.
        BSTR className = SysAllocString(L"WmiMonitorBrightness");
        IEnumWbemClassObject* enumerator = nullptr;
        if (className && SUCCEEDED(services->CreateInstanceEnum(className, 0, nullptr,
                                                                &enumerator))) {
            IWbemClassObject* instance = nullptr;
            ULONG got = 0;
            if (SUCCEEDED(enumerator->Next(2000, 1, &instance, &got)) && got > 0 && instance) {
                supported = true;
                instance->Release();
            }
            enumerator->Release();
        }
        if (className) {
            SysFreeString(className);
        }
        if (supported) {
            level = Get();
        }
    }

    int Get() {
        if (!services) {
            return level;
        }
        int result = level;
        BSTR className = SysAllocString(L"WmiMonitorBrightness");
        IEnumWbemClassObject* enumerator = nullptr;
        if (className && SUCCEEDED(services->CreateInstanceEnum(className, 0, nullptr,
                                                                &enumerator))) {
            IWbemClassObject* instance = nullptr;
            ULONG got = 0;
            if (SUCCEEDED(enumerator->Next(WBEM_INFINITE, 1, &instance, &got)) && got > 0 &&
                instance) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(instance->Get(L"CurrentBrightness", 0, &value, nullptr,
                                            nullptr))) {
                    if (value.vt == VT_UI1) {
                        result = value.bVal;
                    } else if (value.vt == VT_I4) {
                        result = value.lVal;
                    }
                    VariantClear(&value);
                }
                instance->Release();
            }
            enumerator->Release();
        }
        if (className) {
            SysFreeString(className);
        }
        level = result;
        return result;
    }

    void Set(int percent) {
        if (!services) {
            return;
        }
        percent = ClampValue(percent, 0, 100);
        level = percent;
        BSTR className = SysAllocString(L"WmiMonitorBrightnessMethods");
        BSTR methodName = SysAllocString(L"WmiSetBrightness");
        IEnumWbemClassObject* enumerator = nullptr;
        if (!className || !methodName ||
            FAILED(services->CreateInstanceEnum(className, 0, nullptr, &enumerator))) {
            if (className) SysFreeString(className);
            if (methodName) SysFreeString(methodName);
            return;
        }
        IWbemClassObject* instance = nullptr;
        ULONG got = 0;
        if (SUCCEEDED(enumerator->Next(WBEM_INFINITE, 1, &instance, &got)) && got > 0 &&
            instance) {
            VARIANT path;
            VariantInit(&path);
            if (SUCCEEDED(instance->Get(L"__PATH", 0, &path, nullptr, nullptr))) {
                IWbemClassObject* cls = nullptr;
                if (SUCCEEDED(services->GetObject(className, 0, nullptr, &cls, nullptr)) && cls) {
                    IWbemClassObject* inDef = nullptr;
                    if (SUCCEEDED(cls->GetMethod(methodName, 0, &inDef, nullptr)) && inDef) {
                        IWbemClassObject* inInst = nullptr;
                        if (SUCCEEDED(inDef->SpawnInstance(0, &inInst)) && inInst) {
                            VARIANT timeout;
                            timeout.vt = VT_I4;
                            timeout.lVal = 0;
                            inInst->Put(L"Timeout", 0, &timeout, 0);
                            VARIANT brightness;
                            brightness.vt = VT_I4;
                            brightness.lVal = percent;
                            inInst->Put(L"Brightness", 0, &brightness, 0);
                            services->ExecMethod(path.bstrVal, methodName, 0, nullptr, inInst,
                                                 nullptr, nullptr);
                            inInst->Release();
                        }
                        inDef->Release();
                    }
                    cls->Release();
                }
                VariantClear(&path);
            }
            instance->Release();
        }
        enumerator->Release();
        SysFreeString(className);
        SysFreeString(methodName);
    }

    void Shutdown() {
        if (services) {
            services->Release();
            services = nullptr;
        }
        if (locator) {
            locator->Release();
            locator = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Quick actions
// ---------------------------------------------------------------------------

const wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

bool ReadDarkMode() {
    HKEY key = nullptr;
    bool dark = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD value = 1, size = sizeof(value);
        if (RegQueryValueExW(key, L"AppsUseLight", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS) {
            dark = (value == 0);
        }
        RegCloseKey(key);
    }
    return dark;
}

void SetDarkMode(bool dark) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, KEY_SET_VALUE, &key) ==
        ERROR_SUCCESS) {
        DWORD value = dark ? 0 : 1;
        RegSetValueExW(key, L"AppsUseLight", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                       sizeof(value));
        RegSetValueExW(key, L"SystemUsesLightTheme", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
    DWORD_PTR sent = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"ImmersiveColorSet"), SMTO_NORMAL, 1000, &sent);
}

// ---------------------------------------------------------------------------
// Panel state
// ---------------------------------------------------------------------------

enum class Phase { Idle, Enter, Visible, Exit };

enum class Hit {
    None = 0,
    VolSlider,
    VolMute,
    BrightSlider,
    BtnLock,
    BtnSleep,
    BtnDisplay,
    BtnTheme,
};
constexpr int kHitCount = 8;

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px <= x + w && py <= y + h;
    }
};

struct Layout {
    float ss = 1.0f;
    int canvasW = 0, canvasH = 0;
    RectF panel;
    RectF volTile, volTrack, volIconBox;
    RectF briTile, briTrack, briIconBox;
    RectF btnLock, btnSleep, btnDisplay, btnTheme;
    float btnRadius = 0;
    bool showVol = false, showBri = false, showBtns = false, showHeader = false;
};

// Geometry constants at scale 1 (canvas-local coordinates).
constexpr float kPanelW = 352.0f;
constexpr float kMargin = 40.0f;      // canvas margin for glow/shadow
constexpr float kTopPad = 22.0f;
constexpr float kSidePad = 22.0f;
constexpr float kHeaderH = 30.0f;
constexpr float kTileH = 64.0f;
constexpr float kTileGap = 10.0f;
constexpr float kButtonH = 58.0f;

struct UiState {
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    Hit hover = Hit::None;
    Hit pressed = Hit::None;
    Hit dragging = Hit::None;
    bool volAvailable = false;
    bool briAvailable = false;
    float volume = 0.0f;     // real value
    bool muted = false;
    int brightness = 50;     // real value
    float volVis = 0.0f;     // smoothed, rendered
    float briVis = 0.5f;
    int briPending = -1;     // waiting for throttled WMI write
    double lastBriSet = 0.0;
    float glow[kHitCount] = {};      // hover/press glow per region
    float pressAnim[kHitCount] = {}; // button press scale animation
    bool darkMode = false;
};

HWND g_hwnd = nullptr;
HANDLE g_stopEvent = nullptr;
HANDLE g_thread = nullptr;
HHOOK g_escHook = nullptr;
std::atomic<bool> g_panelShowing{false};

// Backing bitmap (raw GDI handles, like the other v2B overlay mods).
HBITMAP g_dib = nullptr;
HDC g_memDc = nullptr;
int g_bitmapW = 0;
int g_bitmapH = 0;

float GetDpiScale() {
    HDC dc = GetDC(nullptr);
    if (!dc) {
        return 1.0f;
    }
    float dpi = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, dc);
    return ClampValue(dpi, 1.0f, 4.0f);
}

// ---------------------------------------------------------------------------
// Layout computation (pure function of settings + availability)
// ---------------------------------------------------------------------------

Layout ComputeLayout(const Settings& settings, const UiState& ui, float ss) {
    Layout out;
    out.ss = ss;
    out.showHeader = settings.showBatteryHeader;
    out.showVol = settings.showVolume && ui.volAvailable;
    out.showBri = settings.showBrightness && ui.briAvailable;
    out.showBtns = settings.showQuickButtons;

    float w = kPanelW * ss;
    float h = kTopPad * ss;
    if (out.showHeader) {
        h += kHeaderH * ss;
    }
    bool hasTiles = out.showVol || out.showBri;
    if (hasTiles || out.showBtns) {
        h += 14.0f * ss;
    }
    if (out.showVol) {
        h += kTileH * ss;
    }
    if (out.showBri) {
        h += (out.showVol ? kTileGap : 0.0f) * ss + kTileH * ss;
    }
    if (out.showBtns) {
        h += (hasTiles ? 16.0f : 0.0f) * ss + kButtonH * ss;
    }
    h += kSidePad * ss;  // bottom padding

    out.canvasW = static_cast<int>(std::ceil(w + kMargin * 2.0f * ss));
    out.canvasH = static_cast<int>(std::ceil(h + kMargin * 2.0f * ss));

    const float px = kMargin * ss;
    const float py = kMargin * ss;
    out.panel = {px, py, w, h};

    float y = py + kTopPad * ss;
    if (out.showHeader) {
        y += kHeaderH * ss;
    }
    if (hasTiles || out.showBtns) {
        y += 14.0f * ss;
    }

    const float iconBoxW = 46.0f * ss;
    const float trackX0 = px + kSidePad * ss + iconBoxW;
    const float trackX1 = px + w - kSidePad * ss;
    const float trackH = 10.0f * ss;

    if (out.showVol) {
        out.volTile = {px + kSidePad * ss, y, w - 2.0f * kSidePad * ss, kTileH * ss};
        out.volIconBox = {out.volTile.x + 14.0f * ss, y + (kTileH * ss - 36.0f * ss) * 0.5f,
                          36.0f * ss, 36.0f * ss};
        out.volTrack = {trackX0, y + (kTileH * ss - trackH) * 0.5f, trackX1 - trackX0, trackH};
        y += kTileH * ss + kTileGap * ss;
    }
    if (out.showBri) {
        if (!out.showVol) {
            y -= kTileGap * ss;  // no leading gap for a single tile
        }
        out.briTile = {px + kSidePad * ss, y, w - 2.0f * kSidePad * ss, kTileH * ss};
        out.briIconBox = {out.briTile.x + 14.0f * ss, y + (kTileH * ss - 36.0f * ss) * 0.5f,
                          36.0f * ss, 36.0f * ss};
        out.briTrack = {trackX0, y + (kTileH * ss - trackH) * 0.5f, trackX1 - trackX0, trackH};
        y += kTileH * ss;
    }
    if (out.showBtns) {
        y += (hasTiles ? 16.0f : 0.0f) * ss;
        const float r = 26.0f * ss;
        out.btnRadius = r;
        const float rowX0 = px + kSidePad * ss;
        const float rowX1 = px + w - kSidePad * ss;
        const float cy = y + kButtonH * ss * 0.5f;
        const float step = (rowX1 - rowX0) / 4.0f;
        auto box = [&](float cx) { return RectF{cx - r, cy - r, r * 2.0f, r * 2.0f}; };
        out.btnLock = box(rowX0 + step * 0.5f);
        out.btnSleep = box(rowX0 + step * 1.5f);
        out.btnDisplay = box(rowX0 + step * 2.5f);
        out.btnTheme = box(rowX0 + step * 3.5f);
    }
    return out;
}

Hit HitTest(const Layout& layout, float x, float y) {
    if (layout.showVol) {
        if (layout.volIconBox.Contains(x, y)) return Hit::VolMute;
        // Generous track area: full tile height over the track column.
        RectF trackArea = {layout.volTrack.x - 8.0f * layout.ss, layout.volTile.y,
                           layout.volTrack.w + 16.0f * layout.ss, layout.volTile.h};
        if (trackArea.Contains(x, y)) return Hit::VolSlider;
    }
    if (layout.showBri) {
        RectF trackArea = {layout.briTrack.x - 8.0f * layout.ss, layout.briTile.y,
                           layout.briTrack.w + 16.0f * layout.ss, layout.briTile.h};
        if (trackArea.Contains(x, y)) return Hit::BrightSlider;
    }
    if (layout.showBtns) {
        if (layout.btnLock.Contains(x, y)) return Hit::BtnLock;
        if (layout.btnSleep.Contains(x, y)) return Hit::BtnSleep;
        if (layout.btnDisplay.Contains(x, y)) return Hit::BtnDisplay;
        if (layout.btnTheme.Contains(x, y)) return Hit::BtnTheme;
    }
    return Hit::None;
}

bool IsInteractive(Hit h) {
    return h != Hit::None;
}

// ---------------------------------------------------------------------------
// Esc hook while the panel is open
// ---------------------------------------------------------------------------

LRESULT CALLBACK EscHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == WM_KEYDOWN && g_hwnd) {
        KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (kb && kb->vkCode == VK_ESCAPE) {
            PostMessageW(g_hwnd, WM_APP_HIDE, 0, 0);
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool IsForegroundFullscreen(HMONITOR mon) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == GetShellWindow() || fg == GetDesktopWindow()) {
        return false;
    }
    MONITORINFO mi = {sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(mon, &mi)) {
        return false;
    }
    RECT r = {};
    if (!GetWindowRect(fg, &r)) {
        return false;
    }
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

// ---------------------------------------------------------------------------
// Vector icons (drawn with Direct2D primitives, DPI-crisp at any scale)
// ---------------------------------------------------------------------------

void DrawSpeakerIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                     float s, bool muted) {
    // Speaker body: rectangle + cone.
    ID2D1PathGeometry* path = nullptr;
    ID2D1Factory* factory = nullptr;
    target->GetFactory(&factory);
    if (!factory || FAILED(factory->CreatePathGeometry(&path)) || !path) {
        if (path) path->Release();
        return;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(path->Open(&sink)) && sink) {
        const float u = s;
        sink->BeginFigure(D2D1::Point2F(cx - 0.50f * u, cy - 0.16f * u), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(cx - 0.18f * u, cy - 0.16f * u));
        sink->AddLine(D2D1::Point2F(cx + 0.12f * u, cy - 0.44f * u));
        sink->AddLine(D2D1::Point2F(cx + 0.12f * u, cy + 0.44f * u));
        sink->AddLine(D2D1::Point2F(cx - 0.18f * u, cy + 0.16f * u));
        sink->AddLine(D2D1::Point2F(cx - 0.50f * u, cy + 0.16f * u));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        target->FillGeometry(path, brush);
    }
    path->Release();

    if (muted) {
        const float u = s;
        target->DrawLine(D2D1::Point2F(cx + 0.28f * u, cy - 0.16f * u),
                         D2D1::Point2F(cx + 0.56f * u, cy + 0.16f * u), brush, 0.10f * u);
        target->DrawLine(D2D1::Point2F(cx + 0.56f * u, cy - 0.16f * u),
                         D2D1::Point2F(cx + 0.28f * u, cy + 0.16f * u), brush, 0.10f * u);
    } else {
        // Two sound waves.
        ID2D1Factory* f2 = nullptr;
        target->GetFactory(&f2);
        ID2D1PathGeometry* waves = nullptr;
        if (f2 && SUCCEEDED(f2->CreatePathGeometry(&waves)) && waves) {
            ID2D1GeometrySink* ws = nullptr;
            if (SUCCEEDED(waves->Open(&ws)) && ws) {
                const float u = s;
                ws->BeginFigure(D2D1::Point2F(cx + 0.26f * u, cy - 0.14f * u),
                                D2D1_FIGURE_BEGIN_HOLLOW);
                ws->AddArc(D2D1::ArcSegment(
                    D2D1::Point2F(cx + 0.26f * u, cy + 0.14f * u),
                    D2D1::SizeF(0.22f * u, 0.22f * u), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                    D2D1_ARC_SIZE_SMALL));
                ws->EndFigure(D2D1_FIGURE_END_OPEN);
                ws->BeginFigure(D2D1::Point2F(cx + 0.36f * u, cy - 0.30f * u),
                                D2D1_FIGURE_BEGIN_HOLLOW);
                ws->AddArc(D2D1::ArcSegment(
                    D2D1::Point2F(cx + 0.36f * u, cy + 0.30f * u),
                    D2D1::SizeF(0.42f * u, 0.42f * u), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                    D2D1_ARC_SIZE_SMALL));
                ws->EndFigure(D2D1_FIGURE_END_OPEN);
                ws->Close();
                ws->Release();
                target->DrawGeometry(waves, brush, 0.09f * u);
            }
            waves->Release();
        }
    }
}

void DrawSunIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                 float s) {
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 0.22f * s, 0.22f * s), brush,
                        0.10f * s);
    ID2D1Factory* factory = nullptr;
    target->GetFactory(&factory);
    ID2D1PathGeometry* rays = nullptr;
    if (!factory || FAILED(factory->CreatePathGeometry(&rays)) || !rays) {
        if (rays) rays->Release();
        return;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(rays->Open(&sink)) && sink) {
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 3.14159265f * 0.25f;
            const float ca = std::cos(a), sa = std::sin(a);
            sink->BeginFigure(D2D1::Point2F(cx + ca * 0.36f * s, cy + sa * 0.36f * s),
                              D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(cx + ca * 0.52f * s, cy + sa * 0.52f * s));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        }
        sink->Close();
        sink->Release();
        target->DrawGeometry(rays, brush, 0.10f * s);
    }
    rays->Release();
}

void DrawLockIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                  float s) {
    const float bodyW = 0.62f * s;
    const float bodyH = 0.46f * s;
    const float bodyY = cy - 0.06f * s;
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(cx - bodyW * 0.5f, bodyY, cx + bodyW * 0.5f,
                                      bodyY + bodyH),
                          0.10f * s, 0.10f * s),
        brush);
    ID2D1Factory* factory = nullptr;
    target->GetFactory(&factory);
    ID2D1PathGeometry* shackle = nullptr;
    if (!factory || FAILED(factory->CreatePathGeometry(&shackle)) || !shackle) {
        if (shackle) shackle->Release();
        return;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(shackle->Open(&sink)) && sink) {
        const float shR = 0.20f * s;
        sink->BeginFigure(D2D1::Point2F(cx - shR, bodyY + 0.02f * s), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(cx - shR, bodyY - 0.10f * s));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + shR, bodyY - 0.10f * s),
                                      D2D1::SizeF(shR, shR), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                      D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(cx + shR, bodyY + 0.02f * s));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        target->DrawGeometry(shackle, brush, 0.10f * s);
    }
    shackle->Release();
}

// Crescent moon via geometry combination (circle minus offset circle).
void DrawMoonIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                  float s) {
    ID2D1Factory* factory = nullptr;
    target->GetFactory(&factory);
    if (!factory) {
        return;
    }
    ComPtr<ID2D1EllipseGeometry> bigCircle;
    ComPtr<ID2D1EllipseGeometry> cutCircle;
    ComPtr<ID2D1PathGeometry> moon;
    const D2D1_ELLIPSE big = D2D1::Ellipse(D2D1::Point2F(cx, cy), 0.42f * s, 0.42f * s);
    const D2D1_ELLIPSE cut =
        D2D1::Ellipse(D2D1::Point2F(cx + 0.28f * s, cy - 0.18f * s), 0.34f * s, 0.34f * s);
    if (FAILED(factory->CreateEllipseGeometry(&big, &bigCircle)) ||
        FAILED(factory->CreateEllipseGeometry(&cut, &cutCircle)) ||
        FAILED(factory->CreatePathGeometry(&moon))) {
        return;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(moon->Open(&sink)) && sink) {
        if (SUCCEEDED(bigCircle->CombineWithGeometry(cutCircle.Get(), D2D1_COMBINE_MODE_EXCLUDE,
                                                     nullptr, sink))) {
            sink->Close();
            sink->Release();
            target->FillGeometry(moon.Get(), brush);
        } else {
            sink->Close();
            sink->Release();
        }
    }
}

void DrawMonitorIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                     float s) {
    target->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(cx - 0.50f * s, cy - 0.34f * s, cx + 0.50f * s,
                                      cy + 0.22f * s),
                          0.08f * s, 0.08f * s),
        brush, 0.10f * s);
    target->DrawLine(D2D1::Point2F(cx, cy + 0.22f * s), D2D1::Point2F(cx, cy + 0.42f * s), brush,
                     0.10f * s);
    target->DrawLine(D2D1::Point2F(cx - 0.22f * s, cy + 0.44f * s),
                     D2D1::Point2F(cx + 0.22f * s, cy + 0.44f * s), brush, 0.10f * s);
}

// Theme button: circle split in half; the left half is filled when dark mode
// is active.
void DrawThemeIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                   float s, bool dark) {
    const float r = 0.42f * s;
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush, 0.10f * s);
    if (dark) {
        ID2D1Factory* factory = nullptr;
        target->GetFactory(&factory);
        ID2D1PathGeometry* half = nullptr;
        if (!factory || FAILED(factory->CreatePathGeometry(&half)) || !half) {
            if (half) half->Release();
            return;
        }
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(half->Open(&sink)) && sink) {
            sink->BeginFigure(D2D1::Point2F(cx, cy - r + 0.06f * s), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx, cy + r - 0.06f * s),
                                          D2D1::SizeF(r - 0.06f * s, r - 0.06f * s), 0.0f,
                                          D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                          D2D1_ARC_SIZE_LARGE));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->FillGeometry(half, brush);
        }
        half->Release();
    }
}

void DrawBatteryGlyph(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                      float s, int percent, bool charging) {
    const float gw = 26.0f * s;
    const float gh = 12.5f * s;
    const D2D1_RECT_F body = D2D1::RectF(cx - gw * 0.5f, cy - gh * 0.5f, cx + gw * 0.5f,
                                         cy + gh * 0.5f);
    target->DrawRoundedRectangle(D2D1::RoundedRect(body, 3.4f * s, 3.4f * s), brush, 1.6f * s);
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(body.right + 2.2f * s, cy - 3.0f * s,
                                      body.right + 4.6f * s, cy + 3.0f * s),
                          1.2f * s, 1.2f * s),
        brush);
    if (percent >= 0 && percent <= 100) {
        const float inset = 2.6f * s;
        const float fillW = (body.right - body.left - inset * 2.0f) * (percent / 100.0f);
        if (fillW > 1.0f) {
            target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(body.left + inset, body.top + inset,
                                              body.left + inset + fillW, body.bottom - inset),
                                  1.8f * s, 1.8f * s),
                brush);
        }
    }
    if (charging) {
        // Small bolt above the glyph.
        ID2D1Factory* factory = nullptr;
        target->GetFactory(&factory);
        ID2D1PathGeometry* bolt = nullptr;
        if (!factory || FAILED(factory->CreatePathGeometry(&bolt)) || !bolt) {
            if (bolt) bolt->Release();
            return;
        }
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(bolt->Open(&sink)) && sink) {
            const float bx = cx, by = cy - 9.5f * s, u = 4.0f * s;
            sink->BeginFigure(D2D1::Point2F(bx + 0.10f * u, by - 0.9f * u),
                              D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(bx - 0.45f * u, by + 0.15f * u));
            sink->AddLine(D2D1::Point2F(bx - 0.02f * u, by + 0.15f * u));
            sink->AddLine(D2D1::Point2F(bx - 0.10f * u, by + 0.9f * u));
            sink->AddLine(D2D1::Point2F(bx + 0.45f * u, by - 0.15f * u));
            sink->AddLine(D2D1::Point2F(bx + 0.02f * u, by - 0.15f * u));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->FillGeometry(bolt, brush);
        }
        bolt->Release();
    }
}

// ---------------------------------------------------------------------------
// Window procedure (input)
// ---------------------------------------------------------------------------

// Set by the overlay thread before the message loop runs.
AudioModule* g_audio = nullptr;
BrightnessModule* g_brightness = nullptr;
UiState* g_ui = nullptr;

// Panel anchor (screen coords) so resizes keep the panel glued to the corner.
int g_panelRightEdge = 0;
int g_panelTop = 0;

float SliderValueFromX(const RectF& track, float x) {
    if (track.w <= 0.0f) {
        return 0.0f;
    }
    return ClampValue((x - track.x) / track.w, 0.0f, 1.0f);
}

// Current layout for hit testing: recomputed cheaply on demand.
Layout g_lastLayout;

void ApplySliderDrag(Hit which, float x) {
    if (!g_ui || !g_audio || !g_brightness) {
        return;
    }
    if (which == Hit::VolSlider) {
        const float v = SliderValueFromX(g_lastLayout.volTrack, x);
        g_ui->volume = v;
        g_ui->volVis = v;
        if (v > 0.0f && g_ui->muted) {
            g_ui->muted = false;
        }
        g_audio->SetVolume(v);
    } else if (which == Hit::BrightSlider) {
        const float v = SliderValueFromX(g_lastLayout.briTrack, x);
        g_ui->briVis = v;
        g_ui->briPending = static_cast<int>(std::lround(v * 100.0f));
    }
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        if (!g_ui || g_ui->phase == Phase::Idle) {
            return 0;
        }
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        const Hit hit = HitTest(g_lastLayout, x, y);
        if (hit == Hit::None) {
            if (!g_lastLayout.panel.Contains(x, y)) {
                PostMessageW(hwnd, WM_APP_HIDE, 0, 0);
            }
            return 0;
        }
        g_ui->pressed = hit;
        if (hit == Hit::VolSlider || hit == Hit::BrightSlider) {
            g_ui->dragging = hit;
            SetCapture(hwnd);
            ApplySliderDrag(hit, x);  // respond instantly on pointer-down
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_ui || g_ui->phase == Phase::Idle) {
            return 0;
        }
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        if (g_ui->dragging != Hit::None) {
            ApplySliderDrag(g_ui->dragging, x);
        } else {
            g_ui->hover = HitTest(g_lastLayout, x, y);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!g_ui) {
            return 0;
        }
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        const Hit hit = HitTest(g_lastLayout, x, y);
        if (g_ui->dragging != Hit::None) {
            if (g_ui->dragging == Hit::BrightSlider && g_ui->briPending >= 0 && g_brightness) {
                g_brightness->Set(g_ui->briPending);  // final, unthrottled write
                g_ui->brightness = g_ui->briPending;
                g_ui->briPending = -1;
            }
            g_ui->dragging = Hit::None;
            ReleaseCapture();
        } else if (g_ui->pressed != Hit::None && hit == g_ui->pressed) {
            switch (hit) {
            case Hit::VolMute:
                if (g_audio && g_audio->ToggleMute()) {
                    g_ui->muted = !g_ui->muted;
                }
                break;
            case Hit::BtnTheme:
                g_ui->darkMode = !g_ui->darkMode;
                SetDarkMode(g_ui->darkMode);
                break;
            case Hit::BtnLock:
                PostMessageW(hwnd, WM_APP_HIDE, 0, 0);
                LockWorkStation();
                break;
            case Hit::BtnSleep:
                PostMessageW(hwnd, WM_APP_HIDE, 0, 0);
                SetSuspendState(FALSE, FALSE, FALSE);
                break;
            case Hit::BtnDisplay:
                PostMessageW(hwnd, WM_APP_HIDE, 0, 0);
                SendMessageTimeoutW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2, SMTO_NORMAL,
                                    300, nullptr);
                break;
            default:
                break;
            }
        }
        g_ui->pressed = Hit::None;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!g_ui || !g_audio || g_ui->phase == Phase::Idle) {
            return 0;
        }
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        const Hit hit = HitTest(g_lastLayout, static_cast<float>(pt.x), static_cast<float>(pt.y));
        if (hit == Hit::VolSlider || hit == Hit::VolMute ||
            (g_lastLayout.showVol && g_lastLayout.volTile.Contains(static_cast<float>(pt.x),
                                                                   static_cast<float>(pt.y)))) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float v = g_ui->volume + (delta > 0 ? 0.02f : -0.02f);
            v = ClampValue(v, 0.0f, 1.0f);
            g_ui->volume = v;
            g_ui->volVis = v;
            if (v > 0.0f && g_ui->muted) {
                g_ui->muted = false;
            }
            g_audio->SetVolume(v);
        }
        return 0;
    }
    case WM_SETCURSOR: {
        if (g_ui && g_ui->phase != Phase::Idle && IsInteractive(g_ui->hover)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Overlay thread
// ---------------------------------------------------------------------------

DWORD WINAPI OverlayThreadProc(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    Settings settings = GetSettingsCopy();

    AudioModule audio;
    BrightnessModule brightness;
    UiState ui;
    ui.darkMode = ReadDarkMode();
    g_audio = &audio;
    g_brightness = &brightness;
    g_ui = &ui;

    brightness.Init();
    ui.briAvailable = brightness.supported;
    ui.brightness = brightness.level;
    ui.briVis = brightness.level / 100.0f;

    float volRead = 0.0f;
    bool muteRead = false;
    ui.volAvailable = audio.ReadState(&volRead, &muteRead);
    if (ui.volAvailable) {
        ui.volume = volRead;
        ui.volVis = volRead;
        ui.muted = muteRead;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                                    WS_EX_NOACTIVATE,
                                kWindowClass, L"Quick Controls v2B", WS_POPUP, 0, 0, 32, 32,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        Wh_Log(L"QuickControls: CreateWindowEx failed.");
        return 0;
    }
    g_hwnd = hwnd;

    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> dwriteFactory;
    ID2D1DCRenderTarget* target = nullptr;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                                 reinterpret_cast<void**>(factory.GetAddressOf())))) {
        Wh_Log(L"QuickControls: D2D1CreateFactory failed.");
        DestroyWindow(hwnd);
        return 0;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())))) {
        Wh_Log(L"QuickControls: DWriteCreateFactory failed; text disabled.");
    }
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 0.0f, 0.0f,
        D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
    if (FAILED(factory->CreateDCRenderTarget(&props, &target))) {
        Wh_Log(L"QuickControls: CreateDCRenderTarget failed.");
        DestroyWindow(hwnd);
        return 0;
    }

    // Hotkey registration (re-run when settings change).
    bool hotkeyRegistered = false;
    auto registerHotkey = [&]() {
        if (hotkeyRegistered) {
            UnregisterHotKey(hwnd, kHotkeyId);
            hotkeyRegistered = false;
        }
        ParsedHotkey parsed = ParseHotkey(settings.hotkey);
        if (!parsed.ok) {
            parsed = ParseHotkey(L"Win+Alt+C");
            Wh_Log(L"QuickControls: hotkey \"%s\" unparseable, using Win+Alt+C.",
                   settings.hotkey.c_str());
        }
        if (RegisterHotKey(hwnd, kHotkeyId, parsed.mods, parsed.vk)) {
            hotkeyRegistered = true;
        } else {
            Wh_Log(L"QuickControls: RegisterHotKey failed (already taken?).");
        }
    };
    registerHotkey();

    const float dpiScale = GetDpiScale();

    float textScaleCache = -1.0f;
    ComPtr<IDWriteTextFormat> headerFormat;
    ComPtr<IDWriteTextFormat> labelFormat;
    auto ensureTextFormats = [&]() {
        const float ss = settings.sizeScale * dpiScale;
        if (!dwriteFactory ||
            (headerFormat && labelFormat && textScaleCache == ss)) {
            return;
        }
        headerFormat.Reset();
        labelFormat.Reset();
        dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        17.0f * ss, L"", &headerFormat);
        dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        11.0f * ss, L"", &labelFormat);
        if (headerFormat) headerFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (labelFormat) labelFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        textScaleCache = ss;
    };

    auto ensureBitmap = [&](int w, int h) -> bool {
        if (g_dib && g_bitmapW == w && g_bitmapH == h) {
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
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        g_dib = CreateDIBSection(g_memDc, &bmi, DIB_RGB_COLORS, nullptr, nullptr, 0);
        if (!g_dib) {
            return false;
        }
        SelectObject(g_memDc, g_dib);
        g_bitmapW = w;
        g_bitmapH = h;
        return true;
    };

    auto installEscHook = [&]() {
        if (!g_escHook) {
            g_escHook = SetWindowsHookExW(WH_KEYBOARD_LL, EscHookProc, GetModuleHandleW(nullptr),
                                          0);
        }
    };
    auto removeEscHook = [&]() {
        if (g_escHook) {
            UnhookWindowsHookEx(g_escHook);
            g_escHook = nullptr;
        }
    };

    auto hidePanel = [&](bool instant) {
        if (ui.phase == Phase::Idle) {
            return;
        }
        if (instant || settings.reduceMotion) {
            ui.phase = Phase::Idle;
            ShowWindow(hwnd, SW_HIDE);
            g_panelShowing = false;
        } else {
            ui.phase = Phase::Exit;
            ui.phaseStart = NowSeconds();
        }
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        removeEscHook();
        ui.hover = Hit::None;
        ui.pressed = Hit::None;
        ui.dragging = Hit::None;
    };

    auto showPanel = [&]() {
        if (ui.phase != Phase::Idle) {
            return;
        }
        // Anchor the panel on the monitor that contains the cursor.
        POINT cursor = {};
        GetCursorPos(&cursor);
        HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(mon, &mi)) {
            return;
        }
        const float ss = settings.sizeScale * dpiScale;
        const Layout layout = ComputeLayout(settings, ui, ss);
        const int winX = mi.rcWork.right - layout.canvasW - 12;
        const int winY = mi.rcWork.top + 12;
        g_panelRightEdge = winX + layout.canvasW;
        g_panelTop = winY;
        SetWindowPos(hwnd, HWND_TOPMOST, winX, winY, layout.canvasW, layout.canvasH,
                     SWP_NOACTIVATE);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetCapture(hwnd);
        installEscHook();

        // Refresh live state before appearing.
        float v = 0.0f;
        bool m = false;
        if (audio.ReadState(&v, &m)) {
            ui.volAvailable = true;
            ui.volume = v;
            ui.volVis = v;
            ui.muted = m;
        } else {
            ui.volAvailable = false;
        }
        ui.darkMode = ReadDarkMode();
        PostMessageW(hwnd, WM_APP_BRIGHTNESS_READ, 0, 0);

        ui.phase = Phase::Enter;
        ui.phaseStart = NowSeconds();
        g_panelShowing = true;
        Wh_Log(L"QuickControls: panel opened.");
    };

    // Renders one frame. alpha/scale/yOff come from the animation state.
    auto drawFrame = [&](float alpha, float scale, float yOff) -> bool {
        const float ss = settings.sizeScale * dpiScale;
        const Layout layout = ComputeLayout(settings, ui, ss);
        g_lastLayout = layout;
        if (!ensureBitmap(layout.canvasW, layout.canvasH)) {
            return false;
        }
        // Keep the panel glued to its corner when its size changes.
        SetWindowPos(hwnd, nullptr, g_panelRightEdge - layout.canvasW, g_panelTop,
                     layout.canvasW, layout.canvasH, SWP_NOZORDER | SWP_NOACTIVATE);
        ensureTextFormats();

        RECT rc = {0, 0, layout.canvasW, layout.canvasH};
        if (FAILED(target->BindDC(g_memDc, &rc))) {
            return false;
        }
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.0f, 0.0f));

        // Entrance transform: scale about the top-right corner of the panel.
        const float ax = layout.panel.x + layout.panel.w;
        const float ay = layout.panel.y;
        const D2D1_MATRIX_3X2_F xf =
            D2D1::Matrix3x2F::Translation(-ax, -ay) * D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(ax, ay + yOff);
        target->SetTransform(xf);

        auto withAlpha = [](D2D1_COLOR_F color, float a) {
            color.a = ClampValue(a, 0.0f, 1.0f);
            return color;
        };

        // Soft drop shadow: a few stacked translucent rounded rects.
        ComPtr<ID2D1SolidColorBrush> shadowBrush;
        if (SUCCEEDED(target->CreateSolidColorBrush(
                withAlpha(D2D1::ColorF(0.0f, 0.0f, 0.0f), 1.0f), &shadowBrush)) &&
            shadowBrush) {
            for (int i = 5; i >= 1; --i) {
                const float grow = static_cast<float>(i) * 5.0f * ss;
                const float a = alpha * 0.05f * (6 - i) / 5.0f;
                shadowBrush->SetColor(withAlpha(D2D1::ColorF(0.0f, 0.0f, 0.0f), a));
                target->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(layout.panel.x - grow, layout.panel.y - grow * 0.4f +
                                                                4.0f * ss,
                                    layout.panel.x + layout.panel.w + grow,
                                    layout.panel.y + layout.panel.h + grow),
                        26.0f * ss + grow * 0.6f, 26.0f * ss + grow * 0.6f),
                    shadowBrush.Get());
            }
        }

        // Glass panel fill: subtle vertical gradient.
        ComPtr<ID2D1GradientStopCollection> panelStops;
        D2D1_GRADIENT_STOP panelGradient[2] = {
            {0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f)},
            {1.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.05f)},
        };
        ComPtr<ID2D1LinearGradientBrush> panelBrush;
        if (SUCCEEDED(target->CreateGradientStopCollection(
                panelGradient, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &panelStops)) &&
            panelStops &&
            SUCCEEDED(target->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(layout.panel.x, layout.panel.y),
                    D2D1::Point2F(layout.panel.x, layout.panel.y + layout.panel.h)),
                panelStops.Get(), &panelBrush)) &&
            panelBrush) {
            panelBrush->SetOpacity(alpha);
            target->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(layout.panel.x, layout.panel.y,
                                layout.panel.x + layout.panel.w,
                                layout.panel.y + layout.panel.h),
                    26.0f * ss, 26.0f * ss),
                panelBrush.Get());
        }

        // Specular rim: bright top, faint bottom.
        ComPtr<ID2D1GradientStopCollection> rimStops;
        D2D1_GRADIENT_STOP rimGradient[2] = {
            {0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.45f)},
            {1.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f)},
        };
        ComPtr<ID2D1LinearGradientBrush> rimBrush;
        if (SUCCEEDED(target->CreateGradientStopCollection(rimGradient, 2, D2D1_GAMMA_2_2,
                                                           D2D1_EXTEND_MODE_CLAMP, &rimStops)) &&
            rimStops &&
            SUCCEEDED(target->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(layout.panel.x, layout.panel.y),
                    D2D1::Point2F(layout.panel.x, layout.panel.y + layout.panel.h)),
                rimStops.Get(), &rimBrush)) &&
            rimBrush) {
            rimBrush->SetOpacity(alpha);
            target->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(layout.panel.x + 0.75f * ss, layout.panel.y + 0.75f * ss,
                                layout.panel.x + layout.panel.w - 0.75f * ss,
                                layout.panel.y + layout.panel.h - 0.75f * ss),
                    25.5f * ss, 25.5f * ss),
                rimBrush.Get(), 1.5f * ss);
        }

        ComPtr<ID2D1SolidColorBrush> whiteBrush;
        target->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteBrush);
        if (!whiteBrush) {
            target->EndDraw();
            return false;
        }
        const D2D1_COLOR_F accent = D2D1::ColorF(0.039f, 0.518f, 1.0f);  // #0A84FF

        // Header: clock on the left, battery on the right.
        if (layout.showHeader) {
            SYSTEMTIME st = {};
            GetLocalTime(&st);
            wchar_t timeBuf[16] = {};
            wsprintfW(timeBuf, L"%02d:%02d", st.wHour, st.wMinute);
            whiteBrush->SetColor(withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.95f * alpha));
            if (dwriteFactory && headerFormat) {
                IDWriteTextLayout* tl = nullptr;
                if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                        timeBuf, static_cast<UINT32>(wcslen(timeBuf)), headerFormat.Get(), 400.0f,
                        60.0f, &tl)) &&
                    tl) {
                    target->DrawTextLayout(
                        D2D1::Point2F(layout.panel.x + kSidePad * ss,
                                      layout.panel.y + kTopPad * ss + 2.0f * ss),
                        tl, whiteBrush.Get());
                    tl->Release();
                }
            }

            SYSTEM_POWER_STATUS ps = {};
            GetSystemPowerStatus(&ps);
            const int pct = ps.BatteryLifePercent;
            const bool charging = ps.ACLineStatus == 1;
            const bool known = pct != 255;
            const float rightEdge = layout.panel.x + layout.panel.w - kSidePad * ss;
            whiteBrush->SetColor(withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.90f * alpha));
            const float headerCy = layout.panel.y + kTopPad * ss + kHeaderH * ss * 0.5f;
            float glyphCx = rightEdge - 18.0f * ss;
            if (known) {
                wchar_t pctBuf[8] = {};
                wsprintfW(pctBuf, L"%d%%", pct);
                IDWriteTextLayout* tl = nullptr;
                if (dwriteFactory && labelFormat &&
                    SUCCEEDED(dwriteFactory->CreateTextLayout(
                        pctBuf, static_cast<UINT32>(wcslen(pctBuf)), labelFormat.Get(), 100.0f,
                        40.0f, &tl)) &&
                    tl) {
                    DWRITE_TEXT_METRICS m = {};
                    tl->GetMetrics(&m);
                    whiteBrush->SetColor(withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.75f * alpha));
                    target->DrawTextLayout(
                        D2D1::Point2F(rightEdge - m.width - 40.0f * ss, headerCy - m.height * 0.5f - 1.0f * ss),
                        tl, whiteBrush.Get());
                    tl->Release();
                }
            } else {
                glyphCx = rightEdge - 18.0f * ss;
            }
            whiteBrush->SetColor(withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.90f * alpha));
            DrawBatteryGlyph(target, whiteBrush.Get(), glyphCx, headerCy, ss, known ? pct : -1,
                             charging);
        }

        // Tiles (volume / brightness).
        auto drawTile = [&](const RectF& tile, const RectF& track, const RectF& iconBox,
                            float value, bool available, bool isVolume) {
            const float glow = ui.glow[isVolume ? static_cast<int>(Hit::VolSlider)
                                                : static_cast<int>(Hit::BrightSlider)];
            ComPtr<ID2D1SolidColorBrush> tileBrush;
            if (SUCCEEDED(target->CreateSolidColorBrush(
                    withAlpha(D2D1::ColorF(1, 1, 1, 1), (0.07f + 0.03f * glow) * alpha),
                    &tileBrush)) &&
                tileBrush) {
                target->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(tile.x, tile.y, tile.x + tile.w,
                                                  tile.y + tile.h),
                                      18.0f * ss, 18.0f * ss),
                    tileBrush.Get());
            }

            const float iconCx = iconBox.x + iconBox.w * 0.5f;
            const float iconCy = iconBox.y + iconBox.h * 0.5f;
            whiteBrush->SetColor(withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.92f * alpha));
            if (isVolume) {
                DrawSpeakerIcon(target, whiteBrush.Get(), iconCx, iconCy, 30.0f * ss, ui.muted);
            } else {
                DrawSunIcon(target, whiteBrush.Get(), iconCx, iconCy, 30.0f * ss);
            }

            // Track.
            const float trackCy = track.y + track.h * 0.5f;
            ComPtr<ID2D1SolidColorBrush> trackBrush;
            if (SUCCEEDED(target->CreateSolidColorBrush(
                    withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.10f * alpha), &trackBrush)) &&
                trackBrush) {
                target->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(track.x, track.y, track.x + track.w,
                                                  track.y + track.h),
                                      track.h * 0.5f, track.h * 0.5f),
                    trackBrush.Get());
            }
            const float fillW = track.w * ClampValue(value, 0.0f, 1.0f);
            if (fillW > 1.0f) {
                ComPtr<ID2D1SolidColorBrush> fillBrush;
                const float fillAlpha = (isVolume && ui.muted) ? 0.35f : 1.0f;
                if (SUCCEEDED(target->CreateSolidColorBrush(
                        withAlpha(accent, fillAlpha * alpha), &fillBrush)) &&
                    fillBrush) {
                    target->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(track.x, track.y, track.x + fillW,
                                                      track.y + track.h),
                                          track.h * 0.5f, track.h * 0.5f),
                        fillBrush.Get());
                }
            }
            // Knob.
            const float knobX = track.x + fillW;
            const bool draggingThis =
                ui.dragging == (isVolume ? Hit::VolSlider : Hit::BrightSlider);
            const float knobR = track.h * 0.5f + 5.0f * ss + (draggingThis ? 2.0f * ss : 0.0f);
            ComPtr<ID2D1SolidColorBrush> knobShadow;
            if (SUCCEEDED(target->CreateSolidColorBrush(
                    withAlpha(D2D1::ColorF(0, 0, 0, 1), 0.25f * alpha), &knobShadow)) &&
                knobShadow) {
                target->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(knobX, trackCy + 1.0f * ss), knobR, knobR),
                    knobShadow.Get());
            }
            whiteBrush->SetColor(withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.98f * alpha));
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, trackCy), knobR, knobR),
                                whiteBrush.Get());
            (void)available;
        };

        if (layout.showVol) {
            drawTile(layout.volTile, layout.volTrack, layout.volIconBox, ui.volVis, true, true);
        }
        if (layout.showBri) {
            drawTile(layout.briTile, layout.briTrack, layout.briIconBox, ui.briVis, true, false);
        }

        // Quick action buttons.
        if (layout.showBtns) {
            struct ButtonSpec {
                RectF rect;
                Hit id;
            };
            const ButtonSpec buttons[] = {
                {layout.btnLock, Hit::BtnLock},
                {layout.btnSleep, Hit::BtnSleep},
                {layout.btnDisplay, Hit::BtnDisplay},
                {layout.btnTheme, Hit::BtnTheme},
            };
            for (const ButtonSpec& btn : buttons) {
                const int idx = static_cast<int>(btn.id);
                const float cx = btn.rect.x + btn.rect.w * 0.5f;
                const float cy = btn.rect.y + btn.rect.h * 0.5f;
                const float btnScale = 1.0f - 0.08f * ui.pressAnim[idx];
                const float r = layout.btnRadius * btnScale;
                const float glowVal = ui.glow[idx];

                ComPtr<ID2D1SolidColorBrush> btnBrush;
                if (SUCCEEDED(target->CreateSolidColorBrush(
                        withAlpha(D2D1::ColorF(1, 1, 1, 1), (0.07f + 0.05f * glowVal) * alpha),
                        &btnBrush)) &&
                    btnBrush) {
                    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r),
                                        btnBrush.Get());
                }
                ComPtr<ID2D1SolidColorBrush> btnRim;
                if (SUCCEEDED(target->CreateSolidColorBrush(
                        withAlpha(D2D1::ColorF(1, 1, 1, 1), (0.10f + 0.08f * glowVal) * alpha),
                        &btnRim)) &&
                    btnRim) {
                    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), btnRim.Get(),
                                        1.0f * ss);
                }

                whiteBrush->SetColor(withAlpha(D2D1::ColorF(1, 1, 1, 1), 0.92f * alpha));
                const float iconS = 40.0f * ss * btnScale;
                switch (btn.id) {
                case Hit::BtnLock:
                    DrawLockIcon(target, whiteBrush.Get(), cx, cy + 1.0f * ss, iconS);
                    break;
                case Hit::BtnSleep:
                    DrawMoonIcon(target, whiteBrush.Get(), cx, cy, iconS);
                    break;
                case Hit::BtnDisplay:
                    DrawMonitorIcon(target, whiteBrush.Get(), cx, cy, iconS);
                    break;
                case Hit::BtnTheme:
                    DrawThemeIcon(target, whiteBrush.Get(), cx, cy, iconS, ui.darkMode);
                    break;
                default:
                    break;
                }
            }
        }

        target->SetTransform(D2D1::Matrix3x2F::Identity());
        if (FAILED(target->EndDraw())) {
            return false;
        }

        POINT src = {0, 0};
        SIZE size = {layout.canvasW, layout.canvasH};
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

    double cornerDwellStart = 0.0;

    MSG msg;
    bool quit = false;
    while (!quit) {
        settings = GetSettingsCopy();
        DWORD timeout = INFINITE;
        if (ui.phase != Phase::Idle) {
            timeout = 16;
        } else if (settings.cornerEnabled) {
            timeout = 66;
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
            if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
                if (ui.phase == Phase::Idle) {
                    showPanel();
                } else {
                    hidePanel(false);
                }
                continue;
            }
            if (msg.message == WM_APP_HIDE) {
                hidePanel(false);
                continue;
            }
            if (msg.message == WM_APP_SETTINGS_APPLIED) {
                registerHotkey();
                textScaleCache = -1.0f;
                ui.briAvailable = brightness.supported;
                ui.volAvailable = audio.ReadState(&ui.volume, &ui.muted) || ui.volAvailable;
                if (ui.volAvailable) {
                    ui.volVis = ui.volume;
                }
                continue;
            }
            if (msg.message == WM_APP_BRIGHTNESS_READ) {
                if (brightness.supported) {
                    ui.brightness = brightness.Get();
                    if (ui.dragging == Hit::None) {
                        ui.briVis = ui.brightness / 100.0f;
                    }
                }
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) {
            break;
        }

        if (g_settingsDirty.exchange(false)) {
            textScaleCache = -1.0f;
        }

        // Advance the phase state machine.
        const double now = NowSeconds();
        if (ui.phase == Phase::Enter) {
            const double dur = settings.reduceMotion ? 0.15 : 0.26;
            const float t = SmoothStep(static_cast<float>((now - ui.phaseStart) / dur));
            const float a = t;
            const float sc = settings.reduceMotion ? 1.0f : (0.94f + 0.06f * t);
            const float yOff = settings.reduceMotion ? 0.0f : (-12.0f * (1.0f - t) * settings.sizeScale * dpiScale);
            drawFrame(a, sc, yOff);
            if (now - ui.phaseStart >= dur) {
                ui.phase = Phase::Visible;
            }
        } else if (ui.phase == Phase::Visible) {
            // Live-sync external volume changes.
            float v = 0.0f;
            bool m = false;
            if (audio.ReadState(&v, &m)) {
                ui.volAvailable = true;
                if (ui.dragging != Hit::VolSlider) {
                    ui.volume = v;
                    ui.volVis = v;
                }
                ui.muted = m;
            }
            // Throttled brightness writes while dragging.
            if (ui.briPending >= 0 && brightness.supported && now - ui.lastBriSet >= 0.030) {
                brightness.Set(ui.briPending);
                ui.brightness = ui.briPending;
                ui.lastBriSet = now;
            }
            // Smooth hover/press visuals (input response, always allowed).
            for (int i = 0; i < kHitCount; ++i) {
                const Hit id = static_cast<Hit>(i);
                const float glowTarget =
                    (ui.hover == id || ui.pressed == id || ui.dragging == id) ? 1.0f : 0.0f;
                ui.glow[i] += (glowTarget - ui.glow[i]) * 0.35f;
                const bool isButton = id == Hit::BtnLock || id == Hit::BtnSleep ||
                                      id == Hit::BtnDisplay || id == Hit::BtnTheme;
                if (isButton) {
                    const float pressTarget = (ui.pressed == id) ? 1.0f : 0.0f;
                    ui.pressAnim[i] += (pressTarget - ui.pressAnim[i]) * 0.40f;
                }
            }
            drawFrame(1.0f, 1.0f, 0.0f);
        } else if (ui.phase == Phase::Exit) {
            const double dur = settings.reduceMotion ? 0.12 : 0.18;
            const float t = SmoothStep(static_cast<float>((now - ui.phaseStart) / dur));
            const float a = 1.0f - t;
            const float sc = settings.reduceMotion ? 1.0f : (1.0f + 0.02f * t);
            const float yOff = settings.reduceMotion ? 0.0f : (-6.0f * t * settings.sizeScale * dpiScale);
            drawFrame(a, sc, yOff);
            if (now - ui.phaseStart >= dur) {
                ui.phase = Phase::Idle;
                ShowWindow(hwnd, SW_HIDE);
                g_panelShowing = false;
            }
        } else {
            // Idle: watch the top-right corner when enabled.
            if (settings.cornerEnabled) {
                POINT p = {};
                GetCursorPos(&p);
                HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi = {sizeof(MONITORINFO)};
                bool inZone = false;
                if (GetMonitorInfoW(mon, &mi)) {
                    inZone = p.x >= mi.rcMonitor.right - 8 && p.y <= mi.rcMonitor.top + 8;
                }
                if (inZone && !IsForegroundFullscreen(mon)) {
                    if (cornerDwellStart <= 0.0) {
                        cornerDwellStart = now;
                    } else if (now - cornerDwellStart >= 0.35) {
                        cornerDwellStart = 0.0;
                        showPanel();
                    }
                } else {
                    cornerDwellStart = 0.0;
                }
            } else {
                cornerDwellStart = 0.0;
            }
        }
    }

    if (hotkeyRegistered) {
        UnregisterHotKey(hwnd, kHotkeyId);
    }
    removeEscHook();
    if (GetCapture() == hwnd) {
        ReleaseCapture();
    }
    ShowWindow(hwnd, SW_HIDE);
    if (g_memDc) {
        DeleteDC(g_memDc);
        g_memDc = nullptr;
    }
    if (g_dib) {
        DeleteObject(g_dib);
        g_dib = nullptr;
    }
    if (target) {
        target->Release();
    }
    DestroyWindow(hwnd);
    g_hwnd = nullptr;
    g_panelShowing = false;
    brightness.Shutdown();
    audio.Shutdown();
    CoUninitialize();
    return 0;
}

}  // namespace

BOOL WhTool_ModInit() {
    InitializeCriticalSection(&g_settingsCs);
    LoadSettings();
    Wh_Log(L"Quick Controls v2B initialized.");
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
    if (g_hwnd) {
        PostMessageW(g_hwnd, WM_APP_SETTINGS_APPLIED, 0, 0);
    }
    Wh_Log(L"QuickControls: settings reloaded.");
}

void WhTool_ModUninit() {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    DeleteCriticalSection(&g_settingsCs);
    Wh_Log(L"Quick Controls v2B unloaded.");
}
