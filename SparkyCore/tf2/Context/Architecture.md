# Sparky Codebase Architecture

## Overview
Sparky is an internal cheat for Team Fortress 2 (64-bit). It is structured as a dynamically linked library (DLL) injected into the `tf_win64.exe` process. It relies on standard internal hacking techniques: signature scanning for patterns, virtual function hooking (via MinHook), byte patching, and directly interfacing with the Source Engine SDK.

## Directory Structure
- **`src/Core/`**: Manages the cheat's lifecycle (Load, Loop, Unload). Ensures injection into the correct process, waits for engine modules (like `client.dll`), and initializes all systems (interfaces, signatures, hooks).
- **`src/Features/`**: Contains the logical implementations of all cheat features. Key submodules include:
  - `Aimbot`: Automated aiming assistance.
  - `Visuals`: ESP, chams, world modulation.
  - `EnginePrediction`: Matches client movement logic with the server to predict positions for accuracy.
  - `CritHack`, `Backtrack`, `Ticks`, `PacketManip`: Explicit exploit features manipulating game states.
  - `ImGui`, `Menu`: Graphical user interface using ImGui over DirectX 9.
- **`src/Hooks/`**: Contains individual files for each hooked Source Engine function. High-traffic hooks like `CHLClient_CreateMove`, `IVModelRender_DrawModelExecute`, and `CNetChannel_SendNetMsg` dictate the execution flow of the cheat features.
- **`src/SDK/`**: Classes and structures derived from the Source Engine SDK. Allows direct interaction with engine interfaces (e.g., `IBaseClientDLL`, `IClientEntityList`).
- **`src/Utils/`**: Utility functions for hooking, memory management (`Memory.h`), signature mapping (`Signatures.h`), and hashing.
- **`src/BytePatches/`**: Responsible for overwriting game memory with NOPs or custom bytes to override specific behaviors implicitly without a full VTable hook.

## Execution Flow
1. **Injection & Attachment**: `DllMain.cpp` is called upon injection. It spawns a new thread calling `Core::Load`.
2. **Initialization**: `Core::Load` verifies the process (`tf_win64.exe`), waits for `client.dll` to be loaded in memory, and triggers initialization sequences for interfaces, signatures, hooks, bytepatches, and features.
3. **Execution**: The cheat operates asynchronously within the game's native threads via the installed hooks.
   - For logic: `CHLClient_CreateMove` serves as the primary tick/logic entry point for features like Aimbot, Backtrack, and Engine Prediction.
   - For rendering: Hooks like `CClientModeShared_DoPostScreenSpaceEffects`, `IVModelRender_DrawModelExecute`, and `IPanel_PaintTraverse` intercept draw calls to overlay Visuals and the Menu.
4. **Unload**: On panic key (F11), `Core::Unload` reverts hooks, byte patches, and restores hardware context, safely detaching the cheat.

## Security & Detection Vectors
- **Strings**: String constants are secured using an XOR mechanism (`XS("string")`), deterring static string analysis by tools like VAC.
- **Hooks**: MinHook is utilized. Hook placement and restoration must cleanly happen without leaving artifacts.
- **Performance Constraints**: Overloading `CreateMove` or drawing functions can degrade FPS and timing, potentially indicating abnormal processing latency to anti-cheat methods.

## Performance Bottlenecks & Refactoring Targets
- Optimization of entity iteration in `CreateMove` and visual hooks.
- Math optimizations in `Aimbot`, `Backtrack`, and `EnginePrediction`.
- Caching results of frequently accessed NetVars and Interface pointers instead of dynamic lookups.
- Memory usage inside Features allocations to avoid continuous reallocation during high frame rates.

*(To be verified through codebase tracing and execution)*
