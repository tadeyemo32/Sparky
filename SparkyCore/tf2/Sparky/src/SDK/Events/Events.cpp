#include "Events.h"

#include "../../Core/Core.h"
#include "../../Features/Aimbot/AutoHeal/AutoHeal.h"
#include "../../Features/Backtrack/Backtrack.h"
#include "../../Features/CheaterDetection/CheaterDetection.h"
#include "../../Features/CritHack/CritHack.h"
#include "../../Features/Misc/Misc.h"
#include "../../Features/PacketManip/AntiAim/AntiAim.h"
#include "../../Features/Output/Output.h"
#include "../../Features/Resolver/Resolver.h"
#include "../../Features/Visuals/Visuals.h"

bool CEventListener::Initialize()
{
	std::vector<const char*> vEvents = { 
		XS("client_beginconnect"), XS("client_connected"), XS("client_disconnect"), XS("game_newmap"), XS("teamplay_round_start"), XS("scorestats_accumulated_update"), XS("mvm_reset_stats"), XS("player_connect_client"), XS("player_spawn"), XS("player_changeclass"), XS("player_hurt"), XS("vote_cast"), XS("item_pickup"), XS("revive_player_notify")
	};

	for (auto szEvent : vEvents)
	{
		I::GameEventManager->AddListener(this, szEvent, false);

		if (!I::GameEventManager->FindListener(this, szEvent))
		{
			U::Core.AppendFailText(XSFMT(XS("Failed to add listener: {}"), szEvent).c_str());
			m_bFailed = true;
		}
	}

	return !m_bFailed;
}

void CEventListener::Unload()
{
	I::GameEventManager->RemoveListener(this);
}

void CEventListener::FireGameEvent(IGameEvent* pEvent)
{
	if (!pEvent)
		return;

	auto pLocal = H::Entities.GetLocal();
	auto uHash = FNV1A::Hash32(pEvent->GetName());

	F::Output.Event(pEvent, uHash, pLocal);
	if (I::EngineClient->IsPlayingDemo())
		return;

	F::CritHack.Event(pEvent, uHash, pLocal);
	F::AutoHeal.Event(pEvent, uHash);
	F::Misc.Event(pEvent, uHash);
	F::Visuals.Event(pEvent, uHash);
	switch (uHash)
	{
	case FNV1A::Hash32Const(XS("player_hurt")):
	{
		F::Resolver.PlayerHurt(pEvent);
		F::CheaterDetection.ReportDamage(pEvent);
		return;
	}
	case FNV1A::Hash32Const(XS("player_spawn")):
	{
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt(XS("userid"))) != I::EngineClient->GetLocalPlayer())
			return;

		F::Backtrack.SetLerp();
		return;
	}
	case FNV1A::Hash32Const(XS("revive_player_notify")):
	{
		if (!Vars::Misc::MannVsMachine::InstantRevive.Value || pEvent->GetInt(XS("entindex")) != I::EngineClient->GetLocalPlayer())
			return;

		KeyValues* kv = new KeyValues(XS("MVM_Revive_Response"));
		kv->SetBool(XS("accepted"), true);
		I::EngineClient->ServerCmdKeyValues(kv);
	}
	}
}