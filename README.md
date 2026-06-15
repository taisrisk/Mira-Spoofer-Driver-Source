# Mira Hardware Spoofer

<div align="center">

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)
![Driver](https://img.shields.io/badge/driver-KMDF-orange.svg)

**Advanced Hardware Identity Modification System**

A sophisticated Windows kernel-mode driver and user-mode application for temporary, reversible hardware identifier modification.

[Features](#features) • [Installation](#installation) • [Usage](#usage) • [Technical Details](#technical-details) • [Safety](#safety)

</div>

---

## Overview

Mira is a professional-grade hardware spoofing solution that provides temporary modification of system hardware identifiers through a combination of registry manipulation and SMBIOS memory patching. Unlike permanent modification tools, Mira ensures all changes are fully reversible and automatically cleaned up.

**Key Capabilities:**
- Registry-based identifier modification (Machine GUID, Product ID, Hardware Profile GUID)
- SMBIOS firmware table patching (Motherboard Serial, BIOS Serial, System UUID)
- Network adapter MAC address spoofing
- Manufacturer-specific serial number generation (ASUS, MSI, Gigabyte, ASRock, Intel)
- Deterministic ID generation with custom seed support
- Thread-safe kernel operations with automatic backup/restore

---

## Features

### 🔒 **Kernel-Mode Driver (KMDFDriver7.sys)**

- **KMDF Framework**: Built on Windows Kernel-Mode Driver Framework for stability and compatibility
- **Thread-Safe Operations**: WDF wait lock synchronization prevents race conditions
- **Automatic Backup**: Original values stored securely in non-paged memory
- **Registry Modification**: Direct kernel-mode registry access for system-level changes
- **SMBIOS Patching**: Physical memory scanning and modification of firmware tables
- **MAC Address Spoofing**: Enumerates and modifies all network adapters (up to 64 devices)
- **Auto-Cleanup**: Restores original state on driver unload or system reboot

### 💻 **User-Mode Application (TEMP_CONSOLE_LOADER.exe)**

- **Interactive CLI**: Full-featured command-line interface with intuitive menus
- **Service Management**: Automatic driver installation, loading, and unloading
- **Seed Customization**: Multiple seed generation methods (auto, timestamp, custom, preset)
- **Real-Time Status**: Driver health monitoring and connection verification
- **Profile Display**: Detailed view of modified and original identifiers
- **Error Handling**: Comprehensive diagnostics with troubleshooting guidance

### 🎯 **Hardware Identifiers Modified**

| Component | Modification Type | Tools Affected |
|-----------|------------------|----------------|
| Machine GUID | Registry | Windows Activation, .NET Apps |
| Hardware Profile GUID | Registry | System Configuration |
| Product ID | Registry | Software Licensing |
| Volume GUID | Registry | Disk Management |
| System UUID | Registry + SMBIOS | WMI Queries |
| BIOS Serial | Registry + SMBIOS | Hardware Info Tools |
| Motherboard Serial | Registry + SMBIOS | System Diagnostics |
| Chassis Serial | Registry + SMBIOS | OEM Software |
| MAC Address | Registry | Network Identification |

### 🏭 **Manufacturer-Specific Generation**

The driver intelligently detects CPU and motherboard vendors to generate realistic serial numbers:

- **ASUS**: Format `[Year][Month][10-digit sequence]` (e.g., `5B1234567890`)
- **MSI**: Format `601-XXXX-XXXSBYYYMM` (e.g., `601-7C02-012SB2105`)
- **Gigabyte**: Format `SNYYMMXXXXXX` (e.g., `SN2511034567`)
- **ASRock**: Realistic "To Be Filled By O.E.M." strings
- **Intel**: Format `JKF[Sequence]` (e.g., `JKF2224IR515000`)
- **Generic**: Date-encoded fallback format

### 🔐 **Security & Safety**

- **Administrator Required**: Enforces privilege checks before execution
- **Test Signing Mode**: Requires Windows test signing for driver loading
- **Reversible Changes**: All modifications backed up and restorable
- **Memory Security**: Sensitive data cleared with `RtlSecureZeroMemory`
- **No Persistence**: Automatic restoration on reboot or driver unload
- **Comprehensive Logging**: Kernel debug output via `DbgPrint`/`KdPrint`

---

## Installation

### Prerequisites

1. **Windows Operating System**
   - Windows 10/11 (64-bit recommended)
   - Administrator privileges required

2. **Test Signing Enabled**
   ```cmd
   bcdedit /set testsigning on
   ```
   **⚠️ Reboot required after enabling test signing**

3. **Visual C++ Redistributable** (for user-mode application)

### Setup Steps

1. **Download Release Package**
   - Extract `KMDFDriver7.sys` and `TEMP_CONSOLE_LOADER.exe` to the same directory

2. **Enable Test Signing**
   - Open Command Prompt as Administrator
   - Run: `bcdedit /set testsigning on`
   - Restart your computer

3. **Run Application**
   - Right-click `TEMP_CONSOLE_LOADER.exe`
   - Select "Run as administrator"

---

## Usage

### Quick Start

1. **Launch the application** as Administrator
2. **Driver loads automatically** on first run
3. **Select option 1** to apply a temporary profile
4. **Choose seed generation method** (auto-generate recommended)
5. **Verify changes** in the displayed profile summary

### Main Menu Options

```
┌──────────────────────────────────────────────┐
│  1. Apply Temporary Profile                 │
│  2. Restore Original Identifiers             │
│  3. Check Driver Status                      │
│  4. Display Current Configuration            │
│  5. Restart Driver                           │
│  6. Unload Driver and Exit                   │
│  7. Exit (Keep Driver Loaded)                │
└──────────────────────────────────────────────┘
```

### Seed Generation Options

- **Auto-Generate**: Uses high-resolution performance counter (most random)
- **Timestamp**: Current system time as seed (reproducible)
- **Custom**: Enter your own hex seed value (for deterministic profiles)
- **Preset**: Test seed `0xDEADBEEFCAFEBABE` (for testing)

### Verification

Check modified registry values:
```powershell
Get-ItemProperty HKLM:\SOFTWARE\Microsoft\Cryptography
Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\IDConfigDB\Hardware Profiles\0001"
```

Check SMBIOS (will show modified values):
```powershell
Get-WmiObject Win32_ComputerSystemProduct | Select IdentifyingNumber, UUID
Get-WmiObject Win32_BIOS | Select SerialNumber
```

**Note**: Tools like CPU-Z, HWiNFO, and AIDA64 read directly from firmware and may show original values depending on implementation.

---

## Technical Details

### Architecture

```
┌─────────────────────────────────────────────────┐
│           User-Mode Application                 │
│        (TEMP_CONSOLE_LOADER.exe)                │
│  ┌──────────────────────────────────────────┐   │
│  │ DriverLoader | DriverController          │   │
│  │ Service Mgmt | IOCTL Communication       │   │
│  └────────────────────┬─────────────────────┘   │
└────────────────────────┼─────────────────────────┘
                         │ DeviceIoControl
                         ▼
┌─────────────────────────────────────────────────┐
│          Kernel-Mode Driver                     │
│           (KMDFDriver7.sys)                     │
│  ┌──────────────────────────────────────────┐   │
│  │ Driver.c       │ IOCTL Handler          │   │
│  │ SerialGen.c    │ SMBIOS Patcher         │   │
│  │ GuidGen.c      │ Registry Manager       │   │
│  └──────────────────────────────────────────┘   │
│           WDF Framework Layer                   │
└────────────────────┬────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────┐
│           Windows Registry                      │
│  • Cryptography\MachineGuid                     │
│  • IDConfigDB\Hardware Profiles                 │
│  • CurrentVersion\ProductId                     │
│  • HARDWARE\DESCRIPTION\System\BIOS             │
│  • Control\Class\{Network Adapters}             │
└─────────────────────────────────────────────────┘
```

### IOCTL Communication

The driver exposes three control codes:

| IOCTL | Code | Purpose |
|-------|------|---------|
| `IOCTL_APPLY_TEMP_PROFILE` | `0x800` | Generate and apply new identity |
| `IOCTL_RESTORE_PROFILE` | `0x801` | Restore original values |
| `IOCTL_GET_STATUS` | `0x802` | Check driver health |

### Registry Modifications

Modified registry locations:
```
HKLM\SOFTWARE\Microsoft\Cryptography
  └─ MachineGuid

HKLM\SYSTEM\CurrentControlSet\Control\IDConfigDB\Hardware Profiles\0001
  └─ HwProfileGuid

HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion
  └─ ProductId

HKLM\HARDWARE\DESCRIPTION\System\BIOS
  ├─ BIOSSerialNumber
  ├─ BaseBoardSerialNumber
  ├─ SystemSerialNumber
  └─ SystemUUID

HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E972-E325-11CE-BFC1-08002BE10318}\####
  └─ NetworkAddress
```

### SMBIOS Patching

The driver locates and modifies SMBIOS tables in physical memory:

- **Entry Point**: Scans `0xF0000 - 0xFFFFF` for `_SM_` anchor
- **Table Types Modified**:
  - Type 0 (BIOS Information)
  - Type 1 (System Information)
  - Type 2 (Baseboard Information)
  - Type 3 (Chassis Information)
- **Backup**: Original tables stored in non-paged pool memory
- **Restore**: Automatic on driver cleanup

### Limitations

**What This Modifies:**
- Windows registry identifiers
- SMBIOS memory tables (runtime only)
- Network adapter registry settings
- WMI query results (partially)

**What This Does NOT Modify:**
- Hardware firmware (BIOS/UEFI)
- TPM attestation values
- CPU serial numbers
- Secure Boot measurements
- Hardware DRM systems

**Detection Resistance:**
- ✅ Most software using Windows API
- ✅ .NET applications (System.Management)
- ✅ Registry-based fingerprinting
- ⚠️ Tools reading SMBIOS directly (may vary)
- ❌ Physical firmware readers
- ❌ TPM-based attestation

---

## Safety

### Automatic Safeguards

1. **Backup Before Modify**: All original values saved before any changes
2. **Thread-Safe**: WDF wait locks prevent concurrent access issues
3. **Atomic Operations**: Changes applied or rolled back completely
4. **Auto-Restore**: Driver cleanup automatically restores original state
5. **Reboot Protection**: All changes reset on system restart

### Best Practices

- ✅ Always run as Administrator
- ✅ Enable test signing before use
- ✅ Close the application properly (Option 6)
- ✅ Keep driver and loader in the same directory
- ✅ Test in a virtual machine first

- ❌ Don't modify system files manually
- ❌ Don't leave driver loaded permanently
- ❌ Don't use with anti-cheat protected software
- ❌ Don't disable important security features

### Recovery

If the system becomes unstable:

1. **Restart Computer**: All changes automatically revert
2. **Disable Test Signing**: `bcdedit /set testsigning off`
3. **Safe Mode**: Boot into Safe Mode to remove driver service
4. **Registry Backup**: Use System Restore if needed

---

## Legitimate Use Cases

This tool is designed for:

- 🔬 **Security Research**: Testing fingerprinting and tracking mechanisms
- 🧪 **Software Testing**: Validating application behavior with different hardware IDs
- 🔐 **Privacy Auditing**: Understanding what identifiers applications collect
- 📚 **Educational Purposes**: Learning kernel-mode driver development
- 🖥️ **Forensic Analysis**: Creating isolated testing environments

**⚠️ NOT for:**
- Bypassing software licensing (illegal)
- Evading anti-cheat systems (violates ToS)
- Hardware ban circumvention (unethical)
- Fraudulent activities (criminal)

---

## Building from Source

### Requirements

- Visual Studio 2019/2022
- Windows Driver Kit (WDK) 10.0.26100.0
- C++14 compiler

### Build Steps

1. Open `KMDF Driver7.sln` in Visual Studio
2. Select configuration (Debug/Release, x64)
3. Build Solution (F7)
4. Outputs:
   - `KMDFDriver7.sys` (kernel driver)
   - `TEMP_CONSOLE_LOADER.exe` (user application)

---

## Troubleshooting

### Driver Loading Failed

**Symptom**: "Failed to start driver. Error: [code]"

**Solutions**:
1. Verify test signing is enabled: `bcdedit /enum {current}`
2. Ensure you're running as Administrator
3. Check Windows Event Viewer → System logs
4. Reboot after enabling test signing
5. Disable Secure Boot in BIOS (if applicable)

### Connection Failed

**Symptom**: "Driver device not found"

**Solutions**:
1. Verify driver loaded: `sc query PCCleanupDriver`
2. Check DebugView for kernel messages
3. Restart the driver (Option 5)
4. Reinstall by selecting Option 6 then relaunch

### Changes Not Visible

**Symptom**: Hardware tools still show original values

**Solutions**:
1. Some tools read directly from firmware (expected)
2. Verify registry changes with PowerShell commands
3. Restart applications querying hardware info
4. Check if tool uses WMI (should see changes)

---

## FAQ

**Q: Is this safe to use?**
A: Yes, when used properly. All changes are temporary and automatically reversed. Always test in a VM first.

**Q: Will this work with [specific software]?**
A: Depends on how the software queries hardware. Registry/WMI-based methods will see changes, direct firmware reads won't.

**Q: Can I use this to bypass bans?**
A: We do not support or condone using this tool to violate terms of service or evade legitimate restrictions.

**Q: Does this work on laptops?**
A: Yes, but laptop OEM software may detect discrepancies between registry and firmware values.

**Q: Will this survive a reboot?**
A: No. All changes are intentionally temporary and reset on reboot for safety.

---

## Contributing

This project is currently in maintenance mode. For bug reports or suggestions, please open an issue on the repository.

---

## License

MIT License - See LICENSE file for details

---

## Disclaimer

This software is provided for educational and research purposes only. Users are responsible for ensuring their use complies with all applicable laws and regulations. The authors assume no liability for misuse or damage caused by this software.

**Use at your own risk. Always maintain backups of your system.**

---

## Credits

Developed by the Mira project team
Built with Windows Driver Framework (WDF)
Released: December 4, 2025

---

<div align="center">

**⭐ Star this repository if you find it useful!**

[Report Bug](../../issues) • [Request Feature](../../issues)

</div>
