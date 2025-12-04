#pragma once

#include <ntddk.h>

// GUID/UUID generation functions
VOID GenerateGUID(_In_ UINT64 Seed, _Out_ PCHAR GuidString, _In_ SIZE_T BufferSize);
VOID GenerateUUID(_In_ UINT64 Seed, _Out_ PCHAR UuidString, _In_ SIZE_T BufferSize);
NTSTATUS GenerateProductId(_In_ UINT64 Seed, _Out_ PCHAR Buffer, _In_ SIZE_T BufferSize);
NTSTATUS GenerateMACAddress(_In_ UINT64 Seed, _Out_ PUCHAR MacAddress);
UINT64 HashValue(_In_ UINT64 Value);
