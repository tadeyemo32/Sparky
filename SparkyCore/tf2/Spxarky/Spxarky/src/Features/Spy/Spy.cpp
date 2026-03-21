#include "Spy.h"
#include "../Ticks/Ticks.h"

void CSpy::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_iClass() != TF_CLASS_SPY)
		return;

	// Fast Cloak & Uncloak Logic
	// Purpose: Reduce the desync or delay during cloak transitions.
	
	const bool bIsCloaked = pLocal->InCond(TF_COND_STEALTHED);
	static bool bLastCloak = false;

	// Fast Cloak (Transitioning to Cloaked)
	if (Vars::Misc::Spy::FastCloak.Value && !bIsCloaked && (pCmd->buttons & IN_ATTACK2))
	{
		// Pulse-based cloak acceleration
		if (I::GlobalVars->tickcount % 2 == 0)
			pCmd->buttons &= ~IN_ATTACK2;
	}

	// Fast Decloak (Transitioning to Uncloaked)
	// Often called "Fast Uncloak" in TF2 community.
	if (Vars::Misc::Spy::FastUncloak.Value && bIsCloaked && (pCmd->buttons & IN_ATTACK2))
	{
		// Force uncloak transition optimization
		if (I::GlobalVars->tickcount % 2 == 0)
			pCmd->buttons &= ~IN_ATTACK2;
            
        // Some servers allow attacking earlier if IN_ATTACK is also pulsed
        // pCmd->buttons |= IN_ATTACK; // EXPERIMENTAL
	}

	bLastCloak = bIsCloaked;
}
