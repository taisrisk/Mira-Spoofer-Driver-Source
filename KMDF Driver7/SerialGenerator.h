#pragma once

#include <ntddk.h>
#include "Driver.h"

// Motherboard manufacturer IDs
typedef enum _MOTHERBOARD_VENDOR {
    VENDOR_ASUS = 0,
    VENDOR_MSI = 1,
    VENDOR_GIGABYTE = 2,
    VENDOR_ASROCK = 3,
    VENDOR_INTEL = 4,
    VENDOR_GENERIC = 5
} MOTHERBOARD_VENDOR;

// Platform detection structure
typedef struct _PLATFORM_INFO {
    BOOLEAN IsIntel;                // TRUE=Intel, FALSE=AMD
    MOTHERBOARD_VENDOR Vendor;      // Motherboard manufacturer
    CHAR ChipsetModel[32];          // Chipset model string
    USHORT SubsysVendorId;          // PCI subsystem vendor ID
} PLATFORM_INFO, *PPLATFORM_INFO;

// SMBIOS structure headers
#pragma pack(push, 1)
typedef struct _SMBIOS_ENTRY_POINT {
    CHAR AnchorString[4];
    UCHAR Checksum;
    UCHAR Length;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT MaxStructureSize;
    UCHAR EntryPointRevision;
    CHAR FormattedArea[5];
    CHAR IntermediateAnchor[5];
    UCHAR IntermediateChecksum;
    USHORT TableLength;
    ULONG TableAddress;
    USHORT NumberOfStructures;
    UCHAR BCDRevision;
} SMBIOS_ENTRY_POINT, *PSMBIOS_ENTRY_POINT;

typedef struct _SMBIOS_STRUCTURE_HEADER {
    UCHAR Type;
    UCHAR Length;
    USHORT Handle;
} SMBIOS_STRUCTURE_HEADER, *PSMBIOS_STRUCTURE_HEADER;
#pragma pack(pop)

// SMBIOS Type definitions
#define SMBIOS_TYPE_BIOS 0
#define SMBIOS_TYPE_SYSTEM 1
#define SMBIOS_TYPE_BASEBOARD 2
#define SMBIOS_TYPE_CHASSIS 3

// Function prototypes
NTSTATUS DetectHardwarePlatform(_Out_ PPLATFORM_INFO PlatformInfo);
NTSTATUS GenerateMotherboardSerial(_In_ UINT64 Seed, _In_ MOTHERBOARD_VENDOR Vendor, _Out_ PCHAR Buffer, _In_ SIZE_T BufferSize);
NTSTATUS GenerateDiskSerial(_In_ UINT64 Seed, _Out_ PCHAR Buffer, _In_ SIZE_T BufferSize);
NTSTATUS GenerateBIOSSerial(_In_ UINT64 Seed, _In_ MOTHERBOARD_VENDOR Vendor, _Out_ PCHAR Buffer, _In_ SIZE_T BufferSize);
NTSTATUS GenerateChassisSerial(_In_ UINT64 Seed, _Out_ PCHAR Buffer, _In_ SIZE_T BufferSize);
UINT64 AdvancedHash(_In_ UINT64 Value, _In_ UINT32 Rounds);
NTSTATUS GenerateSecureEntropy(_Out_ PUINT64 EntropyValue);

// SMBIOS patching functions
NTSTATUS FindSMBIOSTable(_Out_ PVOID* TableAddress, _Out_ PULONG TableSize);
NTSTATUS PatchSMBIOSString(_In_ PVOID TableBase, _In_ ULONG TableSize, _In_ UCHAR Type, _In_ UCHAR StringIndex, _In_ PCHAR NewValue);
NTSTATUS ApplySMBIOSChanges(_In_ PSYSTEM_PROFILE Profile);
NTSTATUS BackupSMBIOSTables(VOID);
NTSTATUS RestoreSMBIOSTables(VOID);
