# Security Hardening - Latency Tray Icon

This document describes the security mitigations implemented in the latency tray icon application.

## Runtime Process Mitigation Policies

The `HardenProcess()` function applies the following Windows runtime security policies:

- **Heap Termination on Corruption**: Process terminates if heap corruption detected
- **Secure DLL Search**: Only loads DLLs from System32 and user directories
- **Strict Handle Checks**: Invalid handle access causes immediate termination
- **No Dynamic Code**: Prevents JIT compilation and dynamic code execution
- **Microsoft-Signed DLLs Only**: Only loads DLLs signed by Microsoft
- **Extension Points Disabled**: Blocks AppInit DLLs and Winlogon hooks
- **ASLR Enforced**: Address Space Layout Randomization always enabled
- **DEP Enforced**: Data Execution Prevention permanently enabled
- **Control Flow Guard**: Hardware-assisted control flow protection
- **No Child Processes**: Prevents spawning child processes

## Compiler & Linker Mitigations

### Recommended Secure Build Commands

**For x64 (64-bit):**
```cmd
cl /O2 /Oi /Gy /GL /MT /EHsc /guard:cf /Qspectre /GS /sdl /W4 latency_tray_full.cpp /link /LTCG /OPT:REF /OPT:ICF /NXCOMPAT /DYNAMICBASE /HIGHENTROPYVA iphlpapi.lib ws2_32.lib gdi32.lib user32.lib shell32.lib /MANIFESTFILE:latency_tray_full.manifest
```

**For x86 (32-bit):**
```cmd
cl /O2 /Oi /Gy /GL /MT /EHsc /guard:cf /Qspectre /GS /sdl /W4 /SAFESEH latency_tray_full.cpp /link /LTCG /OPT:REF /OPT:ICF /NXCOMPAT /DYNAMICBASE iphlpapi.lib ws2_32.lib gdi32.lib user32.lib shell32.lib /MANIFESTFILE:latency_tray_full.manifest
```

**Note:** Use "x64 Native Tools Command Prompt" for 64-bit builds. `/HIGHENTROPYVA` is x64-only.

### Security Flags Explained

- `/guard:cf` - Control Flow Guard
- `/Qspectre` - Spectre variant 1 mitigations
- `/GS` - Stack buffer overflow protection
- `/sdl` - Additional security checks
- `/W4` - High warning level
- `/NXCOMPAT` - DEP/NX compatibility
- `/DYNAMICBASE` - ASLR
- `/HIGHENTROPYVA` - 64-bit ASLR with high entropy
- `/SAFESEH` - Safe exception handling (x86 only, omit for x64)
- `/MT` - Static CRT (no external DLL dependencies)
- `/LTCG` - Link-time code generation
- `/OPT:REF /OPT:ICF` - Optimize unused code and identical functions

## Application Manifest

The manifest file (`latency_tray_full.manifest`) enforces:

- **asInvoker**: Runs at user privilege level (no elevation)
- **uiAccess="false"**: Prevents UI privilege escalation
- **DPI Aware**: Sharp icon rendering on high-DPI displays
- **Windows 10+ compatibility**: Uses supported OS declarations

## Code-Level Security

### Input Validation

- All string operations use secure functions (`strncpy_s`, `wcsncpy_s`, `swprintf_s`)
- Bounds checking on all array accesses
- IP address string length validation
- Icon text length limits (max 8 characters)

### Resource Management

- Thread-safe icon handle tracking
- Icons only destroyed after successful push to Explorer
- Proper cleanup on all exit paths
- No resource leaks

### Data Handling

- Rolling averages cleared after 5 consecutive ping failures (prevents stale data)
- No persistent storage of network data
- No logging of PII or sensitive information
- IP targets compile-time only (no user-supplied strings)

### Network Security

- Uses Windows ICMP APIs (no listening sockets)
- No inbound connections
- No outbound connections except ICMP echo requests
- ICMP payload is fixed pattern, not user data

## Operational Security Recommendations

1. **Deployment**: Deploy from secure, non-user-writable directories
2. **WDAC/AppLocker**: Configure allow-listing by file hash
3. **No Auto-Update**: Avoid automatic updates; if added, enforce HTTPS and signature verification

## Notes on Integrity Levels

The code currently runs at Medium Integrity Level (default). Running at Low Integrity Level is possible but may interfere with ICMP APIs. Test thoroughly before enabling.

## Security Posture Summary

- **Code Security**: Safe string handling, no inbound IPC, no elevated privileges
- **Compiler Security**: CFG, DEP, ASLR, stack protection enabled
- **Runtime Security**: Dynamic code, extension points, and child process creation disabled
- **Manifest Security**: Non-elevated, no UIAccess, DPI aware
- **Deployment Security**: Allow-listed (recommended), not writable by normal users (recommended)
- **Operational Posture**: No open sockets, minimal privileges, no persistence beyond tray icon

