#include "../../SDK/SDK.h"

#include "../../Features/Features.h"

MAKE_HOOK(IEngineVGui_Paint, U::Memory.GetVirtual(I::EngineVGui, 14), void,
	void* rcx, int iMode)
{
	DEBUG_RETURN(IEngineVGui_Paint, rcx, iMode);

	if (G::Unload)
		return CALL_ORIGINAL(rcx, iMode);

	if (iMode & PAINT_INGAMEPANELS && !SDK::CleanScreenshot())
	{
		H::Draw.UpdateScreenSize();
		H::Draw.UpdateW2SMatrix();
		H::Draw.Start(true);
		if (auto pLocal = H::Entities.GetLocal())
		{
			F::CameraWindow.Draw();
			F::Visuals.DrawAntiAim(pLocal);

			F::Visuals.DrawPickupTimers();
			F::ESP.Draw();
			F::Arrows.Draw(pLocal);
			F::Aimbot.Draw(pLocal);

#ifdef DEBUG_VACCINATOR
			F::AutoHeal.Draw(pLocal);
#endif
			F::NoSpreadHitscan.Draw(pLocal);
			F::PlayerConditions.Draw(pLocal);
			F::Backtrack.Draw(pLocal);
			F::SpectatorList.Draw(pLocal);
			F::CritHack.Draw(pLocal);
			F::Ticks.Draw(pLocal);
			F::Visuals.DrawDebugInfo(pLocal);
		}
		H::Draw.End();
	}

	CALL_ORIGINAL(rcx, iMode);

	if (iMode & PAINT_UIPANELS && !SDK::CleanScreenshot())
	{
		H::Draw.UpdateScreenSize();
		H::Draw.Start();
		{
			F::Notifications.Draw();
		}
		H::Draw.End();
	}
}