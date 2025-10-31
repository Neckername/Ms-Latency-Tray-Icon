// latency_tray_trimmed.cpp - Aggressive memory trimming version
// This version uses Windows APIs to minimize working set
// while keeping all security features intact

#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0500
#include <winsock2.h>  // Must be before windows.h
#include <ws2tcpip.h>  // For IP constants
#include <windows.h>
#include <shellapi.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ipexport.h>  // For IP_STATUS constants
#include <processthreadsapi.h>
#include <heapapi.h>
#include <psapi.h>  // For working set functions

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")

// Delay load expensive DLLs
#pragma comment(lib, "delayimp.lib")
#pragma comment(linker, "/DELAYLOAD:iphlpapi.dll")
#pragma comment(linker, "/DELAYLOAD:ws2_32.dll")
#pragma comment(linker, "/DELAYLOAD:gdi32.dll")

#define WM_TRAYICON   (WM_USER + 1)
#define TRAY_UID      1001
#define CMD_EXIT      1
#define CMD_SELECT_BASE 100

// Minimal target structure with IPv6 support
struct Target {
    const char* ip;
    const char* name;
    bool isIPv6;  // true for IPv6, false for IPv4
};

static const Target g_targets[] = {
    {"1.1.1.1", "Cloudflare DNS", false},
    {"8.8.8.8", "Google DNS", false},
    {"9.9.9.9", "Quad9 DNS", false},
    {"208.67.222.222", "OpenDNS", false},
    {"1.0.0.1", "Cloudflare Alt", false},
    {"8.8.4.4", "Google Alt", false},
    // Netflix/Fast.com IPv6 servers (direct ISP peering)
    {"2a00:86c0:2054:2054::167", "Fast.com (Pittsburgh)", true},
    {"2a00:86c0:2063:2063::135", "Fast.com (Ashburn)", true}
};
static const int g_numTargets = sizeof(g_targets) / sizeof(g_targets[0]);

static NOTIFYICONDATAA nid;
static HWND g_hWnd;
static volatile BOOL g_running = TRUE;
static volatile int g_selectedTarget = 0;  // Index into g_targets
static char g_targetIP[46] = "1.1.1.1";  // Increased to 46 for IPv6 (INET6_ADDRSTRLEN)

// Minimal icon creation - solid color square with text
static HICON CreateMinimalIcon(const char* text) {
    HDC hdc = GetDC(NULL);
    if (!hdc) return NULL;
    
    HDC hMemDC = CreateCompatibleDC(hdc);
    if (!hMemDC) {
        ReleaseDC(NULL, hdc);
        return NULL;
    }
    
    // Create minimal 16x16 bitmap
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, 16, 16);
    if (!hBitmap) {
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hdc);
        return NULL;
    }
    
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBitmap);
    
    // Fill with dark background
    RECT rc = {0, 0, 16, 16};
    FillRect(hMemDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    
    // Draw text in white
    SetBkMode(hMemDC, TRANSPARENT);
    SetTextColor(hMemDC, RGB(255, 255, 255));
    
    // Use DrawTextA instead of TextOutA for better compatibility
    DrawTextA(hMemDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    SelectObject(hMemDC, hOldBmp);
    
    // Create mask
    HBITMAP hMask = CreateBitmap(16, 16, 1, 1, NULL);
    if (!hMask) {
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hdc);
        return NULL;
    }
    
    HDC hMaskDC = CreateCompatibleDC(hdc);
    if (!hMaskDC) {
        DeleteObject(hBitmap);
        DeleteObject(hMask);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hdc);
        return NULL;
    }
    
    HBITMAP hOldMask = (HBITMAP)SelectObject(hMaskDC, hMask);
    FillRect(hMaskDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(hMaskDC, hOldMask);
    
    // Create icon
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = hMask;
    ii.hbmColor = hBitmap;
    HICON hIcon = CreateIconIndirect(&ii);
    
    // Cleanup
    DeleteDC(hMaskDC);
    DeleteDC(hMemDC);
    DeleteObject(hMask);
    DeleteObject(hBitmap);
    ReleaseDC(NULL, hdc);
    
    return hIcon;
}

// IPv4 ping function
static DWORD PingIPv4(const char* ip) {
    HANDLE hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE) return 999;
    
    unsigned long addr = inet_addr(ip);
    if (addr == INADDR_NONE) {
        IcmpCloseHandle(hIcmp);
        return 999;
    }
    
    char data[4] = "PNG";
    BYTE reply[sizeof(ICMP_ECHO_REPLY) + sizeof(data)];
    
    DWORD ret = IcmpSendEcho(hIcmp, addr, data, sizeof(data), 
                             NULL, reply, sizeof(reply), 1000);
    
    DWORD rtt = 999;
    if (ret != 0) {
        PICMP_ECHO_REPLY pReply = (PICMP_ECHO_REPLY)reply;
        if (pReply->Status == IP_SUCCESS) {
            rtt = pReply->RoundTripTime;
            if (rtt == 0) rtt = 1;  // Show 1ms minimum
        }
    }
    
    IcmpCloseHandle(hIcmp);
    return rtt;
}

// IPv6 ping function (minimal)
static DWORD PingIPv6(const char* ip) {
    HANDLE hIcmp6 = Icmp6CreateFile();
    if (hIcmp6 == INVALID_HANDLE_VALUE) return 999;
    
    struct in6_addr dest6;
    if (inet_pton(AF_INET6, ip, &dest6) != 1) {
        IcmpCloseHandle(hIcmp6);
        return 999;
    }
    
    // Source and destination addresses
    struct sockaddr_in6 source = {0};
    source.sin6_family = AF_INET6;
    source.sin6_addr = in6addr_any;
    
    struct sockaddr_in6 dest = {0};
    dest.sin6_family = AF_INET6;
    dest.sin6_addr = dest6;
    
    char data[4] = "PNG";
    BYTE reply[sizeof(ICMPV6_ECHO_REPLY) + sizeof(data)];
    
    DWORD ret = Icmp6SendEcho2(hIcmp6, NULL, NULL, NULL,
                               &source, &dest, data, sizeof(data), NULL,
                               reply, sizeof(reply), 1000);
    
    DWORD rtt = 999;
    if (ret != 0) {
        PICMPV6_ECHO_REPLY pReply = (PICMPV6_ECHO_REPLY)reply;
        if (pReply->Status == IP_SUCCESS) {
            rtt = pReply->RoundTripTime;
            if (rtt == 0) rtt = 1;
        }
    }
    
    IcmpCloseHandle(hIcmp6);
    return rtt;
}

// Unified ping that handles both IPv4 and IPv6
static DWORD SimplePing(const char* ip, bool isIPv6) {
    if (isIPv6) {
        return PingIPv6(ip);
    } else {
        return PingIPv4(ip);
    }
}

// Safe memory trimming
static void TrimMemory() {
    // Only trim working set, don't empty it completely
    // This is safer and prevents crashes
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    
    // Don't compact heaps during active operations
    // as it can cause crashes with active allocations
}

// Apply all security hardening
static void HardenProcess() {
    // Enable heap termination on corruption
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
    
    // Secure DLL search
    SetDllDirectoryW(L"");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    
    // DEP
    PROCESS_MITIGATION_DEP_POLICY dep = {0};
    dep.Enable = 1;
    dep.Permanent = 1;
    SetProcessMitigationPolicy(ProcessDEPPolicy, &dep, sizeof(dep));
    
    // ASLR
    PROCESS_MITIGATION_ASLR_POLICY aslr = {0};
    aslr.EnableBottomUpRandomization = 1;
    aslr.EnableForceRelocateImages = 1;
    SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr, sizeof(aslr));
}

// Window procedure with menu
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON && lParam == WM_RBUTTONUP) {
        POINT pt;
        GetCursorPos(&pt);
        HMENU hMenu = CreatePopupMenu();
        
        // Add header
        AppendMenuA(hMenu, MF_STRING | MF_GRAYED, 0, "Select Target:");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        
        // Add targets
        for (int i = 0; i < g_numTargets; i++) {
            UINT flags = MF_STRING;
            if (i == g_selectedTarget) {
                flags |= MF_CHECKED;
            }
            AppendMenuA(hMenu, flags, CMD_SELECT_BASE + i, g_targets[i].name);
        }
        
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, CMD_EXIT, "Exit");
        
        SetForegroundWindow(hWnd);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(hMenu);
        
        if (cmd == CMD_EXIT) {
            g_running = FALSE;
            PostQuitMessage(0);
        } else if (cmd >= CMD_SELECT_BASE && cmd < CMD_SELECT_BASE + g_numTargets) {
            // Update selected target (supports both IPv4 and IPv6)
            g_selectedTarget = cmd - CMD_SELECT_BASE;
            // Note: g_targetIP is no longer needed as we directly use g_targets[g_selectedTarget].ip
        }
    } else if (msg == WM_DESTROY) {
        g_running = FALSE;
        PostQuitMessage(0);
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// Worker thread - minimal stack
DWORD WINAPI WorkerThread(LPVOID) {
    char text[8];
    HICON hIcon = NULL;
    HICON hOldIcon = NULL;
    int trimCounter = 0;
    
    // Wait a moment for main thread to complete initialization
    Sleep(100);
    
    while (g_running) {
        // Use the current target IP and IPv6 flag
        const char* currentIP = g_targets[g_selectedTarget].ip;
        bool isIPv6 = g_targets[g_selectedTarget].isIPv6;
        DWORD rtt = SimplePing(currentIP, isIPv6);
        
        // Format text
        if (rtt == 999 || rtt == 0xFFFFFFFF) {
            lstrcpyA(text, "--");
        } else if (rtt > 999) {
            lstrcpyA(text, "999");
        } else {
            wsprintfA(text, "%u", rtt);
        }
        
        // Create new icon
        hOldIcon = hIcon;
        hIcon = CreateMinimalIcon(text);
        if (hIcon) {
            nid.hIcon = hIcon;
            
            // Update tooltip with target name (shows IPv6 indicator for IPv6 targets)
            if (rtt == 999 || rtt == 0xFFFFFFFF) {
                if (g_targets[g_selectedTarget].isIPv6) {
                    wsprintfA(nid.szTip, "%s [IPv6]: No response", g_targets[g_selectedTarget].name);
                } else {
                    wsprintfA(nid.szTip, "%s: No response", g_targets[g_selectedTarget].name);
                }
            } else {
                if (g_targets[g_selectedTarget].isIPv6) {
                    wsprintfA(nid.szTip, "%s [IPv6]: %s ms", g_targets[g_selectedTarget].name, text);
                } else {
                    wsprintfA(nid.szTip, "%s: %s ms", g_targets[g_selectedTarget].name, text);
                }
            }
            
            // Update tray - check if still running
            if (g_running && g_hWnd) {
                Shell_NotifyIconA(NIM_MODIFY, &nid);
            }
            
            // Delete old icon after successful update
            if (hOldIcon) {
                DestroyIcon(hOldIcon);
                hOldIcon = NULL;
            }
        }
        
        // Safe memory trimming every 10 seconds
        if (++trimCounter >= 10) {
            TrimMemory();
            trimCounter = 0;
        }
        
        // Check if still running before sleep
        if (g_running) {
            Sleep(1000);
        }
    }
    
    // Cleanup
    if (hIcon) {
        DestroyIcon(hIcon);
    }
    return 0;
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Apply security hardening first
    HardenProcess();
    
    // Initialize Winsock (minimal)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 0), &wsaData);
    
    // Register minimal window class
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "LT";
    RegisterClassA(&wc);
    
    // Create message-only window
    g_hWnd = CreateWindowA("LT", "", 0, 0, 0, 0, 0, 
                          HWND_MESSAGE, NULL, hInstance, NULL);
    
    if (!g_hWnd) {
        WSACleanup();
        return 1;
    }
    
    // Initialize tray icon
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = TRAY_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    
    // Create initial icon
    HICON initialIcon = CreateMinimalIcon("--");
    if (!initialIcon) {
        // Fallback to default icon if creation fails
        initialIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    nid.hIcon = initialIcon;
    lstrcpyA(nid.szTip, "Starting...");
    
    if (!Shell_NotifyIconA(NIM_ADD, &nid)) {
        DestroyWindow(g_hWnd);
        WSACleanup();
        return 1;
    }
    
    // Create worker thread with minimal stack (32KB is safer than 16KB)
    HANDLE hThread = CreateThread(NULL, 32768, WorkerThread, NULL, 
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    
    if (!hThread) {
        Shell_NotifyIconA(NIM_DELETE, &nid);
        if (nid.hIcon) DestroyIcon(nid.hIcon);
        DestroyWindow(g_hWnd);
        WSACleanup();
        return 1;
    }
    
    // Set thread and process priority to reduce resource usage
    SetThreadPriority(hThread, THREAD_PRIORITY_BELOW_NORMAL);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    
    // Message loop
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    
    // Cleanup
    g_running = FALSE;
    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);
    Shell_NotifyIconA(NIM_DELETE, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
    DestroyWindow(g_hWnd);
    WSACleanup();
    
    return 0;
}
