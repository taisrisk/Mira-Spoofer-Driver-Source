#include "Driver.h"
#include "SerialGenerator.h"
#include "GuidGenerator.h"
#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <wdmsec.h>

// Global state management
PROFILE_STATE g_ProfileState = { 0 };

//
// EnumerateAndSpoofNetworkAdapters - Spoof MAC addresses for all network adapters
//
NTSTATUS EnumerateAndSpoofNetworkAdapters(_In_ PUCHAR NewMacAddress)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING regPath;
    HANDLE hKey = NULL, hAdapterKey = NULL;
    ULONG index = 0;
    ULONG successCount = 0;
    
    RtlInitUnicodeString(&regPath, 
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}");
    
    InitializeObjectAttributes(&objAttr, &regPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    
    status = ZwOpenKey(&hKey, KEY_ENUMERATE_SUB_KEYS | KEY_SET_VALUE, &objAttr);
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to open network adapter key - 0x%x\n", status));
        return status;
    }
    
    // Enumerate up to 64 adapters (includes virtual, physical, and deprecated)
    for (index = 0; index < 64; index++) {
        WCHAR subKeyName[16];
        UNICODE_STRING subKeyString;
        WCHAR macWide[18];
        
        RtlStringCbPrintfW(subKeyName, sizeof(subKeyName), L"%04d", index);
        RtlInitUnicodeString(&subKeyString, subKeyName);
        
        InitializeObjectAttributes(&objAttr, &subKeyString, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, hKey, NULL);
        
        status = ZwOpenKey(&hAdapterKey, KEY_SET_VALUE | KEY_QUERY_VALUE, &objAttr);
        if (NT_SUCCESS(status)) {
            // Set NetworkAddress (MAC override)
            RtlStringCbPrintfW(macWide, sizeof(macWide), L"%02X%02X%02X%02X%02X%02X",
                NewMacAddress[0], NewMacAddress[1], NewMacAddress[2],
                NewMacAddress[3], NewMacAddress[4], NewMacAddress[5]);
            
            UNICODE_STRING valueName;
            RtlInitUnicodeString(&valueName, L"NetworkAddress");
            
            status = ZwSetValueKey(hAdapterKey, &valueName, 0, REG_SZ, 
                macWide, (ULONG)wcslen(macWide) * sizeof(WCHAR) + sizeof(WCHAR));
            
            if (NT_SUCCESS(status)) {
                // Also set NetworkAddressOverride to force the change
                ULONG overrideValue = 1;
                RtlInitUnicodeString(&valueName, L"NetworkAddressOverride");
                ZwSetValueKey(hAdapterKey, &valueName, 0, REG_DWORD, 
                    &overrideValue, sizeof(ULONG));
                
                successCount++;
                KdPrint(("PCCleanup: Set MAC for adapter %04d: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    index, NewMacAddress[0], NewMacAddress[1], NewMacAddress[2],
                    NewMacAddress[3], NewMacAddress[4], NewMacAddress[5]));
            }
            
            ZwClose(hAdapterKey);
        }
    }
    
    ZwClose(hKey);
    
    if (successCount > 0) {
        KdPrint(("PCCleanup: Network adapter MAC spoofing completed - %d adapters modified\n", successCount));
        return STATUS_SUCCESS;
    } else {
        KdPrint(("PCCleanup: No network adapters were modified\n"));
        return STATUS_NOT_FOUND;
    }
}

//
// Registry helper functions
//
NTSTATUS SetRegistryValue(
    _In_ PCWSTR RegistryPath,
    _In_ PCWSTR ValueName,
    _In_ ULONG Type,
    _In_ PVOID Data,
    _In_ ULONG DataSize
)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING regPath, valueName;
    HANDLE hKey = NULL;

    RtlInitUnicodeString(&regPath, RegistryPath);
    InitializeObjectAttributes(&objAttr, &regPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr);
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to open registry key %ws - 0x%x\n", RegistryPath, status));
        return status;
    }

    RtlInitUnicodeString(&valueName, ValueName);
    status = ZwSetValueKey(hKey, &valueName, 0, Type, Data, DataSize);

    ZwClose(hKey);
    return status;
}

NTSTATUS QueryRegistryValue(
    _In_ PCWSTR RegistryPath,
    _In_ PCWSTR ValueName,
    _Out_ PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_opt_ PULONG ResultLength
)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING regPath, valueName;
    HANDLE hKey = NULL;
    UCHAR buffer[512];
    PKEY_VALUE_PARTIAL_INFORMATION keyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
    ULONG resultLength = 0;

    if (Buffer && BufferSize > 0) {
        RtlZeroMemory(Buffer, BufferSize);
    }

    if (ResultLength) {
        *ResultLength = 0;
    }

    if (!Buffer || BufferSize == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlInitUnicodeString(&regPath, RegistryPath);
    InitializeObjectAttributes(&objAttr, &regPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenKey(&hKey, KEY_QUERY_VALUE, &objAttr);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&valueName, ValueName);
    status = ZwQueryValueKey(hKey, &valueName, KeyValuePartialInformation, keyInfo, sizeof(buffer), &resultLength);

    if (NT_SUCCESS(status) && keyInfo->DataLength <= BufferSize) {
        RtlCopyMemory(Buffer, keyInfo->Data, keyInfo->DataLength);
        if (ResultLength) {
            *ResultLength = keyInfo->DataLength;
        }
    }

    ZwClose(hKey);
    return status;
}

//
// DriverEntry - Main entry point
//
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES driverAttributes;
    WDFDRIVER driver;
    PWDFDEVICE_INIT pDeviceInit = NULL;
    WDFDEVICE controlDevice;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLink;

    KdPrint(("TRACE: Initialization - PCCleanup: DriverEntry called\n"));
    DbgPrint("PCCleanup: ======== DriverEntry START ========\n");

    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    WDF_OBJECT_ATTRIBUTES_INIT(&driverAttributes);
    driverAttributes.EvtCleanupCallback = PCCleanupEvtDriverContextCleanup;

    status = WdfDriverCreate(DriverObject, RegistryPath, &driverAttributes, &config, &driver);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PCCleanup: WdfDriverCreate FAILED 0x%x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = driver;
    status = WdfWaitLockCreate(&lockAttributes, &g_ProfileState.lock);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PCCleanup: WdfWaitLockCreate FAILED 0x%x\n", status);
        return status;
    }

    DbgPrint("PCCleanup: Driver initialized with manufacturer-specific serial generation\n");

    DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    pDeviceInit = WdfControlDeviceInitAllocate(driver, &sddl);
    if (pDeviceInit == NULL) {
        DbgPrint("PCCleanup: WdfControlDeviceInitAllocate FAILED\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    WdfDeviceInitSetCharacteristics(pDeviceInit, FILE_DEVICE_SECURE_OPEN, TRUE);
    WdfDeviceInitSetIoType(pDeviceInit, WdfDeviceIoBuffered);

    RtlInitUnicodeString(&deviceName, L"\\Device\\PCCleanupDriver");
    status = WdfDeviceInitAssignName(pDeviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PCCleanup: WdfDeviceInitAssignName FAILED 0x%x\n", status);
        WdfDeviceInitFree(pDeviceInit);
        return status;
    }

    status = WdfDeviceCreate(&pDeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &controlDevice);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PCCleanup: WdfDeviceCreate FAILED 0x%x\n", status);
        return status;
    }

    RtlInitUnicodeString(&symbolicLink, L"\\DosDevices\\PCCleanupDriver");
    status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLink);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PCCleanup: WdfDeviceCreateSymbolicLink FAILED 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = PCCleanupEvtIoDeviceControl;

    status = WdfIoQueueCreate(controlDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PCCleanup: WdfIoQueueCreate FAILED 0x%x\n", status);
        return status;
    }

    WdfControlFinishInitializing(controlDevice);
    DbgPrint("PCCleanup: DriverEntry complete\n");
    return STATUS_SUCCESS;
}

//
// PCCleanupEvtIoDeviceControl - IOCTL handler
//
VOID PCCleanupEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    NTSTATUS status = STATUS_SUCCESS;
    size_t bytesReturned = 0;
    PVOID inputBuffer = NULL;
    PVOID outputBuffer = NULL;

    UNREFERENCED_PARAMETER(Queue);

    KdPrint(("TRACE: Dispatch - PCCleanup: IOCTL received: 0x%x\n", IoControlCode));

    switch (IoControlCode) {
    case IOCTL_APPLY_TEMP_PROFILE:
    {
        KdPrint(("PCCleanup: IOCTL_APPLY_TEMP_PROFILE\n"));

        if (InputBufferLength >= sizeof(SEED_DATA)) {
            status = WdfRequestRetrieveInputBuffer(Request, sizeof(SEED_DATA), &inputBuffer, NULL);
            if (NT_SUCCESS(status)) {
                PSEED_DATA seed = (PSEED_DATA)inputBuffer;

                if (OutputBufferLength >= sizeof(SYSTEM_PROFILE)) {
                    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SYSTEM_PROFILE), &outputBuffer, NULL);
                    if (NT_SUCCESS(status)) {
                        PSYSTEM_PROFILE profile = (PSYSTEM_PROFILE)outputBuffer;

                        WdfWaitLockAcquire(g_ProfileState.lock, NULL);

                        if (!g_ProfileState.isModified) {
                            status = BackupOriginalValues();
                            if (!NT_SUCCESS(status)) {
                                WdfWaitLockRelease(g_ProfileState.lock);
                                KdPrint(("PCCleanup: Failed to backup original values - 0x%x\n", status));
                                break;
                            }
                        }

                        status = GenerateSystemProfile(seed, profile);
                        if (NT_SUCCESS(status)) {
                            status = ApplyRegistryModifications(profile);
                            if (NT_SUCCESS(status)) {
                                RtlCopyMemory(&g_ProfileState.temporary, profile, sizeof(SYSTEM_PROFILE));
                                g_ProfileState.isModified = TRUE;
                                bytesReturned = sizeof(SYSTEM_PROFILE);
                                KdPrint(("PCCleanup: Temporary profile applied successfully\n"));
                            }
                        }

                        WdfWaitLockRelease(g_ProfileState.lock);
                    }
                }
                else {
                    status = STATUS_BUFFER_TOO_SMALL;
                }
            }
        }
        else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    case IOCTL_RESTORE_PROFILE:
    {
        KdPrint(("PCCleanup: IOCTL_RESTORE_PROFILE\n"));

        WdfWaitLockAcquire(g_ProfileState.lock, NULL);

        if (g_ProfileState.isModified) {
            status = RestoreRegistryValues();
            if (NT_SUCCESS(status)) {
                RtlZeroMemory(&g_ProfileState.temporary, sizeof(SYSTEM_PROFILE));
                g_ProfileState.isModified = FALSE;
                KdPrint(("PCCleanup: Original profile restored\n"));
            }
        }
        else {
            KdPrint(("PCCleanup: No active modifications to restore\n"));
            status = STATUS_SUCCESS;
        }

        WdfWaitLockRelease(g_ProfileState.lock);
        break;
    }

    case IOCTL_GET_STATUS:
    {
        KdPrint(("PCCleanup: IOCTL_GET_STATUS - Driver operational\n"));
        status = STATUS_SUCCESS;
        break;
    }

    default:
        KdPrint(("PCCleanup: Unknown IOCTL 0x%x\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

//
// BackupOriginalValues - Save original registry values
//
NTSTATUS BackupOriginalValues(VOID)
{
    NTSTATUS status;
    NTSTATUS finalStatus = STATUS_SUCCESS;
    WCHAR guidBuffer[64];
    ULONG resultLength = 0;

    KdPrint(("PCCleanup: Backing up original values\n"));

    status = QueryRegistryValue(REG_CRYPTOGRAPHY, L"MachineGuid", guidBuffer, sizeof(guidBuffer), &resultLength);
    if (NT_SUCCESS(status)) {
        RtlStringCbPrintfA(g_ProfileState.original.machineGuid,
            sizeof(g_ProfileState.original.machineGuid),
            "%ws", guidBuffer);
        KdPrint(("PCCleanup: Backed up MachineGuid successfully\n"));
    }
    else {
        KdPrint(("PCCleanup: Failed to backup MachineGuid - 0x%x\n", status));
        finalStatus = status;
    }

    status = QueryRegistryValue(REG_HWPROFILE, L"HwProfileGuid", guidBuffer, sizeof(guidBuffer), &resultLength);
    if (NT_SUCCESS(status)) {
        RtlStringCbPrintfA(g_ProfileState.original.hwProfileGuid,
            sizeof(g_ProfileState.original.hwProfileGuid),
            "%ws", guidBuffer);
        KdPrint(("PCCleanup: Backed up HwProfileGuid successfully\n"));
    }
    else {
        KdPrint(("PCCleanup: Failed to backup HwProfileGuid - 0x%x\n", status));
        if (NT_SUCCESS(finalStatus)) {
            finalStatus = status;
        }
    }

    status = QueryRegistryValue(REG_CURRENTVERSION, L"ProductId", guidBuffer, sizeof(guidBuffer), &resultLength);
    if (NT_SUCCESS(status)) {
        RtlStringCbPrintfA(g_ProfileState.original.productId,
            sizeof(g_ProfileState.original.productId),
            "%ws", guidBuffer);
        KdPrint(("PCCleanup: Backed up ProductId successfully\n"));
    }
    else {
        KdPrint(("PCCleanup: Failed to backup ProductId - 0x%x\n", status));
        if (NT_SUCCESS(finalStatus)) {
            finalStatus = status;
        }
    }

    status = QueryRegistryValue(REG_BIOS, L"BIOSSerialNumber", guidBuffer, sizeof(guidBuffer), &resultLength);
    if (NT_SUCCESS(status)) {
        RtlStringCbPrintfA(g_ProfileState.original.biosSerial,
            sizeof(g_ProfileState.original.biosSerial),
            "%ws", guidBuffer);
        KdPrint(("PCCleanup: Backed up BIOS Serial successfully\n"));
    }

    status = QueryRegistryValue(REG_BIOS, L"BaseBoardSerialNumber", guidBuffer, sizeof(guidBuffer), &resultLength);
    if (NT_SUCCESS(status)) {
        RtlStringCbPrintfA(g_ProfileState.original.motherboardSerial,
            sizeof(g_ProfileState.original.motherboardSerial),
            "%ws", guidBuffer);
        KdPrint(("PCCleanup: Backed up Motherboard Serial successfully\n"));
    }

    status = QueryRegistryValue(REG_BIOS, L"SystemSerialNumber", guidBuffer, sizeof(guidBuffer), &resultLength);
    if (NT_SUCCESS(status)) {
        RtlStringCbPrintfA(g_ProfileState.original.chassisSerial,
            sizeof(g_ProfileState.original.chassisSerial),
            "%ws", guidBuffer);
        KdPrint(("PCCleanup: Backed up Chassis Serial successfully\n"));
    }

    status = QueryRegistryValue(REG_BIOS, L"SystemUUID", guidBuffer, sizeof(guidBuffer), &resultLength);
    if (NT_SUCCESS(status)) {
        RtlStringCbPrintfA(g_ProfileState.original.systemUUID,
            sizeof(g_ProfileState.original.systemUUID),
            "%ws", guidBuffer);
        KdPrint(("PCCleanup: Backed up System UUID successfully\n"));
    }

    if (NT_SUCCESS(finalStatus)) {
        KdPrint(("PCCleanup: Critical values backed up successfully\n"));
    }
    else {
        KdPrint(("PCCleanup: Backup completed with errors - 0x%x\n", finalStatus));
    }

    return finalStatus;
}

//
// ApplyRegistryModifications - Apply temporary registry changes
//
NTSTATUS ApplyRegistryModifications(_In_ PSYSTEM_PROFILE profile)
{
    NTSTATUS status;
    WCHAR wideBuffer[64];
    ANSI_STRING ansiString;
    UNICODE_STRING unicodeString;

    KdPrint(("PCCleanup: Applying registry modifications\n"));

    RtlInitAnsiString(&ansiString, profile->machineGuid);
    unicodeString.Buffer = wideBuffer;
    unicodeString.MaximumLength = sizeof(wideBuffer);
    status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
    if (NT_SUCCESS(status)) {
        status = SetRegistryValue(REG_CRYPTOGRAPHY, L"MachineGuid", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        if (NT_SUCCESS(status)) {
            KdPrint(("PCCleanup: MachineGuid set successfully\n"));
        }
    }

    RtlInitAnsiString(&ansiString, profile->hwProfileGuid);
    unicodeString.Buffer = wideBuffer;
    unicodeString.MaximumLength = sizeof(wideBuffer);
    status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
    if (NT_SUCCESS(status)) {
        status = SetRegistryValue(REG_HWPROFILE, L"HwProfileGuid", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        if (NT_SUCCESS(status)) {
            KdPrint(("PCCleanup: HwProfileGuid set successfully\n"));
        }
    }

    RtlInitAnsiString(&ansiString, profile->productId);
    unicodeString.Buffer = wideBuffer;
    unicodeString.MaximumLength = sizeof(wideBuffer);
    status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
    if (NT_SUCCESS(status)) {
        status = SetRegistryValue(REG_CURRENTVERSION, L"ProductId", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        if (NT_SUCCESS(status)) {
            KdPrint(("PCCleanup: ProductId set successfully\n"));
        }
    }

    RtlInitAnsiString(&ansiString, profile->biosSerial);
    unicodeString.Buffer = wideBuffer;
    unicodeString.MaximumLength = sizeof(wideBuffer);
    status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
    if (NT_SUCCESS(status)) {
        SetRegistryValue(REG_BIOS, L"BIOSSerialNumber", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
    }

    RtlInitAnsiString(&ansiString, profile->motherboardSerial);
    unicodeString.Buffer = wideBuffer;
    unicodeString.MaximumLength = sizeof(wideBuffer);
    status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
    if (NT_SUCCESS(status)) {
        SetRegistryValue(REG_BIOS, L"BaseBoardSerialNumber", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        SetRegistryValue(REG_SYSTEMBIOS, L"SystemProductName", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
    }

    RtlInitAnsiString(&ansiString, profile->chassisSerial);
    unicodeString.Buffer = wideBuffer;
    unicodeString.MaximumLength = sizeof(wideBuffer);
    status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
    if (NT_SUCCESS(status)) {
        SetRegistryValue(REG_BIOS, L"SystemSerialNumber", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
    }

    RtlInitAnsiString(&ansiString, profile->systemUUID);
    unicodeString.Buffer = wideBuffer;
    unicodeString.MaximumLength = sizeof(wideBuffer);
    status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
    if (NT_SUCCESS(status)) {
        SetRegistryValue(REG_BIOS, L"SystemUUID", REG_SZ,
            unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
    }

    // FORCE spoof all network adapter MAC addresses
    status = EnumerateAndSpoofNetworkAdapters(profile->macAddress);
    if (NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Network adapters MAC addresses spoofed successfully\n"));
    } else {
        KdPrint(("PCCleanup: Failed to spoof network adapters - 0x%x (continuing anyway)\n", status));
    }

    // PATCH SMBIOS FIRMWARE TABLES IN MEMORY
    status = ApplySMBIOSChanges(profile);
    if (NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: SMBIOS firmware tables patched successfully\n"));
    } else {
        KdPrint(("PCCleanup: Failed to patch SMBIOS tables - 0x%x (continuing anyway)\n", status));
    }

    KdPrint(("PCCleanup: Registry modifications completed\n"));
    return STATUS_SUCCESS;
}

//
// RestoreRegistryValues - Restore original values from backup
//
NTSTATUS RestoreRegistryValues(VOID)
{
    NTSTATUS status;
    WCHAR wideBuffer[64];
    ANSI_STRING ansiString;
    UNICODE_STRING unicodeString;

    KdPrint(("PCCleanup: Restoring original registry values\n"));

    if (g_ProfileState.original.machineGuid[0] != '\0') {
        RtlInitAnsiString(&ansiString, g_ProfileState.original.machineGuid);
        unicodeString.Buffer = wideBuffer;
        unicodeString.MaximumLength = sizeof(wideBuffer);
        status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
        if (NT_SUCCESS(status)) {
            SetRegistryValue(REG_CRYPTOGRAPHY, L"MachineGuid", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        }
    }

    if (g_ProfileState.original.hwProfileGuid[0] != '\0') {
        RtlInitAnsiString(&ansiString, g_ProfileState.original.hwProfileGuid);
        unicodeString.Buffer = wideBuffer;
        unicodeString.MaximumLength = sizeof(wideBuffer);
        status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
        if (NT_SUCCESS(status)) {
            SetRegistryValue(REG_HWPROFILE, L"HwProfileGuid", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        }
    }

    if (g_ProfileState.original.productId[0] != '\0') {
        RtlInitAnsiString(&ansiString, g_ProfileState.original.productId);
        unicodeString.Buffer = wideBuffer;
        unicodeString.MaximumLength = sizeof(wideBuffer);
        status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
        if (NT_SUCCESS(status)) {
            SetRegistryValue(REG_CURRENTVERSION, L"ProductId", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        }
    }

    if (g_ProfileState.original.biosSerial[0] != '\0') {
        RtlInitAnsiString(&ansiString, g_ProfileState.original.biosSerial);
        unicodeString.Buffer = wideBuffer;
        unicodeString.MaximumLength = sizeof(wideBuffer);
        status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
        if (NT_SUCCESS(status)) {
            SetRegistryValue(REG_BIOS, L"BIOSSerialNumber", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        }
    }

    if (g_ProfileState.original.motherboardSerial[0] != '\0') {
        RtlInitAnsiString(&ansiString, g_ProfileState.original.motherboardSerial);
        unicodeString.Buffer = wideBuffer;
        unicodeString.MaximumLength = sizeof(wideBuffer);
        status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
        if (NT_SUCCESS(status)) {
            SetRegistryValue(REG_BIOS, L"BaseBoardSerialNumber", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
            SetRegistryValue(REG_SYSTEMBIOS, L"SystemProductName", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        }
    }

    if (g_ProfileState.original.chassisSerial[0] != '\0') {
        RtlInitAnsiString(&ansiString, g_ProfileState.original.chassisSerial);
        unicodeString.Buffer = wideBuffer;
        unicodeString.MaximumLength = sizeof(wideBuffer);
        status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
        if (NT_SUCCESS(status)) {
            SetRegistryValue(REG_BIOS, L"SystemSerialNumber", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        }
    }

    if (g_ProfileState.original.systemUUID[0] != '\0') {
        RtlInitAnsiString(&ansiString, g_ProfileState.original.systemUUID);
        unicodeString.Buffer = wideBuffer;
        unicodeString.MaximumLength = sizeof(wideBuffer);
        status = RtlAnsiStringToUnicodeString(&unicodeString, &ansiString, FALSE);
        if (NT_SUCCESS(status)) {
            SetRegistryValue(REG_BIOS, L"SystemUUID", REG_SZ,
                unicodeString.Buffer, unicodeString.Length + sizeof(WCHAR));
        }
    }

    // Restore SMBIOS firmware tables
    status = RestoreSMBIOSTables();
    if (NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: SMBIOS firmware tables restored\n"));
    } else {
        KdPrint(("PCCleanup: Failed to restore SMBIOS tables - 0x%x\n", status));
    }

    KdPrint(("PCCleanup: Original values restored\n"));
    return STATUS_SUCCESS;
}

//
// GenerateSystemProfile - Create deterministic hardware IDs using modular generators
//
NTSTATUS GenerateSystemProfile(
    _In_ PSEED_DATA seed,
    _Out_ PSYSTEM_PROFILE profile
)
{
    NTSTATUS status;
    PLATFORM_INFO platformInfo;
    UINT64 baseSeed = seed->seedValue ^ seed->timestamp;

    RtlZeroMemory(profile, sizeof(SYSTEM_PROFILE));

    // Detect hardware platform
    status = DetectHardwarePlatform(&platformInfo);
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Platform detection failed, using generic\n"));
        platformInfo.Vendor = VENDOR_GENERIC;
    }

    // Generate manufacturer-specific motherboard serial
    status = GenerateMotherboardSerial(baseSeed, platformInfo.Vendor,
        profile->motherboardSerial, sizeof(profile->motherboardSerial));
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to generate motherboard serial\n"));
        return status;
    }

    // Generate disk serial
    status = GenerateDiskSerial(baseSeed * 0x9E3779B97F4A7C15ULL,
        profile->diskSerial, sizeof(profile->diskSerial));
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to generate disk serial\n"));
        return status;
    }

    // Generate BIOS serial
    status = GenerateBIOSSerial(baseSeed * 0x517CC1B727220A95ULL, platformInfo.Vendor,
        profile->biosSerial, sizeof(profile->biosSerial));
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to generate BIOS serial\n"));
        return status;
    }

    // Generate chassis serial
    status = GenerateChassisSerial(baseSeed * 0x85EBCA6B3B4F4A5FULL,
        profile->chassisSerial, sizeof(profile->chassisSerial));
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to generate chassis serial\n"));
        return status;
    }

    // Generate Product ID
    status = GenerateProductId(baseSeed,
        profile->productId, sizeof(profile->productId));
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to generate product ID\n"));
        return status;
    }

    // Generate GUIDs
    GenerateGUID(baseSeed, profile->machineGuid, sizeof(profile->machineGuid));
    GenerateGUID(baseSeed ^ 0x123456789ABCDEFULL, profile->hwProfileGuid, sizeof(profile->hwProfileGuid));
    GenerateGUID(baseSeed ^ 0xFEDCBA9876543210ULL, profile->volumeGuid, sizeof(profile->volumeGuid));

    // Generate MAC address
    status = GenerateMACAddress(baseSeed ^ 0xBADC0FFEE0DDF00DULL, profile->macAddress);
    if (!NT_SUCCESS(status)) {
        KdPrint(("PCCleanup: Failed to generate MAC address\n"));
        return status;
    }

    // Generate System UUID
    GenerateUUID(baseSeed, profile->systemUUID, sizeof(profile->systemUUID));

    // Debug output
    KdPrint(("PCCleanup: Generated profile from seed 0x%llX\n", seed->seedValue));
    KdPrint(("  Platform: %s %s\n",
        platformInfo.IsIntel ? "Intel" : "AMD",
        platformInfo.Vendor == VENDOR_ASUS ? "ASUS" :
        platformInfo.Vendor == VENDOR_MSI ? "MSI" :
        platformInfo.Vendor == VENDOR_GIGABYTE ? "Gigabyte" :
        platformInfo.Vendor == VENDOR_ASROCK ? "ASRock" :
        platformInfo.Vendor == VENDOR_INTEL ? "Intel" : "Generic"));
    KdPrint(("  Motherboard: %s\n", profile->motherboardSerial));
    KdPrint(("  Disk: %s\n", profile->diskSerial));
    KdPrint(("  BIOS: %s\n", profile->biosSerial));
    KdPrint(("  Chassis: %s\n", profile->chassisSerial));
    KdPrint(("  Product ID: %s\n", profile->productId));
    KdPrint(("  Machine GUID: %s\n", profile->machineGuid));

    return STATUS_SUCCESS;
}

//
// PCCleanupEvtDriverContextCleanup - Driver cleanup callback
//
VOID PCCleanupEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
)
{
    UNICODE_STRING symbolicLink;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DriverObject);

    KdPrint(("PCCleanup: Driver cleanup called\n"));
    DbgPrint("PCCleanup: ======== CLEANUP START ========\n");

    RtlInitUnicodeString(&symbolicLink, L"\\DosDevices\\PCCleanupDriver");
    status = IoDeleteSymbolicLink(&symbolicLink);
    DbgPrint("PCCleanup: IoDeleteSymbolicLink returned 0x%x\n", status);

    if (g_ProfileState.lock != NULL) {
        WdfWaitLockAcquire(g_ProfileState.lock, NULL);

        if (g_ProfileState.isModified) {
            KdPrint(("PCCleanup: Restoring original values during cleanup\n"));
            RestoreRegistryValues();
            RestoreSMBIOSTables();
            g_ProfileState.isModified = FALSE;
        }

        RtlSecureZeroMemory(&g_ProfileState.original, sizeof(SYSTEM_PROFILE));
        RtlSecureZeroMemory(&g_ProfileState.temporary, sizeof(SYSTEM_PROFILE));

        WdfWaitLockRelease(g_ProfileState.lock);
    }

    KdPrint(("PCCleanup: Driver cleanup completed\n"));
    DbgPrint("PCCleanup: ======== CLEANUP COMPLETED ========\n");
}
