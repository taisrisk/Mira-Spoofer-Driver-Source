#include "GuidGenerator.h"
#include <ntstrsafe.h>

//
// HashValue - Simple hash function for GUID generation
//
UINT64 HashValue(_In_ UINT64 Value)
{
    Value ^= (Value >> 33);
    Value *= 0xff51afd7ed558ccdULL;
    Value ^= (Value >> 33);
    Value *= 0xc4ceb9fe1a85ec53ULL;
    Value ^= (Value >> 33);
    return Value;
}

//
// GenerateGUID - Create GUID string from seed
//
VOID GenerateGUID(_In_ UINT64 Seed, _Out_ PCHAR GuidString, _In_ SIZE_T BufferSize)
{
    UINT64 hash1 = HashValue(Seed);
    UINT64 hash2 = HashValue(Seed ^ 0xDEADBEEFCAFEBABEULL);

    UINT32 d1 = (UINT32)(hash1 & 0xFFFFFFFF);
    UINT16 d2 = (UINT16)((hash1 >> 32) & 0xFFFF);
    UINT16 d3 = (UINT16)((hash1 >> 48) & 0xFFFF);
    UINT16 d4 = (UINT16)(hash2 & 0xFFFF);
    UINT64 d5 = (hash2 >> 16) & 0xFFFFFFFFFFFFULL;

    RtlStringCbPrintfA(GuidString, BufferSize,
        "{%08X-%04X-%04X-%04X-%012llX}",
        d1, d2, d3, d4, d5);
}

//
// GenerateUUID - Create UUID string from seed (RFC 4122 format)
//
VOID GenerateUUID(_In_ UINT64 Seed, _Out_ PCHAR UuidString, _In_ SIZE_T BufferSize)
{
    UINT64 hash1 = HashValue(Seed);
    UINT64 hash2 = HashValue(Seed ^ 0xDEADBEEFCAFEBABEULL);

    UINT32 d1 = (UINT32)(hash1 & 0xFFFFFFFF);
    UINT16 d2 = (UINT16)((hash1 >> 32) & 0xFFFF);
    UINT16 d3 = (UINT16)((hash1 >> 48) & 0x0FFF) | 0x4000;  // Version 4
    UINT16 d4 = (UINT16)((hash2 & 0x3FFF) | 0x8000);  // Variant bits
    UINT64 d5 = (hash2 >> 16) & 0xFFFFFFFFFFFFULL;

    RtlStringCbPrintfA(UuidString, BufferSize,
        "%08X-%04X-%04X-%04X-%012llX",
        d1, d2, d3, d4, d5);
}

//
// GenerateProductId - Generate Windows Product ID
//
NTSTATUS GenerateProductId(_In_ UINT64 Seed, _Out_ PCHAR Buffer, _In_ SIZE_T BufferSize)
{
    // Windows format: XXXXX-XXXXX-XXXXX-XXXXX
    UINT64 hash = HashValue(Seed);
    
    return RtlStringCbPrintfA(Buffer, BufferSize,
        "%05llu-%05llu-%05llu-%05llu",
        (HashValue(hash) % 100000),
        (HashValue(hash * 2) % 100000),
        (HashValue(hash * 3) % 100000),
        (HashValue(hash * 4) % 100000));
}

//
// GenerateMACAddress - Generate locally administered MAC address
//
NTSTATUS GenerateMACAddress(_In_ UINT64 Seed, _Out_ PUCHAR MacAddress)
{
    UINT64 hash = HashValue(Seed ^ 0xBADC0FFEE0DDF00DULL);
    
    // Set locally administered bit (bit 1 of first octet)
    // Clear multicast bit (bit 0 of first octet)
    MacAddress[0] = 0x02;
    MacAddress[1] = (UCHAR)((hash >> 40) & 0xFF);
    MacAddress[2] = (UCHAR)((hash >> 32) & 0xFF);
    MacAddress[3] = (UCHAR)((hash >> 24) & 0xFF);
    MacAddress[4] = (UCHAR)((hash >> 16) & 0xFF);
    MacAddress[5] = (UCHAR)((hash >> 8) & 0xFF);
    
    return STATUS_SUCCESS;
}
