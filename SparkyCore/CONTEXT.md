# SparkyCore — Component Context

## Role
SparkyCore is the **game DLL** — the actual mod runtime that runs inside the
Marvel Rivals clone process after being injected by SparkyLoader. It:
1. Initialises the UE5 SDK (finds GObjects, resolves class offsets).
2. Hooks engine functions (ProcessEvent, UWorld tick, etc.).
3. Runs mod features: aim assistance, ESP, movement tweaks, etc.
4. Exposes a config interface so the loader/server can push feature settings.

## Source Layout
```
SparkyCore/
├── src/
│   ├── DllMain.cpp             ← DLL entry point, starts Core background thread
│   └── SDK/
│       └── UE5/
│           ├── Types.h         ← FName, FString, TArray, FVector, FMatrix, FTransform…
│           ├── UObject.h       ← UObject, UField, FProperty, UStruct, UClass, UFunction
│           └── GObjects.h      ← TUObjectArray, GObjects::Init/FindObject/GetWorld
└── CMakeLists.txt
```

## UE5 SDK Architecture
```
TUObjectArray (GObjects)          ← global store of every live UObject
    └── UObject                   ← base of everything
        └── UField
            └── UStruct           ← has ChildProperties (FProperty linked list)
                ├── UClass        ← type descriptor; use to IsA() check
                └── UFunction     ← callable UFUNCTION; use with ProcessEvent()
```

To access a field on an actor:
```cpp
// 1. Find the class
UClass* cls = (UClass*)GObjects::FindObject("Class", "BP_HeroBase_C");
// 2. Find the property offset
int32_t off = cls->FindPropertyOffset(nameIndex); // compile-time or runtime name
// 3. Read the value
float hp = *reinterpret_cast<float*>((uint8_t*)actor + off);
```

## Game Integration Points (Marvel Rivals Clone)
These are the hooks / scans needed to fully implement features.
All require running against your clone's binary to get accurate signatures.

| Hook Target            | Purpose |
|------------------------|---------|
| `UWorld::Tick`         | Per-frame update for ESP, aimbot |
| `APlayerController::ProcessInput` | Input interception |
| `UGameplayAbility::CanActivateAbility` | Ability block/force |
| `ACharacter::TakeDamage` | Damage tracking |
| `ULocalPlayer::GetViewMatrix` | World-to-screen matrix |

## Deps
| Dep         | Source |
|-------------|--------|
| Windows.h   | system — DllMain, CreateThread |
| UE5 SDK     | in-tree (`src/SDK/UE5/`) — hand-written from UE5 source layout |
| MinHook     | (TODO) — needed for vtable / inline hooks |

## Flaws / TODOs
- [ ] **Core::Load/Loop/Unload not implemented**: only forward declarations in DllMain.
      Need to add `src/Core/Core.cpp` with GObjects init, hook setup, feature loop.
- [ ] **No hooks yet**: need MinHook (or manual vtable hooks) to intercept
      UWorld::Tick, ProcessEvent, etc. Add as a dep.
- [ ] **GObjects::Init pattern is a placeholder**: the byte-pattern must be scanned
      from your clone's shipping binary. Run a UE4SS dump or manual offset hunt.
- [ ] **No feature implementations**: Aimbot, ESP, movement mods are all TODO.
      Architecture is ready — add `src/Features/` directory mirroring Amalgam structure.
- [ ] **No config consumer**: server can push `config.bin` but SparkyCore has no
      parser for it yet. Add a JSON or binary config reader in `Core::Load`.
- [ ] **FMatrix::ProjectWorldToScreen** uses wrong M[][] indexing for UE5 row-major
      matrices. Verify against UE5 source `FMatrix::TransformFVector4`.
- [ ] **Memory scanner evasion inside DLL**: once injected, SparkyCore's own code
      pages are visible to a scanner. Consider:
      - Encrypting `.text` section at rest, decrypt on load (loader hook)
      - Splitting feature code across multiple small allocations
      - Periodic code mutation (XOR page, restore on access via VEH)
- [ ] **No unload path**: `Core::Unload` needs to restore hooked vtable entries,
      free allocations, and signal DllMain to exit cleanly.
