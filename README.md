# 📊 Ms. Latency Tray Icon

A **lightweight**, **secure**, and **ultra-low memory** Windows system tray application that displays real-time network latency. Perfect for monitoring your connection quality at a glance.

![Windows](https://img.shields.io/badge/Windows-10%2B-blue) ![C++](https://img.shields.io/badge/C%2B%2B-17-blue) ![Memory](https://img.shields.io/badge/Memory-<1MB-brightgreen) ![Security](https://img.shields.io/badge/Security-Hardened-red) ![License](https://img.shields.io/badge/License-MIT-green)

## ✨ Features

- 🎯 **Real-time Latency Display** - Shows round-trip time (RTT) directly in your system tray
- 🚀 **Ultra-Low Memory** - Only **300-900KB** working memory (91% reduction from v1.0!)
- 🌍 **Multiple Targets** - Choose from preset IP targets including:
  - Cloudflare DNS (1.1.1.1, 1.0.0.1)
  - Google DNS (8.8.8.8, 8.8.4.4)
  - Quad9 DNS (9.9.9.9)
  - OpenDNS (208.67.222.222)
  - Netflix/Fast.com IPv6 servers (Pittsburgh & Ashburn)
- 🔒 **Enterprise-Grade Security** - Built with comprehensive security mitigations
- 💚 **IPv4 & IPv6 Support** - Full dual-stack networking
- ⚡ **Ultra-Low Overhead** - Minimal CPU usage, <1MB memory
- 🎨 **High-DPI Support** - Sharp icons on all display resolutions
- 🔄 **Automatic Memory Trimming** - Maintains minimal footprint

## 🚀 Quick Start

### Prerequisites

- Windows 10 or later
- Visual Studio 2019+ (or MSVC Build Tools)
- Developer Command Prompt for VS

### Building from Source

```cmd
# From Developer Command Prompt
build_trimmed.bat
```

This creates `latency_tray_trimmed.exe` with **300-900KB memory usage** and all security features enabled.

#### Manual Build (if needed)

```cmd
cl /O1 /Os /Oy /GF /Gy /GL /MD /GS /guard:cf /Qspectre /W4 latency_tray_trimmed.cpp /Fe:latency_tray_trimmed.exe /link /LTCG /OPT:REF /OPT:ICF=10 /STACK:0x10000,0x10000 /HEAP:0x10000,0x10000 /ALIGN:512 /MERGE:.rdata=.text /NXCOMPAT /DYNAMICBASE /SUBSYSTEM:WINDOWS,5.01 kernel32.lib user32.lib gdi32.lib shell32.lib iphlpapi.lib ws2_32.lib psapi.lib delayimp.lib
```

**Note:** Use "x64 Native Tools Command Prompt" for 64-bit builds.

### Running

Simply launch `latency_tray_trimmed.exe`. The icon will appear in your system tray showing the current latency.

- **Left-click**: No action (minimal design)
- **Right-click**: Context menu to:
  - Select different latency targets
  - View current selection (checked item)
  - Exit the application

## 📖 Usage

### Selecting a Target

1. Right-click the tray icon
2. Choose a target from the menu:
   - **Cloudflare DNS** - Fast, privacy-focused DNS (1.1.1.1)
   - **Google DNS** - Reliable global DNS (8.8.8.8)
   - **Quad9 DNS** - Security-focused DNS (9.9.9.9)
   - **OpenDNS** - Cisco's public DNS (208.67.222.222)
   - **IPv6 Targets** - Netflix/Fast.com servers for ISP peering quality

### Understanding the Display

- **Icon Text**: Shows current RTT in milliseconds, or `--` if unreachable
- **Tooltip**: Displays:
  - Target name
  - Current latency
  - `[IPv6]` indicator for IPv6 targets

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

- **Single Worker Thread** - 32KB stack, handles all network operations
- **Message-Only Window** - Lightweight window for tray icon callbacks
- **Win32/ICMP APIs** - Native Windows networking (no listening sockets)
- **Dynamic CRT** - Shared MSVCRT.dll for minimal memory footprint
- **Memory Trimming** - Automatic working set reduction every 10 seconds
- **No Elevation Required** - Runs at user privilege level

## 📁 Project Structure

```
.
├── latency_tray_trimmed.cpp    # Main source file (ultra-optimized)
├── build_trimmed.bat           # Build script
├── latency_tray_full.cpp       # Legacy v1.0 source (deprecated)
├── latency_tray_full.manifest  # Application manifest
├── SECURITY.md                 # Security documentation
├── CHANGELOG.md                # Version history
└── README.md                   # This file
```

## 🔧 Technical Details

### Requirements
- **Platform**: Windows 10 or later
- **Dependencies**: Windows SDK (iphlpapi.lib, ws2_32.lib, gdi32.lib, user32.lib, shell32.lib)
- **Privileges**: User-level (no administrator rights required)
- **Network**: ICMP echo requests (outbound only, no listening sockets)

### Performance Metrics

| Metric | Value |
|--------|-------|
| **Memory Usage** | **300-900KB** |
| **Executable Size** | **~18KB** |
| **CPU Usage** | <0.1% |
| **Update Interval** | 1 second |
| **Thread Count** | 2 |
| **Network Overhead** | 32 bytes/sec |

### Supported IP Targets

| Target | IP Address | Protocol | Description |
|--------|-----------|----------|-------------|
| Cloudflare DNS | 1.1.1.1 | IPv4 | Primary Cloudflare resolver |
| Cloudflare Alt | 1.0.0.1 | IPv4 | Secondary Cloudflare resolver |
| Google DNS | 8.8.8.8 | IPv4 | Primary Google resolver |
| Google Alt | 8.8.4.4 | IPv4 | Secondary Google resolver |
| Quad9 DNS | 9.9.9.9 | IPv4 | Security-focused resolver |
| OpenDNS | 208.67.222.222 | IPv4 | Cisco's public DNS |
| Fast.com (Pittsburgh) | 2a00:86c0:2054:2054::167 | IPv6 | Netflix edge server |
| Fast.com (Ashburn) | 2a00:86c0:2063:2063::135 | IPv6 | Netflix edge server |

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

