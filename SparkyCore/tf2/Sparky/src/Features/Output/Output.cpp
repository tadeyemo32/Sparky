#include "Output.h"

#include "../Visuals/Notifications/Notifications.h"
#include "../Players/PlayerUtils.h"

static std::string s_sRed =		Color_t(255, 100, 100).ToHex();
static std::string s_sGreen =		Color_t(100, 255, 100).ToHex();
static std::string s_sYellow =	Color_t(200, 169, 0).ToHex();

static inline void OutputInfo(int iFlags, const char* sName, const char* sOutput, const char* sChat)
{
	int iTo = (iFlags & Vars::Logging::LogToEnum::Console ? OUTPUT_CONSOLE : 0)
			| (iFlags & Vars::Logging::LogToEnum::Debug ? OUTPUT_DEBUG : 0)
			| (iFlags & Vars::Logging::LogToEnum::Toasts ? OUTPUT_TOAST : 0)
			| (iFlags & Vars::Logging::LogToEnum::Menu ? OUTPUT_MENU : 0)
			| (iFlags & Vars::Logging::LogToEnum::Party ? OUTPUT_PARTY : 0);
	if (iTo)
		SDK::Output(sName, sOutput, Vars::Menu::Theme::Accent.Value, iTo);

	iTo = (iFlags & Vars::Logging::LogToEnum::Chat ? OUTPUT_CHAT : 0);
	if (iTo)
		SDK::Output(Vars::Menu::CheatTag.Value.c_str(), sChat, Vars::Menu::Theme::Accent.Value, iTo, -1, "", "");
}

// Event info
void COutput::Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal)
{
	if (uHash == FNV1A::Hash32Const(XS("game_newmap")))
	{
		m_bInfoOnJoin = true;
		return;
	}

	if (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame() || !pLocal)
		return;

	switch (uHash)
	{
	case FNV1A::Hash32Const(XS("vote_cast")): // Voting
	{
		if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::VoteCast))
			return;

		int iIndex = pEvent->GetInt(XS("entityid"));
		auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
		if (!pEntity || pEntity->GetClassID() != ETFClassID::CTFPlayer)
			return;

		auto pResource = H::Entities.GetResource();
		if (!pResource || pResource->IsFakePlayer(iIndex))
			return;

		auto sName = F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex));
		bool bVotedYes = pEvent->GetInt(XS("vote_option")) == 0;
		bool bSameTeam = pEntity->As<CTFPlayer>()->m_iTeamNum() == pLocal->m_iTeamNum();
		OutputInfo(Vars::Logging::VoteCast::LogTo.Value, XS("Vote Cast"),
			std::vformat(XS("{}{} voted {}"), std::make_format_args( (bSameTeam ? "" : XS("[Enemy] ")), (sName), (bVotedYes ? XS("Yes") : XS("No")))).c_str(),
			std::vformat(XS("{}{}{}\x1 voted {}{}"), std::make_format_args( (bSameTeam ? "" : XS("[Enemy] ")), (s_sYellow), (sName), (bVotedYes ? s_sGreen : s_sRed), (bVotedYes ? XS("Yes") : XS("No")))).c_str()
		);

		return;
	}
	case FNV1A::Hash32Const(XS("player_changeclass")): // Class change
	{
		if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::ClassChanges))
			return;

		int iIndex = I::EngineClient->GetPlayerForUserID(pEvent->GetInt(XS("userid")));
		auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
		if (!pEntity || iIndex == I::EngineClient->GetLocalPlayer())
			return;

		auto pResource = H::Entities.GetResource();
		if (!pResource || pResource->IsFakePlayer(iIndex))
			return; // dont spam chat by giving class changes for bots

		auto sName = F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex));
		bool bSameTeam = pEntity->As<CTFPlayer>()->m_iTeamNum() == pLocal->m_iTeamNum();
		OutputInfo(Vars::Logging::ClassChange::LogTo.Value, XS("Class Change"),
			std::vformat(XS("{}{} changed class to {}"), std::make_format_args( (bSameTeam ? "" : XS("[Enemy] ")), (sName), (SDK::GetClassByIndex(pEvent->GetInt(XS("class")))))).c_str(),
			std::vformat(XS("{}{}{}\x1 changed class to {}{}"), std::make_format_args( (bSameTeam ? "" : XS("[Enemy] ")), (s_sYellow), (sName), (s_sYellow), (SDK::GetClassByIndex(pEvent->GetInt(XS("class")))))).c_str()
		);

		return;
	}
	case FNV1A::Hash32Const(XS("player_hurt")): // Damage
	{
		if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Damage))
			return;

		auto pResource = H::Entities.GetResource();
		if (!pResource)
			return;

		int nAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt(XS("attacker")));
		if (I::EngineClient->GetLocalPlayer() != nAttacker)
			return;

		int iIndex = I::EngineClient->GetPlayerForUserID(pEvent->GetInt(XS("userid")));
		auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
		if (!pEntity || iIndex == I::EngineClient->GetLocalPlayer())
			return;

		int nHealth = pEvent->GetInt(XS("health"));
		int nDamage = pEvent->GetInt(XS("damageamount"));
		bool bCrit = pEvent->GetBool(XS("crit"));
		bool bMinicrit = pEvent->GetBool(XS("minicrit"));
		int iMaxHealth = pEntity->As<CTFPlayer>()->GetMaxHealth();

		auto sName = F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex));
		OutputInfo(Vars::Logging::Damage::LogTo.Value, XS("Damage"),
			std::vformat(XS("Hit {} for {} damage ({} / {}{})"), std::make_format_args( (sName), (nDamage), (nHealth), (iMaxHealth), (bCrit ? XS(", crit") : bMinicrit ? XS(", minicrit") : ""))).c_str(),
			std::vformat(XS("Hit {}{}\x1 for {}{} damage{} ({} / {}{})"), std::make_format_args( (s_sYellow), (sName), (s_sRed), (nDamage), (s_sYellow), (nHealth), (iMaxHealth), (bCrit ? XS(", crit") : bMinicrit ? XS(", minicrit") : ""))).c_str()
		);

		return;
	}
	case FNV1A::Hash32Const(XS("player_connect_client")): // tags/alias (player join)
	{
		if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Tags) && !(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Aliases) || m_bInfoOnJoin)
			return;

		std::string sID = pEvent->GetString(XS("networkid"));
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt(XS("userid"))) == I::EngineClient->GetLocalPlayer()
			|| FNV1A::Hash32(sID.c_str()) == FNV1A::Hash32Const(XS("BOT")))
			return;

		try
		{
			sID.replace(0, 5, "");
			sID.pop_back();
			uint32_t uAccountID = std::stoul(sID);
			if (H::Entities.InParty(uAccountID)) // ignore party
				return;

			auto sName = pEvent->GetString(XS("name"));
			TagsOnJoin(sName, uAccountID);
			AliasOnJoin(sName, uAccountID);
		}
		catch (...) {}

		return;
	}
	case FNV1A::Hash32Const(XS("player_spawn")): // tags/alias (local join)
	{
		if (!(Vars::Logging::Logs.Value & (1 << 5)) && !(Vars::Logging::Logs.Value & (1 << 6)) || !m_bInfoOnJoin)
			return;

		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt(XS("userid"))) != I::EngineClient->GetLocalPlayer())
			return;

		m_bInfoOnJoin = false;
		auto pResource = H::Entities.GetResource();
		if (!pResource)
			return;

		for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
		{
			if (!pResource->m_bValid(n) || pResource->IsFakePlayer(n) || n == I::EngineClient->GetLocalPlayer()
				|| H::Entities.InParty(n)) // ignore party
				continue;

			auto sName = pResource->GetName(n);
			uint32_t uAccountID = pResource->m_iAccountID(n);
			TagsOnJoin(sName, uAccountID);
			AliasOnJoin(sName, uAccountID);
		}
	}
	}
}

// Vote start
void COutput::UserMessage(bf_read& msgData)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::VoteStart))
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	int iTeam = msgData.ReadByte();
	/*int iVoteID =*/ msgData.ReadLong();
	int iCaller = msgData.ReadByte();
	char sReason[256]; msgData.ReadString(sReason, sizeof(sReason));
	char sTarget[256]; msgData.ReadString(sTarget, sizeof(sTarget));
	int iTarget = msgData.ReadByte() >> 1;
	msgData.Seek(0);
	if (!iCaller || !iTarget)
		return;

	bool bSameTeam = iTeam == pLocal->m_iTeamNum();
	auto sCallerName = F::PlayerUtils.GetPlayerName(iCaller, pResource->GetName(iCaller));
	auto sTargetName = F::PlayerUtils.GetPlayerName(iTarget, pResource->GetName(iTarget));
	OutputInfo(Vars::Logging::VoteStart::LogTo.Value, XS("Vote Start"),
		std::vformat(XS("{}{} called a vote on {}"), std::make_format_args( (bSameTeam ? "" : XS("[Enemy] ")), (sCallerName), (sTargetName))).c_str(),
		std::vformat(XS("{}{}{}\x1 called a vote on {}{}"), std::make_format_args( (bSameTeam ? "" : XS("[Enemy] ")), (s_sYellow), (sCallerName), (s_sYellow), (sTargetName))).c_str()
	);
}

// Cheat detection
void COutput::CheatDetection(const char* sName, const char* sAction, const char* sReason)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::CheatDetection))
		return;

	OutputInfo(Vars::Logging::CheatDetection::LogTo.Value, XS("Cheat Detection"),
		std::vformat(XS("{} {} for {}"), std::make_format_args( (sName), (sAction), (sReason))).c_str(),
		std::vformat(XS("{}{}\x1 {} for {}{}"), std::make_format_args( (s_sYellow), (sName), (sAction), (s_sYellow), (sReason))).c_str()
	);
}

// Tags
void COutput::TagsOnJoin(const char* sName, uint32_t uAccountID)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Tags))
		return;

	if (!F::PlayerUtils.m_mPlayerTags.contains(uAccountID))
		return;

	std::vector<std::pair<std::string, const char*>> vColorsTags = {};
	for (auto& iID : F::PlayerUtils.m_mPlayerTags[uAccountID])
	{
		if (auto pTag = F::PlayerUtils.GetTag(iID))
			vColorsTags.emplace_back(pTag->m_tColor.ToHexA(), pTag->m_sName.c_str());
	}

	std::string sOutputText, sChatText;
	switch (vColorsTags.size())
	{
	case 0: return;
	case 1:
	{
		auto& pColorTag = vColorsTags.front();
		sOutputText = pColorTag.second;
		sChatText = std::vformat(XS("{}{}"), std::make_format_args( pColorTag.first, pColorTag.second));
		break;
	}
	case 2:
	{
		auto& pColorTag1 = vColorsTags.front(), &pColorTag2 = vColorsTags.back();
		sOutputText = std::vformat(XS("{} and {}"), std::make_format_args( pColorTag1.second, pColorTag2.second));
		sChatText = std::vformat(XS("{}{}\x1 and {}{}"), std::make_format_args( pColorTag1.first, pColorTag1.second, pColorTag2.first, pColorTag2.second));
		break;
	}
	default:
	{
		for (auto it = vColorsTags.begin(); it != vColorsTags.end(); it++)
		{
			auto& pColorTag = *it;
			if (it + 1 != vColorsTags.end())
			{
				sOutputText += std::vformat(XS("{}, "), std::make_format_args( pColorTag.second));
				sChatText += std::vformat(XS("{}{}\x1, "), std::make_format_args( pColorTag.first, pColorTag.second));
			}
			else
			{
				sOutputText += std::vformat(XS("and {}"), std::make_format_args( pColorTag.second));
				sChatText += std::vformat(XS("and {}{}"), std::make_format_args( pColorTag.first, pColorTag.second));
			}
		}
	}
	}

	OutputInfo(Vars::Logging::Tags::LogTo.Value, XS("Tags"),
		std::vformat(XS("{} has the {} {}"), std::make_format_args( (sName), (vColorsTags.size() == 1 ? XS("tag") : XS("tags")), (sOutputText))).c_str(),
		std::vformat(XS("{}{}\x1 has the {} {}"), std::make_format_args( (s_sYellow), (sName), (vColorsTags.size() == 1 ? XS("tag") : XS("tags")), (sChatText))).c_str()
	);
}
void COutput::TagsChanged(const char* sName, const char* sAction, const char* sColor, const char* sTag)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Tags))
		return;

	auto uHash = FNV1A::Hash32(sAction);
	OutputInfo(Vars::Logging::Tags::LogTo.Value, XS("Tags"),
		std::vformat(XS("{} tag {} {} {}"), std::make_format_args( (sAction), (sTag), (uHash == FNV1A::Hash32Const(XS("Added")) ? XS("to") : XS("from")), (sName))).c_str(),
		std::vformat(XS("{} tag {}{}\x1 {} {}{}"), std::make_format_args( (sAction), (sColor), (sTag), (uHash == FNV1A::Hash32Const(XS("Added")) ? XS("to") : XS("from")), (s_sYellow), (sName))).c_str()
	);
}

// Aliases
void COutput::AliasOnJoin(const char* sName, uint32_t uAccountID)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Aliases))
		return;

	if (!F::PlayerUtils.m_mPlayerAliases.contains(uAccountID))
		return;

	auto& sAlias = F::PlayerUtils.m_mPlayerAliases[uAccountID];

	OutputInfo(Vars::Logging::Tags::LogTo.Value, XS("Aliases"),
		std::vformat(XS("{} has the alias \"{}\""), std::make_format_args( (sName), (sAlias))).c_str(),
		std::vformat(XS("{}{}\x1 has the alias \"{}{}\x1\""), std::make_format_args( (s_sYellow), (sName), (s_sYellow), (sAlias))).c_str()
	);
}
void COutput::AliasChanged(const char* sName, const char* sAction, const char* sAlias)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Aliases))
		return;

	auto uHash = FNV1A::Hash32(sAction);
	OutputInfo(Vars::Logging::Tags::LogTo.Value, XS("Aliases"),
		std::vformat(XS("{} {}'s alias {} \"{}\""), std::make_format_args( (sAction), (sName), (uHash == FNV1A::Hash32Const(XS("Changed")) ? XS("to") : XS("of")), (sAlias))).c_str(),
		std::vformat(XS("{} {}{}\x1's alias {} \"{}{}\x1\""), std::make_format_args( (sAction), (s_sYellow), (sName), (uHash == FNV1A::Hash32Const(XS("Changed")) ? XS("to") : XS("of")), (s_sYellow), (sAlias))).c_str()
	);
}

void COutput::ReportResolver(int iIndex, const char* sAction, const char* sAxis, float flValue)
{
	ReportResolver(iIndex, sAction, sAxis, std::vformat(XS("{}"), std::make_format_args( flValue)).c_str());
}
void COutput::ReportResolver(int iIndex, const char* sAction, const char* sAxis, bool bValue)
{
	ReportResolver(iIndex, sAction, sAxis, std::vformat(XS("{}"), std::make_format_args( bValue)).c_str());
}
void COutput::ReportResolver(int iIndex, const char* sAction, const char* sAxis, const char* sValue)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Resolver))
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	auto sName = F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex));
	OutputInfo(Vars::Logging::Tags::LogTo.Value, XS("Resolver"),
		std::vformat(XS("{} {} of {} to {}"), std::make_format_args( (sAction), (sAxis), (sName), (sValue))).c_str(),
		std::vformat(XS("{} {}{}\x1 of {}{}\x1 to {}{}\x1"), std::make_format_args( (sAction), (s_sYellow), (sAxis), (s_sYellow), (sName), (s_sYellow), (sValue))).c_str()
	);
}
void COutput::ReportResolver(const char* sMessage)
{
	if (!(Vars::Logging::Logs.Value & Vars::Logging::LogsEnum::Resolver))
		return;

	OutputInfo(Vars::Logging::Tags::LogTo.Value, XS("Resolver"), sMessage, sMessage);
}