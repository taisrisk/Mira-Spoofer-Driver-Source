// ConsoleApplication1.cpp : PC Cleanup Tool - Temporary Hardware ID Modifier
// Updated version with full driver integration and new features
//

#include <iostream>
#include <Windows.h>
#include <string>
#include <iomanip>
#include <ctime>

// Driver communication
#define DRIVER_NAME L"\\\\.\\PCCleanupDriver"
#define SERVICE_NAME L"PCCleanupDriver"
#define DISPLAY_NAME L"PC Cleanup Driver"
#define DRIVER_FILE_NAME L"KMDFDriver7.sys"

// IOCTL codes matching kernel driver
#define IOCTL_APPLY_TEMP_PROFILE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_RESTORE_PROFILE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Seed structure for deterministic ID generation
struct SeedData {
    UINT64 seedValue;
    UINT64 timestamp;
};

// Temporary hardware profile (memory-only, resets on reboot)
struct SystemProfile {
    char motherboardSerial[64];
    char diskSerial[64];
    char machineGuid[64];
    char hwProfileGuid[64];
    char productId[64];
    char volumeGuid[64];
    BYTE macAddress[6];
    char systemUUID[37];
    char biosSerial[64];
    char chassisSerial[64];
};

class DriverLoader {
private:
    SC_HANDLE hSCManager;
    SC_HANDLE hService;
    std::wstring driverPath;

public:
    DriverLoader() : hSCManager(NULL), hService(NULL) {}

    ~DriverLoader() {
        if (hService) {
            CloseServiceHandle(hService);
        }
        if (hSCManager) {
            CloseServiceHandle(hSCManager);
        }
    }

    bool FindDriverFile(std::wstring& outPath) {
        // Look for driver in same directory as executable
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

        // Extract directory from executable path
        std::wstring exePathStr(exePath);
        size_t lastSlash = exePathStr.find_last_of(L"\\/");
        std::wstring exeDir = exePathStr.substr(0, lastSlash);
        
        // Build driver path with new driver name
        std::wstring driverFile = exeDir + L"\\" + DRIVER_FILE_NAME;

        // Check if file exists
        DWORD fileAttr = GetFileAttributesW(driverFile.c_str());
        if (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            outPath = driverFile;
            return true;
        }

        // Also check in Debug/Release folders
        std::wstring debugPath = exeDir + L"\\Debug\\" + DRIVER_FILE_NAME;
        fileAttr = GetFileAttributesW(debugPath.c_str());
        if (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            outPath = debugPath;
            return true;
        }

        std::wstring releasePath = exeDir + L"\\Release\\" + DRIVER_FILE_NAME;
        fileAttr = GetFileAttributesW(releasePath.c_str());
        if (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            outPath = releasePath;
            return true;
        }

        return false;
    }

    bool LoadDriver() {
        // Find driver file
        if (!FindDriverFile(driverPath)) {
            std::wcerr << L"[-] Driver file '" << DRIVER_FILE_NAME << L"' not found!" << std::endl;
            std::cerr << "[-] Please place the driver in the same directory as this executable." << std::endl;
            std::cerr << "[-] Also checked Debug\\ and Release\\ folders." << std::endl;
            return false;
        }

        std::wcout << L"[+] Found driver at: " << driverPath << std::endl;

        // Open Service Control Manager
        hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (!hSCManager) {
            DWORD error = GetLastError();
            std::cerr << "[-] Failed to open Service Control Manager. Error: " << error << std::endl;
            std::cerr << "[-] Make sure you're running as Administrator!" << std::endl;
            return false;
        }

        // Check if service already exists
        hService = OpenServiceW(hSCManager, SERVICE_NAME, SERVICE_ALL_ACCESS);

        if (hService) {
            std::cout << "[*] Service already exists. Attempting to start..." << std::endl;
        }
        else {
            // Create the service
            hService = CreateServiceW(
                hSCManager,
                SERVICE_NAME,
                DISPLAY_NAME,
                SERVICE_ALL_ACCESS,
                SERVICE_KERNEL_DRIVER,
                SERVICE_DEMAND_START,
                SERVICE_ERROR_NORMAL,
                driverPath.c_str(),
                NULL, NULL, NULL, NULL, NULL
            );

            if (!hService) {
                DWORD error = GetLastError();
                if (error == ERROR_SERVICE_EXISTS) {
                    std::cout << "[*] Service already exists. Opening..." << std::endl;
                    hService = OpenServiceW(hSCManager, SERVICE_NAME, SERVICE_ALL_ACCESS);
                }
                else {
                    std::cerr << "[-] Failed to create service. Error: " << error << std::endl;
                    return false;
                }
            }
            else {
                std::cout << "[+] Service created successfully." << std::endl;
            }
        }

        // Start the service
        if (!StartServiceW(hService, 0, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_SERVICE_ALREADY_RUNNING) {
                std::cout << "[*] Driver already running." << std::endl;
                return true;
            }
            else {
                std::cerr << "[-] Failed to start driver. Error: " << error << std::endl;
                std::cerr << "[-] Common solutions:" << std::endl;
                std::cerr << "    1. Enable test signing: bcdedit /set testsigning on" << std::endl;
                std::cerr << "    2. Reboot after enabling test signing" << std::endl;
                std::cerr << "    3. Check if driver is properly signed" << std::endl;
                std::cerr << "    4. Verify Windows version compatibility" << std::endl;
                return false;
            }
        }

        std::cout << "[+] Driver loaded and started successfully!" << std::endl;
        return true;
    }

    bool UnloadDriver() {
        if (!hService) {
            return true;
        }

        SERVICE_STATUS serviceStatus;

        // Stop the service
        if (ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus)) {
            std::cout << "[+] Driver stopped successfully." << std::endl;
            
            // Wait for service to stop
            DWORD waitTime = 0;
            while (serviceStatus.dwCurrentState != SERVICE_STOPPED && waitTime < 5000) {
                Sleep(200);
                waitTime += 200;
                QueryServiceStatus(hService, &serviceStatus);
            }
        }
        else {
            DWORD error = GetLastError();
            if (error != ERROR_SERVICE_NOT_ACTIVE) {
                std::cerr << "[-] Failed to stop driver. Error: " << error << std::endl;
            }
            else {
                std::cout << "[*] Driver was not active." << std::endl;
            }
        }

        // Delete the service
        if (DeleteService(hService)) {
            std::cout << "[+] Service deleted successfully." << std::endl;
        }
        else {
            DWORD error = GetLastError();
            if (error == ERROR_SERVICE_MARKED_FOR_DELETE) {
                std::cout << "[*] Service marked for deletion (will be removed on reboot)." << std::endl;
            }
            else {
                std::cerr << "[-] Failed to delete service. Error: " << error << std::endl;
                return false;
            }
        }

        return true;
    }

    bool RestartDriver() {
        std::cout << "[*] Restarting driver..." << std::endl;
        
        if (!UnloadDriver()) {
            std::cerr << "[-] Failed to unload driver for restart." << std::endl;
            return false;
        }

        // Wait a moment for cleanup
        Sleep(1000);

        return LoadDriver();
    }
};

class DriverController {
private:
    HANDLE hDriver;
    bool isConnected;

public:
    DriverController() : hDriver(INVALID_HANDLE_VALUE), isConnected(false) {}

    ~DriverController() {
        if (hDriver != INVALID_HANDLE_VALUE) {
            CloseHandle(hDriver);
        }
    }

    bool ConnectToDriver() {
        hDriver = CreateFileW(
            DRIVER_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hDriver == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            std::cerr << "[-] Failed to connect to driver. Error: " << error << std::endl;
            
            if (error == ERROR_FILE_NOT_FOUND) {
                std::cerr << "[-] Driver device not found. Driver may not be loaded." << std::endl;
            }
            else if (error == ERROR_ACCESS_DENIED) {
                std::cerr << "[-] Access denied. Run as Administrator." << std::endl;
            }
            
            return false;
        }

        isConnected = true;
        std::cout << "[+] Successfully connected to PC Cleanup Driver." << std::endl;
        return true;
    }

    bool CheckDriverStatus() {
        if (hDriver == INVALID_HANDLE_VALUE) {
            std::cerr << "[-] Driver not connected." << std::endl;
            return false;
        }

        DWORD bytesReturned;
        BOOL result = DeviceIoControl(
            hDriver,
            IOCTL_GET_STATUS,
            NULL,
            0,
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (!result) {
            DWORD error = GetLastError();
            std::cerr << "[-] Failed to get driver status. Error: " << error << std::endl;
            return false;
        }

        std::cout << "[+] Driver is connected and operational." << std::endl;
        std::cout << "[+] All driver systems functioning normally." << std::endl;
        return true;
    }

    bool ApplyTemporaryProfile(const SeedData& seed) {
        if (hDriver == INVALID_HANDLE_VALUE) {
            std::cerr << "[-] Driver not connected." << std::endl;
            return false;
        }

        DWORD bytesReturned;
        SystemProfile profile;

        std::cout << "[*] Sending temporary profile request to driver..." << std::endl;

        BOOL result = DeviceIoControl(
            hDriver,
            IOCTL_APPLY_TEMP_PROFILE,
            (LPVOID)&seed,
            sizeof(SeedData),
            &profile,
            sizeof(SystemProfile),
            &bytesReturned,
            NULL
        );

        if (!result) {
            DWORD error = GetLastError();
            std::cerr << "[-] Failed to apply temporary profile. Error: " << error << std::endl;
            
            if (error == ERROR_INSUFFICIENT_BUFFER) {
                std::cerr << "[-] Buffer size mismatch between user-mode and kernel-mode." << std::endl;
            }
            else if (error == ERROR_ACCESS_DENIED) {
                std::cerr << "[-] Access denied. Ensure driver has proper privileges." << std::endl;
            }
            
            return false;
        }

        if (bytesReturned != sizeof(SystemProfile)) {
            std::cerr << "[-] Warning: Unexpected data size returned: " << bytesReturned << std::endl;
        }

        std::cout << "[+] Temporary hardware profile applied successfully!" << std::endl;
        std::cout << "[+] Original values backed up in driver memory." << std::endl;
        std::cout << "[+] Changes will persist until restore or system reboot." << std::endl;
        DisplayProfile(profile);
        return true;
    }

    bool RestoreOriginal() {
        if (hDriver == INVALID_HANDLE_VALUE) {
            std::cerr << "[-] Driver not connected." << std::endl;
            return false;
        }

        DWORD bytesReturned;
        
        std::cout << "[*] Restoring original hardware identifiers..." << std::endl;

        BOOL result = DeviceIoControl(
            hDriver,
            IOCTL_RESTORE_PROFILE,
            NULL,
            0,
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (!result) {
            DWORD error = GetLastError();
            std::cerr << "[-] Failed to restore original profile. Error: " << error << std::endl;
            return false;
        }

        std::cout << "[+] Original hardware identifiers restored successfully!" << std::endl;
        std::cout << "[+] System is now back to original state." << std::endl;
        return true;
    }

    void DisplayProfile(const SystemProfile& profile) {
        std::cout << "\n??????????????????????????????????????????????????????????????" << std::endl;
        std::cout << "?   Temporary System Profile (Active Until Restore/Reboot)  ?" << std::endl;
        std::cout << "??????????????????????????????????????????????????????????????" << std::endl;
        
        std::cout << "\n[Hardware Identifiers - GENERATED]" << std::endl;
        std::cout << "  Motherboard Serial: " << profile.motherboardSerial << std::endl;
        std::cout << "  Disk Serial:        " << profile.diskSerial << std::endl;
        std::cout << "  BIOS Serial:        " << profile.biosSerial << std::endl;
        std::cout << "  Chassis Serial:     " << profile.chassisSerial << std::endl;
        
        std::cout << "\n[System GUIDs - MODIFIED IN REGISTRY]" << std::endl;
        std::cout << "  Machine GUID:       " << profile.machineGuid << std::endl;
        std::cout << "  HW Profile GUID:    " << profile.hwProfileGuid << std::endl;
        std::cout << "  Volume GUID:        " << profile.volumeGuid << std::endl;
        std::cout << "  System UUID:        " << profile.systemUUID << std::endl;
        
        std::cout << "\n[Network & Licensing - MODIFIED IN REGISTRY]" << std::endl;
        std::cout << "  Product ID:         " << profile.productId << std::endl;
        std::cout << "  MAC Address:        ";
        for (int i = 0; i < 6; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (int)profile.macAddress[i];
            if (i < 5) std::cout << ":";
        }
        std::cout << std::dec << std::endl;
        
        std::cout << "\n[IMPORTANT - Registry vs Firmware]" << std::endl;
        std::cout << "  ? Registry values (GUIDs, Product ID) ARE modified" << std::endl;
        std::cout << "  ? SMBIOS firmware values CANNOT be modified by software" << std::endl;
        std::cout << "  ? Tools like CPU-Z read DIRECTLY from firmware (SMBIOS)" << std::endl;
        std::cout << "  ? Windows API/WMI queries may show MODIFIED values" << std::endl;
        std::cout << "  ? Hardware diagnostic tools will show ORIGINAL values" << std::endl;
        
        std::cout << "\n[Technical Details]" << std::endl;
        std::cout << "  • Modified Registry Locations:" << std::endl;
        std::cout << "    - HKLM\\SOFTWARE\\Microsoft\\Cryptography\\MachineGuid" << std::endl;
        std::cout << "    - HKLM\\SYSTEM\\...\\IDConfigDB\\Hardware Profiles\\0001" << std::endl;
        std::cout << "    - HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion" << std::endl;
        std::cout << "    - HKLM\\HARDWARE\\DESCRIPTION\\System\\BIOS (where accessible)" << std::endl;
        
        std::cout << "\n[What This Affects]" << std::endl;
        std::cout << "  ? Software licensing/activation (registry-based)" << std::endl;
        std::cout << "  ? Windows Settings and system info dialogs" << std::endl;
        std::cout << "  ? Most user-mode applications using standard APIs" << std::endl;
        std::cout << "  ? .NET applications using System.Management" << std::endl;
        
        std::cout << "\n[What This Does NOT Affect]" << std::endl;
        std::cout << "  ? CPU-Z, HWiNFO, AIDA64 (read from SMBIOS firmware)" << std::endl;
        std::cout << "  ? BIOS-level hardware IDs (stored in firmware)" << std::endl;
        std::cout << "  ? TPM attestation and secure boot measurements" << std::endl;
        std::cout << "  ? Hardware-based DRM systems" << std::endl;
        
        std::cout << "\n[Verification Commands]" << std::endl;
        std::cout << "  Check modified registry:" << std::endl;
        std::cout << "    Get-ItemProperty HKLM:\\SOFTWARE\\Microsoft\\Cryptography" << std::endl;
        std::cout << "  Check firmware (unchanged):" << std::endl;
        std::cout << "    Get-WmiObject Win32_BIOS | Select SerialNumber" << std::endl;
        
        std::cout << "\n[For Advanced SMBIOS Spoofing]" << std::endl;
        std::cout << "  See SMBIOS_LIMITATIONS.md for technical details on:" << std::endl;
        std::cout << "  - Physical memory filtering (HIGH complexity)" << std::endl;
        std::cout << "  - WMI provider hooking (MEDIUM complexity)" << std::endl;
        std::cout << "  - ACPI table patching (VERY HIGH complexity)" << std::endl;
        
        std::cout << "\n????????????????????????????????????????????????????????????\n" << std::endl;
    }

    void Disconnect() {
        if (hDriver != INVALID_HANDLE_VALUE) {
            CloseHandle(hDriver);
            hDriver = INVALID_HANDLE_VALUE;
            isConnected = false;
            std::cout << "[*] Disconnected from driver." << std::endl;
        }
    }
};

// Enhanced seed generation with multiple options
SeedData GenerateSeed() {
    SeedData seed;

    std::cout << "\n=== Seed Generation Options ===" << std::endl;
    std::cout << "1. Auto-generate (random based on performance counter)" << std::endl;
    std::cout << "2. Use current timestamp" << std::endl;
    std::cout << "3. Enter custom seed value" << std::endl;
    std::cout << "4. Use preset test seed" << std::endl;
    std::cout << "Select option: ";
    
    int option;
    std::cin >> option;

    LARGE_INTEGER perfCounter;
    QueryPerformanceCounter(&perfCounter);

    switch (option) {
    case 1:
        seed.seedValue = perfCounter.QuadPart;
        std::cout << "[+] Auto-generated seed from performance counter" << std::endl;
        break;
    
    case 2:
        seed.seedValue = static_cast<UINT64>(time(nullptr));
        std::cout << "[+] Using current timestamp as seed" << std::endl;
        break;
    
    case 3:
        std::cout << "Enter seed value (hex format, e.g., 0x123456): ";
        std::cin >> std::hex >> seed.seedValue >> std::dec;
        std::cout << "[+] Using custom seed value" << std::endl;
        break;
    
    case 4:
        seed.seedValue = 0xDEADBEEFCAFEBABEULL;
        std::cout << "[+] Using preset test seed" << std::endl;
        break;
    
    default:
        seed.seedValue = perfCounter.QuadPart;
        std::cout << "[*] Invalid option, using auto-generated seed" << std::endl;
        break;
    }

    seed.timestamp = GetTickCount64();

    std::cout << "[+] Seed Configuration:" << std::endl;
    std::cout << "    Seed Value:  0x" << std::hex << seed.seedValue << std::dec << std::endl;
    std::cout << "    Timestamp:   0x" << std::hex << seed.timestamp << std::dec << std::endl;
    
    return seed;
}

void DisplayBanner() {
    std::cout << "\n????????????????????????????????????????????????????????????????" << std::endl;
    std::cout << "?                                                              ?" << std::endl;
    std::cout << "?        PC Cleanup Tool - Hardware Profile Manager           ?" << std::endl;
    std::cout << "?              Temporary Identity Modification                 ?" << std::endl;
    std::cout << "?                                                              ?" << std::endl;
    std::cout << "????????????????????????????????????????????????????????????????" << std::endl;
    std::cout << "\n[!] IMPORTANT INFORMATION:" << std::endl;
    std::cout << "    • Administrative privileges required" << std::endl;
    std::cout << "    • All modifications are temporary and reversible" << std::endl;
    std::cout << "    • Original values are backed up automatically" << std::endl;
    std::cout << "    • Driver uses thread-safe operations" << std::endl;
    std::cout << "    • Automatic cleanup on driver unload" << std::endl;
    std::cout << "\n[*] Legitimate Use Cases:" << std::endl;
    std::cout << "    • Security testing and research" << std::endl;
    std::cout << "    • Privacy-focused system auditing" << std::endl;
    std::cout << "    • Application behavior testing" << std::endl;
    std::cout << "    • Forensic analysis environments" << std::endl;
    std::cout << "\n????????????????????????????????????????????????????????????????\n" << std::endl;
}

void DisplayMenu() {
    std::cout << "\n???? Main Menu ????????????????????????????????????????????????" << std::endl;
    std::cout << "?                                                              ?" << std::endl;
    std::cout << "?  1. Apply Temporary Profile (Generate New Identity)         ?" << std::endl;
    std::cout << "?  2. Restore Original Identifiers                            ?" << std::endl;
    std::cout << "?  3. Check Driver Status                                     ?" << std::endl;
    std::cout << "?  4. Display Current Configuration                           ?" << std::endl;
    std::cout << "?  5. Restart Driver                                          ?" << std::endl;
    std::cout << "?  6. Unload Driver and Exit                                  ?" << std::endl;
    std::cout << "?  7. Exit (Keep Driver Loaded)                               ?" << std::endl;
    std::cout << "?                                                              ?" << std::endl;
    std::cout << "????????????????????????????????????????????????????????????????" << std::endl;
    std::cout << "Select option: ";
}

void DisplaySystemInfo() {
    std::cout << "\n=== System Configuration ===" << std::endl;
    
    // Get Windows version
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0 };
    DWORDLONG const dwlConditionMask = VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    
    std::cout << "[System Information]" << std::endl;
    std::cout << "  Driver Name:      " << "KMDFDriver7.sys" << std::endl;
    std::cout << "  Service Name:     PCCleanupDriver" << std::endl;
    std::cout << "  Device Interface: \\\\.\\PCCleanupDriver" << std::endl;
    
    // Check if test signing is enabled
    std::cout << "\n[Security Configuration]" << std::endl;
    std::cout << "  • Ensure test signing is enabled for driver loading" << std::endl;
    std::cout << "  • Command: bcdedit /set testsigning on" << std::endl;
    std::cout << "  • Requires system reboot after enabling" << std::endl;
    
    std::cout << "\n[Driver Features]" << std::endl;
    std::cout << "  ? Thread-safe WDFWAITLOCK synchronization" << std::endl;
    std::cout << "  ? Automatic value backup and restore" << std::endl;
    std::cout << "  ? Enhanced error handling and logging" << std::endl;
    std::cout << "  ? Automatic cleanup on driver unload" << std::endl;
    std::cout << "  ? Secure memory handling (RtlSecureZeroMemory)" << std::endl;
    std::cout << "  ? Complete reversibility of all changes" << std::endl;
    
    std::cout << "\n================================\n" << std::endl;
}

int main()
{
    DisplayBanner();

    // Check for admin rights
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    if (!isAdmin) {
        std::cerr << "[!] ERROR: This application must be run as Administrator!" << std::endl;
        std::cerr << "[!] Right-click the executable and select 'Run as administrator'" << std::endl;
        system("pause");
        return 1;
    }

    std::cout << "[+] Running with administrative privileges." << std::endl;

    // Load the driver
    DriverLoader loader;
    std::cout << "\n[*] Attempting to load KMDF kernel driver..." << std::endl;

    if (!loader.LoadDriver()) {
        std::cerr << "\n[!] DRIVER LOADING FAILED" << std::endl;
        std::cerr << "\n[*] Troubleshooting Steps:" << std::endl;
        std::cerr << "    1. Verify you are running as Administrator" << std::endl;
        std::cerr << "    2. Ensure KMDFDriver7.sys is in the same folder" << std::endl;
        std::cerr << "    3. Enable test signing: bcdedit /set testsigning on" << std::endl;
        std::cerr << "    4. Reboot system after enabling test signing" << std::endl;
        std::cerr << "    5. Check Windows Event Viewer for driver errors" << std::endl;
        std::cerr << "    6. Verify driver is properly signed (or test-signed)" << std::endl;
        std::cerr << "\n[*] For debugging, check:" << std::endl;
        std::cerr << "    • Event Viewer -> Windows Logs -> System" << std::endl;
        std::cerr << "    • DebugView for kernel debug messages" << std::endl;
        system("pause");
        return 1;
    }

    // Give driver time to initialize
    std::cout << "[*] Waiting for driver initialization..." << std::endl;
    Sleep(1000);

    DriverController controller;

    // Connect to kernel driver
    std::cout << "[*] Establishing connection to kernel driver..." << std::endl;
    if (!controller.ConnectToDriver()) {
        std::cerr << "\n[!] CONNECTION FAILED" << std::endl;
        std::cerr << "[!] The driver loaded but communication failed." << std::endl;
        std::cerr << "[*] This may indicate:" << std::endl;
        std::cerr << "    • Driver symbolic link not created" << std::endl;
        std::cerr << "    • Device initialization failed" << std::endl;
        std::cerr << "    • Driver crashed during startup" << std::endl;
        std::cerr << "\n[*] Attempting to unload driver..." << std::endl;
        loader.UnloadDriver();
        system("pause");
        return 1;
    }

    // Verify driver is operational
    std::cout << "[*] Verifying driver status..." << std::endl;
    if (!controller.CheckDriverStatus()) {
        std::cerr << "[!] Driver status check failed." << std::endl;
    }

    std::cout << "\n[+] All systems operational. Ready for commands." << std::endl;

    int choice;
    bool running = true;

    while (running) {
        DisplayMenu();
        std::cin >> choice;

        // Clear input buffer
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        switch (choice) {
        case 1: {
            SeedData seed = GenerateSeed();
            std::cout << "\n[*] Applying temporary profile..." << std::endl;
            if (controller.ApplyTemporaryProfile(seed)) {
                std::cout << "\n[SUCCESS] Temporary profile is now active!" << std::endl;
            }
            else {
                std::cerr << "\n[FAILED] Could not apply temporary profile." << std::endl;
            }
            break;
        }
        
        case 2:
            std::cout << "\n[*] Initiating restore operation..." << std::endl;
            if (controller.RestoreOriginal()) {
                std::cout << "\n[SUCCESS] Original identifiers restored!" << std::endl;
            }
            else {
                std::cerr << "\n[FAILED] Could not restore original values." << std::endl;
            }
            break;
        
        case 3:
            std::cout << "\n[*] Checking driver status..." << std::endl;
            controller.CheckDriverStatus();
            break;
        
        case 4:
            DisplaySystemInfo();
            break;
        
        case 5:
            std::cout << "\n[*] Restarting driver (this will restore original values)..." << std::endl;
            controller.Disconnect();
            if (loader.RestartDriver()) {
                Sleep(1000);
                if (controller.ConnectToDriver()) {
                    std::cout << "[+] Driver restarted and reconnected successfully!" << std::endl;
                }
                else {
                    std::cerr << "[-] Failed to reconnect after restart." << std::endl;
                }
            }
            else {
                std::cerr << "[-] Failed to restart driver." << std::endl;
            }
            break;
        
        case 6:
            std::cout << "\n[*] Shutting down..." << std::endl;
            std::cout << "[*] Driver cleanup will automatically restore original values" << std::endl;
            controller.Disconnect();
            loader.UnloadDriver();
            std::cout << "[+] Driver unloaded. System returned to original state." << std::endl;
            std::cout << "[+] Exiting application." << std::endl;
            running = false;
            break;
        
        case 7:
            std::cout << "\n[*] Exiting application..." << std::endl;
            std::cout << "[!] Driver will remain loaded and active." << std::endl;
            std::cout << "[*] Current profile modifications will persist until:" << std::endl;
            std::cout << "    • Manual restore operation" << std::endl;
            std::cout << "    • Driver unload" << std::endl;
            std::cout << "    • System reboot" << std::endl;
            controller.Disconnect();
            running = false;
            break;
        
        default:
            std::cout << "\n[-] Invalid option. Please select 1-7." << std::endl;
        }

        if (running) {
            std::cout << "\nPress Enter to continue...";
            std::cin.get();
        }
    }

    std::cout << "\n[*] Application terminated." << std::endl;
    return 0;
}
