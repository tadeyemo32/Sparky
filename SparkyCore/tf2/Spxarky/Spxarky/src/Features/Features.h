#pragma once
// Spxarky - Aggregated feature header
// Include this single header instead of individual feature headers
// to decouple hook files from feature internals.

// --- Aimbot ---
#include "Aimbot/Aimbot.h"
#include "Aimbot/AimbotGlobal/AimbotGlobal.h"
#include "Aimbot/AimbotHitscan/AimbotHitscan.h"
#include "Aimbot/AimbotMelee/AimbotMelee.h"
#include "Aimbot/AimbotProjectile/AimbotProjectile.h"
#include "Aimbot/AutoAirblast/AutoAirblast.h"
#include "Aimbot/AutoDetonate/AutoDetonate.h"
#include "Aimbot/AutoHeal/AutoHeal.h"
#include "Aimbot/AutoRocketJump/AutoRocketJump.h"

// --- Combat ---
#include "Backtrack/Backtrack.h"
#include "CritHack/CritHack.h"
#include "NoSpread/NoSpread.h"
#include "NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "NoSpread/NoSpreadProjectile/NoSpreadProjectile.h"
#include "Resolver/Resolver.h"

// --- Movement & Simulation ---
#include "EnginePrediction/EnginePrediction.h"
#include "Simulation/MovementSimulation/MovementSimulation.h"
#include "Simulation/ProjectileSimulation/ProjectileSimulation.h"

// --- Network ---
#include "PacketManip/PacketManip.h"
#include "PacketManip/AntiAim/AntiAim.h"
#include "PacketManip/FakeLag/FakeLag.h"
#include "NetworkFix/NetworkFix.h"

// --- Visuals ---
#include "Visuals/Visuals.h"
#include "Visuals/Chams/Chams.h"
#include "Visuals/ESP/ESP.h"
#include "Visuals/FakeAngle/FakeAngle.h"
#include "Visuals/Glow/Glow.h"
#include "Visuals/CameraWindow/CameraWindow.h"
#include "Visuals/Groups/Groups.h"
#include "Visuals/Materials/Materials.h"
#include "Visuals/Notifications/Notifications.h"
#include "Visuals/OffscreenArrows/OffscreenArrows.h"
#include "Visuals/PlayerConditions/PlayerConditions.h"
#include "Visuals/SpectatorList/SpectatorList.h"

// --- Players ---
#include "Players/PlayerCore.h"
#include "Players/PlayerUtils.h"
#include "CheaterDetection/CheaterDetection.h"

// --- System ---
#include "Binds/Binds.h"
#include "Commands/Commands.h"
#include "Configs/Configs.h"
#include "Misc/Misc.h"
#include "Misc/AutoQueue/AutoQueue.h"
#include "Misc/AutoVote/AutoVote.h"
#include "Output/Output.h"
#include "Spectate/Spectate.h"
#include "Ticks/Ticks.h"

// --- UI ---
#include "ImGui/Render.h"
#include "ImGui/Menu/Menu.h"
