#pragma once

#include <ntddk.h>
#include <wdf.h>

// IOCTL codes matching user-mode application
#define IOCTL_APPLY_TEMP_PROFILE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_RESTORE_PROFILE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Registry paths for temporary modification
#define REG_CRYPTOGRAPHY L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography"
#define REG_HWPROFILE L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\IDConfigDB\\Hardware Profiles\\0001"
#define REG_CURRENTVERSION L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

// Additional registry paths for SMBIOS/Hardware identifiers
#define REG_BIOS L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS"
#define REG_SYSTEMBIOS L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\SystemInformation"
#define REG_CENTRALPROCESSOR L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"
#define REG_COMPUTERHARDWAREIDS L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\SystemInformation"

// Structures matching user-mode
typedef struct _SEED_DATA {
    UINT64 seedValue;
    UINT64 timestamp;
} SEED_DATA, * PSEED_DATA;

typedef struct _SYSTEM_PROFILE {
    CHAR motherboardSerial[64];
    CHAR diskSerial[64];
    CHAR machineGuid[64];
    CHAR hwProfileGuid[64];
    CHAR productId[64];
    CHAR volumeGuid[64];
    UCHAR macAddress[6];
    CHAR systemUUID[37];
    CHAR biosSerial[64];
    CHAR chassisSerial[64];
} SYSTEM_PROFILE, * PSYSTEM_PROFILE;

// Global state management
typedef struct _PROFILE_STATE {
    BOOLEAN isModified;
    SYSTEM_PROFILE original;
    SYSTEM_PROFILE temporary;
    WDFWAITLOCK lock;
} PROFILE_STATE, * PPROFILE_STATE;

// Extern globals
extern PROFILE_STATE g_ProfileState;

// Function prototypes
NTSTATUS GenerateSystemProfile(_In_ PSEED_DATA seed, _Out_ PSYSTEM_PROFILE profile);
NTSTATUS ApplyRegistryModifications(_In_ PSYSTEM_PROFILE profile);
NTSTATUS RestoreRegistryValues(VOID);
NTSTATUS BackupOriginalValues(VOID);
NTSTATUS EnumerateAndSpoofNetworkAdapters(_In_ PUCHAR NewMacAddress);
VOID GenerateGUID(_In_ UINT64 seed, _Out_ PCHAR guidString, _In_ SIZE_T bufferSize);
VOID GenerateUUID(_In_ UINT64 seed, _Out_ PCHAR uuidString, _In_ SIZE_T bufferSize);
UINT64 SimpleHash(_In_ UINT64 value);
UINT64 EnhancedHash(_In_ UINT64 value);
NTSTATUS GenerateSecureRandom(_Out_ PVOID Buffer, _In_ ULONG BufferSize);

// Registry helpers
NTSTATUS SetRegistryValue(
    _In_ PCWSTR RegistryPath,
    _In_ PCWSTR ValueName,
    _In_ ULONG Type,
    _In_ PVOID Data,
    _In_ ULONG DataSize
);

NTSTATUS QueryRegistryValue(
    _In_ PCWSTR RegistryPath,
    _In_ PCWSTR ValueName,
    _Out_ PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_opt_ PULONG ResultLength
);

// WDF callbacks
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL PCCleanupEvtIoDeviceControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP PCCleanupEvtDriverContextCleanup;
