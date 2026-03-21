#include "Troldier.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../../SDK/SDK.h"
#include "../../../SDK/Vars.h"

void CTroldier::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Troldier::Enabled.Value)
		return;

	if (!pLocal || !pLocal->IsAlive() || pLocal->m_iClass() != TF_CLASS_SOLDIER)
		return;

	if (!pWeapon || pWeapon->GetSlot() != SLOT_MELEE)
		return;

	if (!pLocal->InCond(TF_COND_BLASTJUMPING))
		return;

	const float flRange = pWeapon->GetSwingRange() * pLocal->m_flModelScale();
	const int iSwingTicks = ceilf(pWeapon->GetSmackDelay() / TICK_INTERVAL);

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pTarget = pEntity->As<CTFPlayer>();
		if (!pTarget || !pTarget->IsAlive() || pTarget->IsDummy() || pTarget->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		// Predict positions
		MoveStorage tLocalMove, tTargetMove;
		F::MoveSim.Initialize(pLocal, tLocalMove);
		F::MoveSim.Initialize(pTarget, tTargetMove);

		for (int i = 0; i < iSwingTicks; i++)
		{
			F::MoveSim.RunTick(tLocalMove);
			F::MoveSim.RunTick(tTargetMove);
		}

		const Vec3 vLocalPos = tLocalMove.m_MoveData.m_vecAbsOrigin + pLocal->m_vecViewOffset();
		const Vec3 vTargetPos = tTargetMove.m_MoveData.m_vecAbsOrigin + (pTarget->GetCenter() - pTarget->GetAbsOrigin());

		if (vLocalPos.DistTo(vTargetPos) <= flRange + 15.f) // Buffer for hull size
		{
			if (Vars::Troldier::AutoSwing.Value)
				pCmd->buttons |= IN_ATTACK;

			if (Vars::Troldier::Silent.Value && (pCmd->buttons & IN_ATTACK))
			{
				Vec3 vAngle = Math::CalcAngle(pLocal->GetShootPos(), pTarget->GetCenter());
				SDK::FixMovement(pCmd, vAngle);
				pCmd->viewangles = vAngle;
				G::PSilentAngles = true;
			}
			
			F::MoveSim.Restore(tLocalMove);
			F::MoveSim.Restore(tTargetMove);
			break;
		}

		F::MoveSim.Restore(tLocalMove);
		F::MoveSim.Restore(tTargetMove);
	}
}
