#include "Commands.h"

#include "../../Core/Core.h"
#include "../ImGui/Menu/Menu.h"
#include <utility>
#include <boost/algorithm/string/replace.hpp>

#define AddCommand(sCommand, fCommand) \
{ \
	FNV1A::Hash32Const(sCommand), \
	[](const std::deque<const char*>& vArgs) \
		fCommand \
},

static std::unordered_map<uint32_t, CommandCallback> s_mCommands = {
	AddCommand(XS("setcvar"),
	{
		if (vArgs.size() < 2)
		{
			SDK::Output(XS("Usage:\n\tsetcvar <cvar> <value>"));
			return;
		}

		const char* sCVar = vArgs[0];
		auto pCVar = I::CVar->FindVar(sCVar);
		if (!pCVar)
		{
			SDK::Output(std::vformat(XS("Could not find {}"), std::make_format_args( sCVar)).c_str());
			return;
		}

		std::string sValue = "";
		for (int i = 1; i < vArgs.size(); i++)
			sValue += std::vformat(XS("{} "), std::make_format_args( vArgs[i]));
		sValue.pop_back();
		boost::replace_all(sValue, XS("\""), "");

		pCVar->SetValue(sValue.c_str());
		SDK::Output(std::vformat(XS("Set {} to {}"), std::make_format_args( sCVar, sValue)).c_str());
	})
	AddCommand(XS("getcvar"),
	{
		if (vArgs.size() != 1)
		{
			SDK::Output(XS("Usage:\n\tgetcvar <cvar>"));
			return;
		}

		const char* sCVar = vArgs[0];
		auto pCVar = I::CVar->FindVar(sCVar);
		if (!pCVar)
		{
			SDK::Output(std::vformat(XS("Could not find {}"), std::make_format_args( sCVar)).c_str());
			return;
		}

		SDK::Output(std::vformat(XS("Value of {} is {}"), std::make_format_args( sCVar, pCVar->GetString())).c_str());
	})
	AddCommand(XS("queue"),
	{
		if (!I::TFPartyClient->AnySelected())
			I::TFPartyClient->LoadSavedCasualCriteria();
		I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
	})
	AddCommand(XS("clear_chat"),
	{
		I::ClientModeShared->m_pChatElement->SetText("");
	})
	AddCommand(XS("menu"),
	{
		I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = !F::Menu.m_bIsOpen);
	})
	AddCommand(XS("unload"),
	{
		if (F::Menu.m_bIsOpen)
			I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = false);
		U::Core.m_bUnload = true;
	})
	AddCommand(XS("crash"),
	{	// if you want to time out of a server and rejoin
		switch (vArgs.empty() ? 0 : FNV1A::Hash32(vArgs.front()))
		{
		case FNV1A::Hash32Const(XS("true")):
		case FNV1A::Hash32Const(XS("t")):
		case FNV1A::Hash32Const(XS("1")):
			break;
		default:
			Vars::Debug::CrashLogging.Value = false; // we are voluntarily crashing, don't give out log if we don't want one
		}
		reinterpret_cast<void(*)()>(0)();
	})
};

bool CCommands::Run(const char* sCmd, std::deque<const char*>& vArgs)
{
	std::string sLower = sCmd;
	std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);

	auto uHash = FNV1A::Hash32(sLower.c_str());
	if (!s_mCommands.contains(uHash))
		return false;

	s_mCommands[uHash](vArgs);
	return true;
}