// latency_tray_full.cpp
// Build (MSVC Developer Command Prompt):
//    Standard build:
//    cl /O2 /MT latency_tray_full.cpp /link iphlpapi.lib ws2_32.lib
//
//    Secure build with all mitigations (recommended):
//    For x64:
//    cl /O2 /Oi /Gy /GL /MT /EHsc /guard:cf /Qspectre /GS /sdl /W4 latency_tray_full.cpp /link /LTCG /OPT:REF /OPT:ICF /NXCOMPAT /DYNAMICBASE /HIGHENTROPYVA iphlpapi.lib ws2_32.lib gdi32.lib user32.lib shell32.lib /MANIFESTFILE:latency_tray_full.manifest
//
//    For x86:
//    cl /O2 /Oi /Gy /GL /MT /EHsc /guard:cf /Qspectre /GS /sdl /W4 /SAFESEH latency_tray_full.cpp /link /LTCG /OPT:REF /OPT:ICF /NXCOMPAT /DYNAMICBASE iphlpapi.lib ws2_32.lib gdi32.lib user32.lib shell32.lib /MANIFESTFILE:latency_tray_full.manifest
//
// Tested with Visual Studio toolchain. Should also compile with mingw-w64 (adjust link flags).

#define WIN32_LEAN_AND_MEAN  // Prevent windows.h from including winsock.h
#include <winsock2.h>        // MUST be before windows.h
#include <ws2tcpip.h>        // Should follow winsock2.h
#include <windows.h>
#include <shellapi.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <processthreadsapi.h>  // For SetProcessMitigationPolicy
#include <heapapi.h>            // For HeapSetInformation
#include <string>
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")     // For GDI functions (CreateCompatibleDC, CreateFont, DrawText, etc.)
#pragma comment(lib, "user32.lib")    // For USER functions (GetMessage, CreateWindow, etc.)
#pragma comment(lib, "shell32.lib")   // For Shell_NotifyIconW

#define WM_TRAYICON   (WM_USER + 1)
#define TRAY_UID      1001

// Menu command IDs
#define CMD_EXIT          1
#define CMD_SELECT_BASE   100  // IP selection starts at 100

// Preset IP targets for latency testing
struct IPTarget {
    const char* ip;
    const wchar_t* name;
    const wchar_t* location;
    bool isIPv6;  // true for IPv6, false for IPv4
};

static const IPTarget g_presets[] = {
    // Default gateway (special case - detected dynamically)
    {nullptr, L"Default Gateway", L"Auto-detect", false},
    
    // Cloudflare DNS - Fast and reliable, global distribution
    {"1.1.1.1", L"Cloudflare DNS", L"Global (Anycast)", false},
    {"1.0.0.1", L"Cloudflare DNS (Alt)", L"Global (Anycast)", false},
    
    // Google DNS - Global distribution
    {"8.8.8.8", L"Google DNS", L"Global (Anycast)", false},
    {"8.8.4.4", L"Google DNS (Alt)", L"Global (Anycast)", false},
    
    // Quad9 DNS - Security-focused, global
    {"9.9.9.9", L"Quad9 DNS", L"Global (Anycast)", false},
    
    // OpenDNS - Cisco
    {"208.67.222.222", L"OpenDNS", L"Global", false},
    
    // US East Coast - Cloudflare edge (typically NYC area)
    {"1.1.1.1", L"Cloudflare (US East)", L"US East", false},
    
    // US West Coast - Cloudflare edge (typically LA area)  
    {"1.0.0.1", L"Cloudflare (US West)", L"US West", false},
    
    // US Central - Google edge
    {"8.8.4.4", L"Google (US Central)", L"US Central", false},
    
    // Netflix/Fast.com IPv6 servers (direct ISP peering)
    {"2a00:86c0:2054:2054::167", L"Fast.com (Pittsburgh)", L"Pittsburgh, PA", true},
    {"2a00:86c0:2063:2063::135", L"Fast.com (Ashburn)", L"Ashburn, VA", true},
};

static const int g_numPresets = sizeof(g_presets) / sizeof(g_presets[0]);

static NOTIFYICONDATAW nid;
static HINSTANCE g_hInst;
static HWND g_hWnd;
static std::atomic_bool g_running(true);
static std::atomic<int> g_selectedPreset(0); // 0 = Default Gateway, 1+ = preset index
static char g_targetIP[64] = {0}; // Thread-safe target IP string

// ---------- Utilities ----------
static std::string IPv4ToString(DWORD ip) {
    // ip is in network byte order in many iphlpapi structures; treat as uint32_t containing IPv4 address in
    // network order. inet_ntop expects pointer to struct in_addr (s_addr in network order).
    struct in_addr a;
    a.s_addr = ip;
    char buf[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &a, buf, INET_ADDRSTRLEN) == nullptr) {
        return std::string();
    }
    return std::string(buf);
}

// Attempt to find default IPv4 gateway by scanning the IPv4 routing table for 0.0.0.0/0
// Returns gateway IP in network byte order (DWORD). On failure returns 0.
static DWORD GetDefaultGatewayIPv4() {
    PMIB_IPFORWARDTABLE pTable = nullptr;
    DWORD dwSize = 0;
    DWORD dwRes = GetIpForwardTable(nullptr, &dwSize, FALSE);
    if (dwRes != ERROR_INSUFFICIENT_BUFFER) {
        // unexpected
        return 0;
    }
    if (dwSize == 0) return 0; // Bounds check
    pTable = (PMIB_IPFORWARDTABLE)malloc(dwSize);
    if (!pTable) return 0;
    dwRes = GetIpForwardTable(pTable, &dwSize, FALSE);
    DWORD gateway = 0;
    if (dwRes == NO_ERROR && pTable->dwNumEntries > 0) {
        // Look for the route with destination 0.0.0.0 and mask 0.0.0.0 and best metric
        DWORD bestMetric = 0xFFFFFFFF;
        for (DWORD i = 0; i < pTable->dwNumEntries; ++i) {
            MIB_IPFORWARDROW &r = pTable->table[i];
            if (r.dwForwardDest == 0 && r.dwForwardMask == 0) {
                // Validate: gateway must be non-zero and reasonable (not 0.0.0.0 or 255.255.255.255)
                if (r.dwForwardNextHop != 0 && r.dwForwardNextHop != 0xFFFFFFFF) {
                    // choose lowest metric
                    if (r.dwForwardMetric1 < bestMetric) {
                        bestMetric = r.dwForwardMetric1;
                        gateway = r.dwForwardNextHop; // DWORD in network byte order
                    }
                }
            }
        }
    }
    free(pTable);
    return gateway;
}

// Create a small square icon (16x16 or 32x32 depending on scale) with white text on transparent background.
// text should be ASCII-ish short (like "24" or "--")
// Returns NULL on failure
static HICON CreateTextIcon(const std::wstring &text, int size = 16) {
    // Validate text length (prevent excessive memory usage)
    if (text.length() > 8 || size <= 0 || size > 64) {
        return NULL;
    }
    const int w = size;
    const int h = size;

    // Create 32bpp ARGB DIB section
    BITMAPV5HEADER bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = w;
    bi.bV5Height = -h; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) return NULL;
    void *pvBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcScreen, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (!hBmp) {
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }
    HDC hMem = CreateCompatibleDC(hdcScreen);
    if (!hMem) {
        ReleaseDC(NULL, hdcScreen);
        DeleteObject(hBmp);
        return NULL;
    }
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMem, hBmp);

    // Clear to fully transparent (alpha = 0)
    if (pvBits) {
        DWORD *p = (DWORD*)pvBits;
        for (int i = 0; i < w*h; ++i) p[i] = 0x00000000;
    }

    // Draw text (white) centered
    SetBkMode(hMem, TRANSPARENT);
    SetTextColor(hMem, RGB(255,255,255));
    // Slightly bold small font - system UI font
    int fontHeight = (int)(size * 0.7);
    HFONT hFont = CreateFontW(-fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hMem, hFont);

    RECT rc = {0,0,w,h};
    DrawTextW(hMem, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hMem, hOldFont);
    if (hFont) DeleteObject(hFont);

    // Create Alpha icon via ICONINFO with both color and mask bitmaps using same ARGB bitmap.
    ICONINFO ii;
    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;
    // For mask use a monochrome mask (NULL will work with hbmColor having alpha on modern Windows)
    ii.hbmMask = hBmp;

    HICON hIcon = CreateIconIndirect(&ii);

    // Clean up resources (Windows makes its own copy of bitmap data for the icon)
    SelectObject(hMem, hOldBmp);
    DeleteDC(hMem);
    ReleaseDC(NULL, hdcScreen);
    DeleteObject(hBmp);

    // Return icon handle (NULL on failure, handled by caller)
    return hIcon;
}

// Send single ICMP echo to IPv4 address given in dotted ASCII, return RTT ms, or 0xFFFFFFFF on failure
static DWORD PingOnceIPv4(const char* ipStr, DWORD timeoutMs = 1000) {
    if (!ipStr) return 0xFFFFFFFF;
    // Validate input string length (prevent buffer overflows)
    size_t len = strlen(ipStr);
    if (len == 0 || len >= INET_ADDRSTRLEN) return 0xFFFFFFFF;

    // Use iphlpapi's Icmp* APIs (these do not open listening sockets).
    HANDLE hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE) return 0xFFFFFFFF;

    unsigned char sendData[8] = {0x50,0x49,0x4E,0x47,0x2D,0x54,0x45,0x53}; // "PING-TES" arbitrary payload
    BYTE replyBuffer[128] = {0};
    DWORD replySize = sizeof(replyBuffer);

    struct in_addr a;
    if (inet_pton(AF_INET, ipStr, &a) != 1) {
        IcmpCloseHandle(hIcmp);
        return 0xFFFFFFFF;
    }
    IPAddr dest = a.s_addr; // network order

    DWORD ret = IcmpSendEcho(hIcmp, dest, sendData, sizeof(sendData), NULL, replyBuffer, replySize, timeoutMs);
    DWORD rtt = 0xFFFFFFFF;
    if (ret != 0) {
        PICMP_ECHO_REPLY pReply = (PICMP_ECHO_REPLY)replyBuffer;
        // if status == IP_SUCCESS
        if (pReply->Status == IP_SUCCESS) {
            rtt = pReply->RoundTripTime;
        } else {
            rtt = 0xFFFFFFFF;
        }
    }

    IcmpCloseHandle(hIcmp);
    return rtt;
}

// Send single ICMP echo to IPv6 address given as string, return RTT ms, or 0xFFFFFFFF on failure
static DWORD PingOnceIPv6(const char* ipStr, DWORD timeoutMs = 1000) {
    if (!ipStr) return 0xFFFFFFFF;
    // Validate input string length
    size_t len = strlen(ipStr);
    if (len == 0 || len >= INET6_ADDRSTRLEN) return 0xFFFFFFFF;

    // Use iphlpapi's Icmp6* APIs for IPv6
    HANDLE hIcmp6 = Icmp6CreateFile();
    if (hIcmp6 == INVALID_HANDLE_VALUE) return 0xFFFFFFFF;

    unsigned char sendData[8] = {0x50,0x49,0x4E,0x47,0x2D,0x54,0x45,0x53}; // "PING-TES" arbitrary payload
    
    struct in6_addr dest6;
    if (inet_pton(AF_INET6, ipStr, &dest6) != 1) {
        IcmpCloseHandle(hIcmp6);
        return 0xFFFFFFFF;
    }

    // For IPv6, we need source and destination addresses
    // Use IN6ADDR_ANY_INIT for source (let system choose)
    struct sockaddr_in6 sourceSockAddr;
    ZeroMemory(&sourceSockAddr, sizeof(sourceSockAddr));
    sourceSockAddr.sin6_family = AF_INET6;
    sourceSockAddr.sin6_addr = in6addr_any;

    struct sockaddr_in6 destSockAddr;
    ZeroMemory(&destSockAddr, sizeof(destSockAddr));
    destSockAddr.sin6_family = AF_INET6;
    destSockAddr.sin6_addr = dest6;

    BYTE replyBuffer[sizeof(ICMPV6_ECHO_REPLY) + 8] = {0};
    DWORD replySize = sizeof(replyBuffer);

    DWORD ret = Icmp6SendEcho2(hIcmp6, NULL, NULL, NULL,
                                &sourceSockAddr,
                                &destSockAddr,
                                sendData, sizeof(sendData), NULL,
                                replyBuffer, replySize, timeoutMs);

    DWORD rtt = 0xFFFFFFFF;
    if (ret != 0) {
        PICMPV6_ECHO_REPLY pReply = (PICMPV6_ECHO_REPLY)replyBuffer;
        if (pReply->Status == IP_SUCCESS) {
            rtt = pReply->RoundTripTime;
        } else {
            rtt = 0xFFFFFFFF;
        }
    }

    IcmpCloseHandle(hIcmp6);
    return rtt;
}

// Unified ping function that detects IP version and calls appropriate function
static DWORD PingOnce(const char* ipStr, bool isIPv6, DWORD timeoutMs = 1000) {
    if (isIPv6) {
        return PingOnceIPv6(ipStr, timeoutMs);
    } else {
        return PingOnceIPv4(ipStr, timeoutMs);
    }
}

// Apply Windows runtime process mitigation policies for security hardening
static void HardenProcess() {
    // Enable heap termination on corruption
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
    
    // Secure DLL search: only System32 and user directories, no current directory
    SetDllDirectoryW(L"");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    
    // Strict handle check policy
    PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY sh = {0};
    sh.Flags = 1;
    sh.HandleExceptionsPermanentlyEnabled = 1;
    SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &sh, sizeof(sh));
    
    // Disable dynamic code generation (no JIT, no dynamic code execution)
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dcp = {0};
    dcp.Flags = 1;
    dcp.ProhibitDynamicCode = 1;
    SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &dcp, sizeof(dcp));
    
    // Require Microsoft-signed DLLs only
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY bsp = {0};
    bsp.MicrosoftSignedOnly = 1;
    SetProcessMitigationPolicy(ProcessSignaturePolicy, &bsp, sizeof(bsp));
    
    // Disable extension points (AppInit DLLs, Winlogon notifications, etc.)
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY ep = {0};
    ep.Flags = 1;
    ep.DisableExtensionPoints = 1;
    SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &ep, sizeof(ep));
    
    // Enforce ASLR (Address Space Layout Randomization)
    PROCESS_MITIGATION_ASLR_POLICY aslr = {0};
    aslr.Flags = 1;
    aslr.EnableBottomUpRandomization = 1;
    aslr.EnableForceRelocateImages = 1;
    SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr, sizeof(aslr));
    
    // Enforce DEP (Data Execution Prevention)
    PROCESS_MITIGATION_DEP_POLICY dep = {0};
    dep.Flags = 1;
    dep.Enable = 1;
    dep.Permanent = 1;
    SetProcessMitigationPolicy(ProcessDEPPolicy, &dep, sizeof(dep));
    
    // Enable Control Flow Guard
    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg = {0};
    cfg.Flags = 1;
    cfg.EnableControlFlowGuard = 1;
    SetProcessMitigationPolicy(ProcessControlFlowGuardPolicy, &cfg, sizeof(cfg));
    
    // Prevent child process creation
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY cpp = {0};
    cpp.Flags = 1;
    cpp.NoChildProcessCreation = 1;
    SetProcessMitigationPolicy(ProcessChildProcessPolicy, &cpp, sizeof(cpp));
}

// ---------- Tray / Window ----------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON) {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            
            // Add "Target:" header
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Target:");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            
            // Add preset targets
            int currentPreset = g_selectedPreset.load();
            for (int i = 0; i < g_numPresets; ++i) {
                wchar_t menuText[256] = {0};
                if (g_presets[i].ip == nullptr) {
                    // Default Gateway
                    wcscpy_s(menuText, _countof(menuText), g_presets[i].name);
                } else {
                    // Show IPv6 addresses in brackets for clarity
                    if (g_presets[i].isIPv6) {
                        swprintf_s(menuText, _countof(menuText), L"%s [%S]", g_presets[i].name, g_presets[i].ip);
                    } else {
                        swprintf_s(menuText, _countof(menuText), L"%s (%S)", g_presets[i].name, g_presets[i].ip);
                    }
                }
                UINT flags = MF_STRING;
                if (i == currentPreset) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(hMenu, flags, CMD_SELECT_BASE + i, menuText);
            }
            
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, CMD_EXIT, L"Exit");
            
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            
            if (cmd == CMD_EXIT) {
                g_running = false;
                PostQuitMessage(0);
            } else if (cmd >= CMD_SELECT_BASE && cmd < CMD_SELECT_BASE + g_numPresets) {
                // Update selected preset
                int newPreset = cmd - CMD_SELECT_BASE;
                g_selectedPreset.store(newPreset);
                
                // Update target IP immediately if it's a fixed IP
                if (g_presets[newPreset].ip != nullptr) {
                    strncpy_s(g_targetIP, sizeof(g_targetIP), g_presets[newPreset].ip, _TRUNCATE);
                }
                // Otherwise (default gateway), it will be updated in the worker thread
            }
        } else if (lParam == WM_LBUTTONUP) {
            // optional: show tooltip manually or bounce icon - keep minimal (no action)
        }
    } else if (msg == WM_DESTROY) {
        g_running = false;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Worker thread: find gateway, ping, update icon & tooltip
DWORD WINAPI WorkerThread(LPVOID) {
    // Initialize target IP based on current selection
    char targetCStr[64] = {0};
    
    // Get current preset selection
    int selectedPreset = g_selectedPreset.load();
    
    if (selectedPreset == 0) {
        // Default Gateway mode
        DWORD gw = GetDefaultGatewayIPv4();
        std::string gwStr;
        if (gw != 0) {
            gwStr = IPv4ToString(gw);
            if (gwStr.empty()) {
                gwStr = "1.1.1.1"; // fallback if conversion failed
            }
        } else {
            gwStr = "1.1.1.1"; // fallback public resolver
        }
        strncpy_s(targetCStr, sizeof(targetCStr), gwStr.c_str(), _TRUNCATE);
    } else if (selectedPreset > 0 && selectedPreset < g_numPresets && g_presets[selectedPreset].ip != nullptr) {
        // Fixed IP preset (IPv4 or IPv6)
        size_t ipLen = strlen(g_presets[selectedPreset].ip);
        if (ipLen < sizeof(targetCStr)) {
            strncpy_s(targetCStr, sizeof(targetCStr), g_presets[selectedPreset].ip, _TRUNCATE);
        } else {
            strncpy_s(targetCStr, sizeof(targetCStr), "1.1.1.1", _TRUNCATE);
        }
    } else {
        // Invalid selection, fallback to Cloudflare
        strncpy_s(targetCStr, sizeof(targetCStr), "1.1.1.1", _TRUNCATE);
    }
    
    // Copy to shared buffer for thread-safe access
    strncpy_s(g_targetIP, sizeof(g_targetIP), targetCStr, _TRUNCATE);

    // Decide icon size (choose 16; on high DPI Windows will scale it)
    const int iconSize = 16;

    // keep a tiny rolling average if desired (simple)
    const int MAX_SAMPLES = 10;
    std::vector<DWORD> samples;
    samples.reserve(MAX_SAMPLES);
    
    // Track consecutive failures to clear stale averages
    int consecutiveFailures = 0;
    const int MAX_CONSECUTIVE_FAILURES = 5; // Clear after 5 consecutive failures

    // Cache previous icon text to avoid unnecessary recreations
    wchar_t prevIconText[16] = {0};

    // Track last successfully pushed icon handle for safe destruction
    // (only destroy after successful Shell_NotifyIconW to ensure Explorer has taken ownership)
    static HICON lastPushedIconHandle = NULL;

    while (g_running) {
        // Check if target has changed
        int currentPreset = g_selectedPreset.load();
        
        if (currentPreset == 0) {
            // Default Gateway mode - re-check gateway occasionally (every 10s)
            static int counter = 0;
            if (counter++ % 10 == 0) {
                DWORD gw2 = GetDefaultGatewayIPv4();
                if (gw2 != 0) {
                    std::string s = IPv4ToString(gw2);
                    if (!s.empty() && s.length() < sizeof(targetCStr)) {
                        strncpy_s(targetCStr, sizeof(targetCStr), s.c_str(), _TRUNCATE);
                        strncpy_s(g_targetIP, sizeof(g_targetIP), targetCStr, _TRUNCATE);
                    }
                }
            }
        } else if (currentPreset > 0 && currentPreset < g_numPresets && g_presets[currentPreset].ip != nullptr) {
            // Fixed IP preset - update if changed
            if (strcmp(targetCStr, g_presets[currentPreset].ip) != 0) {
                strncpy_s(targetCStr, sizeof(targetCStr), g_presets[currentPreset].ip, _TRUNCATE);
                strncpy_s(g_targetIP, sizeof(g_targetIP), targetCStr, _TRUNCATE);
            }
        }

        // Use the current target
        char* currentTarget = targetCStr;
        if (g_targetIP[0] != 0) {
            currentTarget = g_targetIP;
        }

        // Determine if current target is IPv6
        bool isIPv6 = false;
        if (currentPreset == 0) {
            // Default Gateway is always IPv4
            isIPv6 = false;
        } else if (currentPreset > 0 && currentPreset < g_numPresets) {
            // Use the preset's IPv6 flag
            isIPv6 = g_presets[currentPreset].isIPv6;
        } else {
            // Fallback: Auto-detect by checking if string contains colons (IPv6 indicator)
            isIPv6 = (strchr(currentTarget, ':') != nullptr);
        }

        DWORD rtt = PingOnce(currentTarget, isIPv6, 1000 /*timeout*/);

        // update rolling samples
        if (rtt != 0xFFFFFFFF) {
            consecutiveFailures = 0; // Reset failure counter on success
            if ((int)samples.size() >= MAX_SAMPLES) samples.erase(samples.begin());
            samples.push_back(rtt);
        } else {
            // treat as missing; don't add
            consecutiveFailures++;
            // Clear stale averages after consecutive failures to avoid misleading data
            if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
                samples.clear();
            }
        }

        // compute simple average for tooltip
        DWORD avg = 0;
        if (!samples.empty()) {
            uint64_t sum = 0;
            for (auto v : samples) sum += v;
            avg = (DWORD)(sum / samples.size());
        }

        // prepare text for icon: short number or "--"
        wchar_t iconText[16] = {0};
        if (rtt == 0xFFFFFFFF) {
            wcscpy_s(iconText, _countof(iconText), L"--");
        } else {
            swprintf_s(iconText, _countof(iconText), L"%u", (unsigned)rtt);
        }
        
        // Only recreate icon if text changed (optimization)
        HICON hIcon = NULL;
        if (wcscmp(iconText, prevIconText) != 0) {
            wcscpy_s(prevIconText, _countof(prevIconText), iconText);
            hIcon = CreateTextIcon(iconText, iconSize);
            if (hIcon) {
                // Replace icon in notification structure
                // The old icon handle is tracked in lastPushedIconHandle and will be
                // safely destroyed after the next successful Shell_NotifyIconW call
                nid.hIcon = hIcon;
            }
        }

        // tooltip text: e.g. "Cloudflare DNS (1.1.1.1) — 24 ms (avg 26 ms)"
        wchar_t tip[256] = {0};
        const wchar_t* targetName = L"Target";
        if (currentPreset >= 0 && currentPreset < g_numPresets) {
            targetName = g_presets[currentPreset].name;
        }
        
        // Format IP display: use brackets for IPv6
        wchar_t ipDisplay[128] = {0};
        if (isIPv6) {
            swprintf_s(ipDisplay, _countof(ipDisplay), L"[%S]", currentTarget);
        } else {
            swprintf_s(ipDisplay, _countof(ipDisplay), L"(%S)", currentTarget);
        }
        
        if (rtt == 0xFFFFFFFF) {
            swprintf_s(tip, _countof(tip), L"%s %s — no reply", targetName, ipDisplay);
        } else {
            if (avg != 0) {
                swprintf_s(tip, _countof(tip), L"%s %s — %u ms (avg %u ms)", targetName, ipDisplay, (unsigned)rtt, (unsigned)avg);
            } else {
                swprintf_s(tip, _countof(tip), L"%s %s — %u ms", targetName, ipDisplay, (unsigned)rtt);
            }
        }

        // Copy tooltip safely into nid.szTip (128 wchar)
        wcsncpy_s(nid.szTip, _countof(nid.szTip), tip, _TRUNCATE);

        // Update the tray (check return value)
        BOOL notifySuccess = Shell_NotifyIconW(NIM_MODIFY, &nid);
        if (!notifySuccess) {
            // If modify fails, try to re-add (icon may have been lost)
            notifySuccess = Shell_NotifyIconW(NIM_ADD, &nid);
        }
        
        // Only destroy previous icon handle after successful push
        // This ensures Explorer has taken ownership of the new icon before we free the old one
        if (notifySuccess) {
            // Destroy the previously tracked icon (the one that was successfully pushed before)
            // This is safe because Explorer has taken ownership of the new icon in nid.hIcon
            if (lastPushedIconHandle && lastPushedIconHandle != nid.hIcon) {
                DestroyIcon(lastPushedIconHandle);
            }
            // Update tracked handle to the icon we just successfully pushed
            // (This is the icon that will be destroyed after the NEXT successful push)
            lastPushedIconHandle = nid.hIcon;
        }

        // Sleep 1 second total (keep low-duty)
        for (int i = 0; i < 10 && g_running; ++i) Sleep(100); // wake every 100ms to be responsive to exit
    }

    // Cleanup: destroy last pushed icon handle on thread exit
    // (the icon in nid.hIcon will be cleaned up in main thread)
    if (lastPushedIconHandle) {
        DestroyIcon(lastPushedIconHandle);
        lastPushedIconHandle = NULL;
    }

    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInst = hInstance;

    // Initialize Winsock (required for inet_ntop/inet_pton)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 1; // Failed to initialize Winsock
    }

    // Apply runtime process mitigation policies for security hardening
    HardenProcess();

    // Register a message-only window to receive tray callbacks
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"LatencyTrayHiddenClass";
    ATOM classAtom = RegisterClassW(&wc);
    if (classAtom == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            // Only fail if it's not already registered (might be from previous instance)
            WSACleanup();
            return 1;
        }
    }

    g_hWnd = CreateWindowW(wc.lpszClassName, L"LatencyTrayHiddenWindow",
                           0, 0,0,0,0,
                           HWND_MESSAGE, NULL, hInstance, NULL);
    if (!g_hWnd) {
        WSACleanup();
        return 1;
    }

    // Initialize notify icon data
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = TRAY_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = CreateTextIcon(L"--", 16);
    if (!nid.hIcon) {
        // If icon creation fails, try with a simpler fallback
        nid.hIcon = CreateTextIcon(L"??", 16);
    }
    wcscpy_s(nid.szTip, _countof(nid.szTip), L"Latency Tray (starting...)");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        // Failed to add tray icon
        if (nid.hIcon) DestroyIcon(nid.hIcon);
        DestroyWindow(g_hWnd);
        WSACleanup();
        return 1;
    }

    // Spawn worker thread
    HANDLE hThread = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    if (!hThread) {
        // Failed to create thread
        Shell_NotifyIconW(NIM_DELETE, &nid);
        if (nid.hIcon) DestroyIcon(nid.hIcon);
        DestroyWindow(g_hWnd);
        WSACleanup();
        return 1;
    }

    // Message loop (minimal)
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Clean up
    g_running = false;
    if (hThread) {
        WaitForSingleObject(hThread, 2000);
        CloseHandle(hThread);
    }
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (nid.hIcon) {
        DestroyIcon(nid.hIcon);
        nid.hIcon = NULL;
    }
    if (g_hWnd) {
        DestroyWindow(g_hWnd);
        g_hWnd = NULL;
    }
    WSACleanup();

    return 0;
}
