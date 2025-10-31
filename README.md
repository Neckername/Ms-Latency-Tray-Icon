# 📊 Ms. Latency Tray Icon

A **lightweight**, **secure**, and **low-overhead** Windows system tray application that displays real-time network latency. Perfect for monitoring your connection quality at a glance.

![Windows](https://img.shields.io/badge/Windows-10%2B-blue) ![C++](https://img.shields.io/badge/C%2B%2B-17-blue) ![License](https://img.shields.io/badge/License-MIT-green) ![Security](https://img.shields.io/badge/Security-Hardened-red)

## ✨ Features

- 🎯 **Real-time Latency Display** - Shows round-trip time (RTT) directly in your system tray
- 🔄 **Auto-Detection** - Automatically finds and monitors your default gateway
- 🌍 **Multiple Targets** - Choose from preset IP targets including:
  - Cloudflare DNS (1.1.1.1, 1.0.0.1)
  - Google DNS (8.8.8.8, 8.8.4.4)
  - Quad9 DNS (9.9.9.9)
  - OpenDNS (208.67.222.222)
  - Netflix/Fast.com IPv6 servers
  - Regional US endpoints
- 🔒 **Enterprise-Grade Security** - Built with comprehensive security mitigations
- 💚 **IPv4 & IPv6 Support** - Works with both IP protocols
- ⚡ **Ultra-Low Overhead** - Minimal CPU and memory usage
- 📈 **Rolling Average** - Shows current and average latency in tooltip
- 🎨 **High-DPI Support** - Sharp icons on all display resolutions

## 🚀 Quick Start

### Prerequisites

- Windows 10 or later
- Visual Studio 2019+ (or MSVC Build Tools)
- Developer Command Prompt for VS

### Building from Source

#### Standard Build

```cmd
cl /O2 /MT latency_tray_full.cpp /link iphlpapi.lib ws2_32.lib gdi32.lib user32.lib shell32.lib
```

#### Secure Build (Recommended)

**For x64 (64-bit):**
```cmd
cl /O2 /Oi /Gy /GL /MT /EHsc /guard:cf /Qspectre /GS /sdl /W4 latency_tray_full.cpp /link /LTCG /OPT:REF /OPT:ICF /NXCOMPAT /DYNAMICBASE /HIGHENTROPYVA iphlpapi.lib ws2_32.lib gdi32.lib user32.lib shell32.lib /MANIFESTFILE:latency_tray_full.manifest
```

**For x86 (32-bit):**
```cmd
cl /O2 /Oi /Gy /GL /MT /EHsc /guard:cf /Qspectre /GS /sdl /W4 /SAFESEH latency_tray_full.cpp /link /LTCG /OPT:REF /OPT:ICF /NXCOMPAT /DYNAMICBASE iphlpapi.lib ws2_32.lib gdi32.lib user32.lib shell32.lib /MANIFESTFILE:latency_tray_full.manifest
```

**Note:** Open the appropriate "Developer Command Prompt" (x64 Native Tools for 64-bit builds).

### Running

Simply launch `latency_tray_full.exe`. The icon will appear in your system tray showing the current latency.

- **Left-click**: No action (minimal design)
- **Right-click**: Context menu to:
  - Select different latency targets
  - View current selection (checked item)
  - Exit the application

## 📖 Usage

### Selecting a Target

1. Right-click the tray icon
2. Choose a target from the menu:
   - **Default Gateway** - Automatically detects your router
   - **Cloudflare DNS** - Fast, privacy-focused DNS
   - **Google DNS** - Reliable global DNS
   - **Quad9 DNS** - Security-focused DNS
   - **Regional Targets** - US East/West/Central endpoints
   - **IPv6 Targets** - Fast.com servers (if IPv6 is available)

### Understanding the Display

- **Icon Text**: Shows current RTT in milliseconds, or `--` if unreachable
- **Tooltip**: Displays detailed information:
  - Target name and IP address
  - Current latency
  - Rolling average (if available)

## 🔒 Security Features

This application is designed with security as a top priority, making it suitable for **corporate environments**:

### Runtime Mitigations
- ✅ Heap termination on corruption
- ✅ Secure DLL search paths
- ✅ Strict handle validation
- ✅ No dynamic code execution
- ✅ Microsoft-signed DLLs only
- ✅ Extension points disabled
- ✅ ASLR, DEP, and CFG enforced
- ✅ No child process creation

### Code-Level Security
- ✅ Safe string handling (no buffer overflows)
- ✅ Input validation on all operations
- ✅ Thread-safe resource management
- ✅ No user-supplied IP strings (compile-time only)
- ✅ No persistent storage of network data

### Build-Time Security
- ✅ Control Flow Guard (`/guard:cf`)
- ✅ Spectre mitigations (`/Qspectre`)
- ✅ Stack protection (`/GS`)
- ✅ Address space randomization (`/DYNAMICBASE`)
- ✅ DEP/NX compatibility (`/NXCOMPAT`)

For detailed security documentation, see [SECURITY.md](SECURITY.md).

## 🏗️ Architecture

- **Single Worker Thread** - Handles all network operations (1-second default interval)
- **Message-Only Window** - Lightweight window for tray icon callbacks
- **Win32/ICMP APIs** - Uses native Windows networking (no listening sockets)
- **No Elevation Required** - Runs at user privilege level
- **Static Linking** - No external DLL dependencies (except Windows system DLLs)

## 📁 Project Structure

```
.
├── latency_tray_full.cpp      # Main source file
├── latency_tray_full.manifest  # Application manifest
├── SECURITY.md                 # Detailed security documentation
└── README.md                   # This file
```

## 🔧 Technical Details

### Requirements
- **Platform**: Windows 10 or later
- **Dependencies**: Windows SDK (iphlpapi.lib, ws2_32.lib, gdi32.lib, user32.lib, shell32.lib)
- **Privileges**: User-level (no administrator rights required)
- **Network**: ICMP echo requests (outbound only, no listening sockets)

### Supported IP Targets

The application includes pre-configured targets:

| Target | IP Address | Protocol | Location |
|--------|-----------|----------|----------|
| Default Gateway | Auto-detect | IPv4 | Local network |
| Cloudflare DNS | 1.1.1.1 | IPv4 | Global (Anycast) |
| Google DNS | 8.8.8.8 | IPv4 | Global (Anycast) |
| Quad9 DNS | 9.9.9.9 | IPv4 | Global (Anycast) |
| OpenDNS | 208.67.222.222 | IPv4 | Global |
| Fast.com (Pittsburgh) | 2a00:86c0:2054:2054::167 | IPv6 | Pittsburgh, PA |
| Fast.com (Ashburn) | 2a00:86c0:2063:2063::135 | IPv6 | Ashburn, VA |

## 🐛 Troubleshooting

### Icon shows `--`
- Check your internet connection
- Verify the target IP is reachable
- Some networks block ICMP; try a different target

### Build errors
- Ensure you're using the correct Developer Command Prompt (x64 vs x86)
- Verify all required libraries are available
- Check that `latency_tray_full.manifest` exists

### High CPU usage
- This shouldn't happen; if it does, file an issue
- Check Windows Task Manager for conflicting processes

## 📝 License

This project is open source. See LICENSE file for details.

## 🤝 Contributing

Contributions are welcome! Please ensure:
- Code follows the existing security standards
- All builds complete successfully
- Security mitigations remain intact

## 🙏 Acknowledgments

Built with Windows Win32 API and designed for minimal resource usage and maximum security.

---

**Made with ❤️ for network monitoring enthusiasts**

⭐ If you find this useful, consider giving it a star!

