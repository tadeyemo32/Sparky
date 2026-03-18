#pragma once
#include "Types.h"

// ---------------------------------------------------------------------------
// UE5 UObject hierarchy — non-editor shipping build layout (UE 5.1/5.2).
// ---------------------------------------------------------------------------

struct UClass;
struct UFunction;
struct FProperty;

// --- UObject ---------------------------------------------------------------
struct UObject
{
    void**   VTable;          // +0x00
    int32_t  ObjectFlags;     // +0x08
    int32_t  InternalIndex;   // +0x0C  (index into GObjects)
    UClass*  ClassPrivate;    // +0x10
    FName    NamePrivate;     // +0x18
    UObject* OuterPrivate;    // +0x20

    FName   GetFName() const { return NamePrivate; }
    bool    IsA(UClass* cls) const;

    // Invoke a UFunction by pointer (mirrors UObject::ProcessEvent).
    void ProcessEvent(UFunction* func, void* params);
};

// --- UField ----------------------------------------------------------------
struct UField : UObject { UField* Next; /* +0x28 */ };

// --- FProperty (UE5 — replaces UProperty) ----------------------------------
struct FProperty
{
    void*     VTable;
    int32_t   ArrayDim;
    int32_t   ElementSize;
    uint64_t  PropertyFlags;
    uint16_t  RepIndex;
    uint8_t   BlueprintReplicationCondition;
    uint8_t   _pad;
    int32_t   Offset_Internal;
    FName     RepNotifyFunc;
    FProperty* PropertyLinkNext;
    FProperty* NextRef;
    FProperty* DestructorLinkNext;
    FProperty* PostConstructLinkNext;
};

// --- UStruct ---------------------------------------------------------------
struct UStruct : UField
{
    UStruct*          SuperStruct;
    UField*           Children;
    FProperty*        ChildProperties;
    int32_t           PropertiesSize;
    int32_t           MinAlignment;
    TArray<uint8_t>   Script;
    FProperty*        PropertyLink;
    FProperty*        RefLink;
    FProperty*        DestructorLink;
    FProperty*        PostConstructLink;
    TArray<UObject*>  ScriptAndPropertyObjectReferences;

    // Walk ChildProperties for a property by FName ComparisonIndex.
    int32_t FindPropertyOffset(uint32_t nameIndex) const
    {
        for (auto* p = ChildProperties; p; p = p->PropertyLinkNext)
            if (p->RepNotifyFunc.ComparisonIndex == nameIndex)
                return p->Offset_Internal;
        return -1;
    }
};

// --- UClass ----------------------------------------------------------------
struct UClass : UStruct { uint8_t _pad[0x28]; };

// --- UFunction -------------------------------------------------------------
struct UFunction : UStruct
{
    uint32_t   FunctionFlags;
    uint16_t   RepOffset;
    uint8_t    NumParms;
    uint8_t    _pad1;
    uint16_t   ParmsSize;
    uint16_t   ReturnValueOffset;
    uint16_t   RPCId;
    uint16_t   RPCResponseId;
    FProperty* FirstPropertyToInit;
    UFunction* EventGraphFunction;
    int32_t    EventGraphCallOffset;
    void*      Func; // native fn pointer if FUNC_Native
};

// --- UWorld ----------------------------------------------------------------
struct UWorld : UObject
{
    // Key fields — offsets differ between UE versions;
    // scan with GObjects at runtime if your clone changes them.
    uint8_t _pad[0x100];
    // AGameStateBase* GameState;  // find via GObjects scan
};
