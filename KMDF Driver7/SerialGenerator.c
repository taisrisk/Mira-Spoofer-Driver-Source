#include "SerialGenerator.h"
#include <ntstrsafe.h>

// Global SMBIOS backup
static PVOID g_SMBIOSBackup = NULL;
static ULONG g_SMBIOSBackupSize = 0;
static PVOID g_SMBIOSTableAddress = NULL;
static ULONG g_SMBIOSTableSize = 0;

//
// AdvancedHash - Enhanced hash function with multiple rounds
//
UINT64 AdvancedHash(_In_ UINT64 Value, _In_ UINT32 Rounds)
{
    for (UINT32 round = 0; round < Rounds; round++) {
        Value ^= (Value >> 33);
        Value *= 0xff51afd7ed558ccdULL;
        Value ^= (Value >> 33);
        Value *= 0xc4ceb9fe1a85ec53ULL;
        Value ^= (Value >> 33);
        
        // Mix in round number to prevent cycles
        Value ^= ((UINT64)round * 0x9E3779B97F4A7C15ULL);
    }
    
    return Value;
}

//
// GenerateSecureEntropy - Generate entropy from hardware sources
//
NTSTATUS GenerateSecureEntropy(_Out_ PUINT64 EntropyValue)
{
    LARGE_INTEGER perfCounter, systemTime;
    
    // Query multiple hardware entropy sources
    KeQueryPerformanceCounter(&perfCounter);
    KeQuerySystemTime(&systemTime);
    
    // Mix entropy sources
    *EntropyValue = perfCounter.QuadPart ^ systemTime.QuadPart ^ (UINT64)(ULONG_PTR)EntropyValue;
    
    // Apply hashing to distribute entropy
    *EntropyValue = AdvancedHash(*EntropyValue, 3);
    
    return STATUS_SUCCESS;
}

//
// GenerateMotherboardSerial - Create manufacturer-specific motherboard serial
//
NTSTATUS GenerateMotherboardSerial(
    _In_ UINT64 Seed,
    _In_ MOTHERBOARD_VENDOR Vendor,
    _Out_ PCHAR Buffer,
    _In_ SIZE_T BufferSize
)
{
    NTSTATUS status;
    UINT64 hash = AdvancedHash(Seed, 5);
    
    // Extract date components
    UINT32 year = (UINT32)(hash % 10);  // Last digit of year
    UINT32 month = (UINT32)((hash >> 8) % 12) + 1;
    UINT64 sequence = (hash >> 16);
    
    // Month character encoding (1-9, A=Oct, B=Nov, C=Dec)
    CHAR monthChar;
    if (month >= 10) {
        monthChar = 'A' + (CHAR)(month - 10);
    } else {
        monthChar = '0' + (CHAR)month;
    }
    
    switch (Vendor) {
        case VENDOR_ASUS:
            // ASUS format: [Year][Month][10-digit sequence]
            // Example: 5B1234567890
            status = RtlStringCbPrintfA(Buffer, BufferSize,
                "%d%c%010llu",
                year, monthChar, sequence % 10000000000ULL);
            break;
            
        case VENDOR_MSI:
            // MSI format: 601-XXXX-XXXSBYYYMM
            // Example: 601-7C02-012SB2105
            status = RtlStringCbPrintfA(Buffer, BufferSize,
                "601-%04X-%03XSB%02d%02d",
                (USHORT)(hash & 0xFFFF),
                (USHORT)((hash >> 16) & 0xFFF),
                year, month);
            break;
            
        case VENDOR_GIGABYTE:
            // Gigabyte format: SNYYMMXXXXXX
            // Example: SN2511034567
            status = RtlStringCbPrintfA(Buffer, BufferSize,
                "SN%02d%02d%06llu",
                year, month, sequence % 1000000ULL);
            break;
            
        case VENDOR_ASROCK:
            // ASRock often has "To Be Filled By O.E.M."
            status = RtlStringCbPrintfA(Buffer, BufferSize,
                "To Be Filled By O.E.M.");
            break;
            
        case VENDOR_INTEL:
            // Intel format: [PREFIX][Sequence]
            // Example: JKF2224IR515000
            status = RtlStringCbPrintfA(Buffer, BufferSize,
                "JKF%04X%08llu",
                (USHORT)(hash & 0xFFFF),
                sequence % 100000000ULL);
            break;
            
        default:
            // Generic format with date encoding
            status = RtlStringCbPrintfA(Buffer, BufferSize,
                "%d%c%010llu",
                year, monthChar, sequence % 10000000000ULL);
            break;
    }
    
    return status;
}

//
// GenerateDiskSerial - Create realistic disk serial number
//
NTSTATUS GenerateDiskSerial(
    _In_ UINT64 Seed,
    _Out_ PCHAR Buffer,
    _In_ SIZE_T BufferSize
)
{
    UINT64 hash = AdvancedHash(Seed, 4);
    UINT32 brandSelect = (UINT32)(hash % 3);
    
    // Rotate between Samsung, WD, and Seagate formats
    switch (brandSelect) {
        case 0:
            // Samsung SSD format: S4NXXXXXXXXX
            return RtlStringCbPrintfA(Buffer, BufferSize,
                "S4NX%011llu",
                hash % 100000000000ULL);
                
        case 1:
            // Western Digital format: WD-XXXXXXXXXXXXX
            return RtlStringCbPrintfA(Buffer, BufferSize,
                "WD-WCC%012llu",
                hash % 1000000000000ULL);
                
        case 2:
        default:
            // Seagate format: XXXXXXXX
            return RtlStringCbPrintfA(Buffer, BufferSize,
                "%08llu%08llu",
                (hash % 100000000ULL),
                ((hash >> 32) % 100000000ULL));
    }
}

//
// GenerateBIOSSerial - Create BIOS serial (often blank or matches motherboard)
//
NTSTATUS GenerateBIOSSerial(
    _In_ UINT64 Seed,
    _In_ MOTHERBOARD_VENDOR Vendor,
    _Out_ PCHAR Buffer,
    _In_ SIZE_T BufferSize
)
{
    UINT64 hash = AdvancedHash(Seed, 3);
    
    // 50% chance of "Default string", 30% blank, 20% matches motherboard
    UINT32 choice = (UINT32)(hash % 10);
    
    if (choice < 5) {
        return RtlStringCbPrintfA(Buffer, BufferSize, "Default string");
    } else if (choice < 8) {
        Buffer[0] = '\0';  // Blank
        return STATUS_SUCCESS;
    } else {
        // Match motherboard serial format
        return GenerateMotherboardSerial(Seed, Vendor, Buffer, BufferSize);
    }
}

//
// GenerateChassisSerial - Create chassis serial number
//
NTSTATUS GenerateChassisSerial(
    _In_ UINT64 Seed,
    _Out_ PCHAR Buffer,
    _In_ SIZE_T BufferSize
)
{
    UINT64 hash = AdvancedHash(Seed, 3);
    UINT32 choice = (UINT32)(hash % 4);
    
    switch (choice) {
        case 0:
            return RtlStringCbPrintfA(Buffer, BufferSize, "Default string");
            
        case 1:
            return RtlStringCbPrintfA(Buffer, BufferSize, "To Be Filled By O.E.M.");
            
        case 2:
            return RtlStringCbPrintfA(Buffer, BufferSize, "Not Specified");
            
        case 3:
        default:
            // Dell-style format: ..CNXXXXXXXXBS006B.
            return RtlStringCbPrintfA(Buffer, BufferSize,
                "..CN%09lluBS006B.",
                hash % 1000000000ULL);
    }
}

//
// DetectHardwarePlatform - Detect CPU and motherboard vendor
//
NTSTATUS DetectHardwarePlatform(_Out_ PPLATFORM_INFO PlatformInfo)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING regPath, valueName;
    HANDLE hKey = NULL;
    UCHAR buffer[256];
    PKEY_VALUE_PARTIAL_INFORMATION keyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
    ULONG resultLength;
    
    RtlZeroMemory(PlatformInfo, sizeof(PLATFORM_INFO));
    
    // Default to generic
    PlatformInfo->Vendor = VENDOR_GENERIC;
    PlatformInfo->IsIntel = TRUE;
    
    // Detect CPU vendor
    RtlInitUnicodeString(&regPath, L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
    InitializeObjectAttributes(&objAttr, &regPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    
    status = ZwOpenKey(&hKey, KEY_QUERY_VALUE, &objAttr);
    if (NT_SUCCESS(status)) {
        RtlInitUnicodeString(&valueName, L"VendorIdentifier");
        status = ZwQueryValueKey(hKey, &valueName, KeyValuePartialInformation, keyInfo, sizeof(buffer), &resultLength);
        
        if (NT_SUCCESS(status)) {
            PWCHAR vendorId = (PWCHAR)keyInfo->Data;
            
            // Check for Intel vs AMD
            if (wcsstr(vendorId, L"GenuineIntel") != NULL) {
                PlatformInfo->IsIntel = TRUE;
                RtlStringCbCopyA(PlatformInfo->ChipsetModel, sizeof(PlatformInfo->ChipsetModel), "Intel");
            } else if (wcsstr(vendorId, L"AuthenticAMD") != NULL) {
                PlatformInfo->IsIntel = FALSE;
                RtlStringCbCopyA(PlatformInfo->ChipsetModel, sizeof(PlatformInfo->ChipsetModel), "AMD");
            }
        }
        
        ZwClose(hKey);
    }
    
    // Attempt to detect motherboard vendor via ACPI/SMBIOS registry entries
    // This is a simplified detection - real systems would enumerate PCI devices
    // For now, we'll use a hash-based selection for variety
    UINT64 entropy;
    GenerateSecureEntropy(&entropy);
    
    // Distribute vendors based on market share (approximate)
    UINT32 vendorSelect = (UINT32)(entropy % 100);
    
    if (vendorSelect < 30) {
        PlatformInfo->Vendor = VENDOR_ASUS;
        PlatformInfo->SubsysVendorId = 0x1043;
    } else if (vendorSelect < 50) {
        PlatformInfo->Vendor = VENDOR_GIGABYTE;
        PlatformInfo->SubsysVendorId = 0x1458;
    } else if (vendorSelect < 70) {
        PlatformInfo->Vendor = VENDOR_MSI;
        PlatformInfo->SubsysVendorId = 0x1462;
    } else if (vendorSelect < 85) {
        PlatformInfo->Vendor = VENDOR_ASROCK;
        PlatformInfo->SubsysVendorId = 0x1849;
    } else {
        PlatformInfo->Vendor = VENDOR_INTEL;
        PlatformInfo->SubsysVendorId = 0x8086;
    }
    
    KdPrint(("PCCleanup: Detected platform - CPU: %s, Vendor: %d\n",
        PlatformInfo->IsIntel ? "Intel" : "AMD", PlatformInfo->Vendor));
    
    return STATUS_SUCCESS;
}

//
// FindSMBIOSTable - Locate SMBIOS table in physical memory
//
NTSTATUS FindSMBIOSTable(_Out_ PVOID* TableAddress, _Out_ PULONG TableSize)
{
    NTSTATUS status = STATUS_SUCCESS;
    PHYSICAL_ADDRESS physAddr;
    PVOID mappedAddress = NULL;
    PSMBIOS_ENTRY_POINT entryPoint = NULL;
    ULONG scanSize = 0x10000; // 64KB BIOS area
    
    *TableAddress = NULL;
    *TableSize = 0;
    
    // SMBIOS entry point is typically in the range 0xF0000 - 0xFFFFF
    physAddr.QuadPart = 0xF0000;
    
    mappedAddress = MmMapIoSpace(physAddr, scanSize, MmNonCached);
    if (mappedAddress == NULL) {
        KdPrint(("PCCleanup: Failed to map BIOS memory area\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Search for "_SM_" anchor string
    for (ULONG offset = 0; offset < scanSize - sizeof(SMBIOS_ENTRY_POINT); offset += 16) {
        PUCHAR current = (PUCHAR)mappedAddress + offset;
        
        if (current[0] == '_' && current[1] == 'S' && current[2] == 'M' && current[3] == '_') {
            entryPoint = (PSMBIOS_ENTRY_POINT)current;
            
            // Validate entry point
            if (entryPoint->TableAddress != 0 && entryPoint->TableLength > 0) {
                PHYSICAL_ADDRESS tablePhysAddr;
                tablePhysAddr.QuadPart = entryPoint->TableAddress;
                
                // Map the actual SMBIOS table
                *TableAddress = MmMapIoSpace(tablePhysAddr, entryPoint->TableLength, MmNonCached);
                if (*TableAddress != NULL) {
                    *TableSize = entryPoint->TableLength;
                    g_SMBIOSTableAddress = *TableAddress;
                    g_SMBIOSTableSize = *TableSize;
                    
                    KdPrint(("PCCleanup: Found SMBIOS table at 0x%08X, size %d bytes\n", 
                        entryPoint->TableAddress, *TableSize));
                    status = STATUS_SUCCESS;
                } else {
                    KdPrint(("PCCleanup: Failed to map SMBIOS table\n"));
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
                
                break;
            }
        }
    }
    
    MmUnmapIoSpace(mappedAddress, scanSize);
    
    if (*TableAddress == NULL) {
        KdPrint(("PCCleanup: SMBIOS table not found\n"));
        status = STATUS_NOT_FOUND;
    }
    
    return status;
}

//
// PatchSMBIOSString - Modify a string in SMBIOS table
//
NTSTATUS PatchSMBIOSString(
    _In_ PVOID TableBase,
    _In_ ULONG TableSize,
    _In_ UCHAR Type,
    _In_ UCHAR StringIndex,
    _In_ PCHAR NewValue)
{
    PUCHAR current = (PUCHAR)TableBase;
    PUCHAR end = current + TableSize;
    
    while (current + sizeof(SMBIOS_STRUCTURE_HEADER) < end) {
        PSMBIOS_STRUCTURE_HEADER header = (PSMBIOS_STRUCTURE_HEADER)current;
        
        if (header->Type == 127) { // End of table
            break;
        }
        
        if (header->Type == Type) {
            // Find the string section (after the formatted section)
            PUCHAR stringSection = current + header->Length;
            UCHAR currentStringIndex = 1;
            
            // Navigate to the target string
            while (stringSection < end && *stringSection != 0) {
                if (currentStringIndex == StringIndex) {
                    // Found target string - calculate length
                    SIZE_T oldLength = strlen((PCHAR)stringSection);
                    SIZE_T newLength = strlen(NewValue);
                    
                    // Overwrite in place (risky but direct)
                    if (newLength <= oldLength) {
                        RtlCopyMemory(stringSection, NewValue, newLength);
                        RtlZeroMemory(stringSection + newLength, oldLength - newLength);
                        
                        KdPrint(("PCCleanup: Patched SMBIOS Type %d String %d to: %s\n", 
                            Type, StringIndex, NewValue));
                        return STATUS_SUCCESS;
                    } else {
                        KdPrint(("PCCleanup: New string too long for SMBIOS patch\n"));
                        return STATUS_BUFFER_TOO_SMALL;
                    }
                }
                
                // Move to next string
                while (*stringSection != 0 && stringSection < end) {
                    stringSection++;
                }
                stringSection++; // Skip null terminator
                currentStringIndex++;
            }
            
            return STATUS_NOT_FOUND;
        }
        
        // Skip to next structure
        current += header->Length;
        
        // Skip string section (double null terminated)
        while (current < end && !(*current == 0 && *(current + 1) == 0)) {
            current++;
        }
        current += 2; // Skip double null
    }
    
    KdPrint(("PCCleanup: SMBIOS Type %d not found\n", Type));
    return STATUS_NOT_FOUND;
}

//
// BackupSMBIOSTables - Save original SMBIOS data
//
NTSTATUS BackupSMBIOSTables(VOID)
{
    NTSTATUS status;
    
    if (g_SMBIOSBackup != NULL) {
        KdPrint(("PCCleanup: SMBIOS already backed up\n"));
        return STATUS_SUCCESS;
    }
    
    status = FindSMBIOSTable(&g_SMBIOSTableAddress, &g_SMBIOSTableSize);
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to find SMBIOS table for backup - 0x%x\n", status));
        return status;
    }
    
    g_SMBIOSBackup = ExAllocatePoolWithTag(NonPagedPool, g_SMBIOSTableSize, 'SMBS');
    if (g_SMBIOSBackup == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlCopyMemory(g_SMBIOSBackup, g_SMBIOSTableAddress, g_SMBIOSBackupSize);
    g_SMBIOSBackupSize = g_SMBIOSTableSize;
    
    KdPrint(("PCCleanup: SMBIOS table backed up (%d bytes)\n", g_SMBIOSBackupSize));
    return STATUS_SUCCESS;
}

//
// RestoreSMBIOSTables - Restore original SMBIOS data
//
NTSTATUS RestoreSMBIOSTables(VOID)
{
    if (g_SMBIOSBackup == NULL || g_SMBIOSTableAddress == NULL) {
        KdPrint(("PCCleanup: No SMBIOS backup to restore\n"));
        return STATUS_SUCCESS;
    }
    
    RtlCopyMemory(g_SMBIOSTableAddress, g_SMBIOSBackup, g_SMBIOSBackupSize);
    
    ExFreePoolWithTag(g_SMBIOSBackup, 'SMBS');
    g_SMBIOSBackup = NULL;
    g_SMBIOSBackupSize = 0;
    
    KdPrint(("PCCleanup: SMBIOS table restored\n"));
    return STATUS_SUCCESS;
}

//
// ApplySMBIOSChanges - Apply all SMBIOS patches
//
NTSTATUS ApplySMBIOSChanges(_In_ PSYSTEM_PROFILE Profile)
{
    NTSTATUS status;
    PVOID tableAddress = NULL;
    ULONG tableSize = 0;
    
    // Backup original SMBIOS tables if not already done
    status = BackupSMBIOSTables();
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    tableAddress = g_SMBIOSTableAddress;
    tableSize = g_SMBIOSTableSize;
    
    if (tableAddress == NULL) {
        KdPrint(("PCCleanup: SMBIOS table address is NULL\n"));
        return STATUS_UNSUCCESSFUL;
    }
    
    // Patch BIOS Serial (Type 0, String 1)
    PatchSMBIOSString(tableAddress, tableSize, SMBIOS_TYPE_BIOS, 1, Profile->biosSerial);
    
    // Patch System Serial (Type 1, String 1)
    PatchSMBIOSString(tableAddress, tableSize, SMBIOS_TYPE_SYSTEM, 1, Profile->chassisSerial);
    
    // Patch System UUID (Type 1, offset 8)
    // Note: UUID is binary, not a string - would need more complex patching
    
    // Patch Baseboard Serial (Type 2, String 1)
    PatchSMBIOSString(tableAddress, tableSize, SMBIOS_TYPE_BASEBOARD, 1, Profile->motherboardSerial);
    
    // Patch Chassis Serial (Type 3, String 1)
    PatchSMBIOSString(tableAddress, tableSize, SMBIOS_TYPE_CHASSIS, 1, Profile->chassisSerial);
    
    KdPrint(("PCCleanup: SMBIOS changes applied\n"));
    return STATUS_SUCCESS;
}
