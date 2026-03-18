#pragma once
#include "UObject.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// TUObjectArray — global UObject store in UE5.
// Layout: chunked array of FUObjectItem pointers.
// ---------------------------------------------------------------------------

struct FUObjectItem
{
    UObject* Object;        // +0x00
    int32_t  Flags;         // +0x08
    int32_t  ClusterIndex;  // +0x0C
    int32_t  SerialNumber;  // +0x10
    int32_t  _pad;
};

struct TUObjectArray
{
    FUObjectItem** Objects;    // +0x00  pointer to chunk array
    int32_t        MaxElements;// +0x08
    int32_t        NumElements;// +0x0C
    int32_t        MaxChunks;  // +0x10
    int32_t        NumChunks;  // +0x14

    static constexpr int32_t CHUNK_SIZE = 64 * 1024;

    UObject* GetByIndex(int32_t idx) const
    {
        if (idx < 0 || idx >= NumElements) return nullptr;
        int32_t chunk  = idx / CHUNK_SIZE;
        int32_t offset = idx % CHUNK_SIZE;
        if (chunk >= NumChunks || !Objects[chunk]) return nullptr;
        return Objects[chunk][offset].Object;
    }

    int32_t Num() const { return NumElements; }
};

// ---------------------------------------------------------------------------
// GObjects accessor.
// Call Init() once after injection to resolve the pointer from your clone.
// ---------------------------------------------------------------------------
namespace GObjects
{
    // Pointer to the global TUObjectArray.
    // Set via Init() by scanning for the engine's GUObjectArray export
    // or by finding it with a byte-pattern scan.
    inline TUObjectArray* Array = nullptr;

    // Scan the process for the known byte-pattern that precedes GUObjectArray.
    // Pattern comes from your clone's shipping executable — re-dump on updates.
    // Returns true if found and Array was set.
    bool Init(const char* pattern);

    // Find any UObject by class name (slow — use sparingly, cache results).
    UObject* FindObject(const char* className, const char* objectName = nullptr);

    // Get the owning UWorld (first UWorld in the array).
    UObject* GetWorld();
}
