#include "PlayerCore.h"

#include "PlayerUtils.h"
#include "../Configs/Configs.h"

void CPlayerlistCore::Run()
{
	static Timer tTimer = {};
	if (!tTimer.Run(1.f))
		return;

	LoadPlayerlist();
	SavePlayerlist();
}

void CPlayerlistCore::SavePlayerlist()
{
	if (!F::PlayerUtils.m_bSave || F::PlayerUtils.m_bLoad) // terrible if we end up saving while loading
		return;

	try
	{
		boost::property_tree::ptree tWrite;

		{
			boost::property_tree::ptree tSub;
			for (auto it = F::PlayerUtils.m_vTags.begin(); it != F::PlayerUtils.m_vTags.end(); it++)
			{
				int iID = std::distance(F::PlayerUtils.m_vTags.begin(), it);
				auto& tTag = *it;

				boost::property_tree::ptree tChild;
				F::Configs.SaveJson(tChild, XS("Name"), tTag.m_sName);
				F::Configs.SaveJson(tChild, XS("Color"), tTag.m_tColor);
				F::Configs.SaveJson(tChild, XS("Priority"), tTag.m_iPriority);
				F::Configs.SaveJson(tChild, XS("Label"), tTag.m_bLabel);

				tSub.put_child(std::to_string(F::PlayerUtils.IndexToTag(iID)), tChild);
			}
			tWrite.put_child(XS("Config"), tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (auto& [uAccountID, vTags] : F::PlayerUtils.m_mPlayerTags)
			{
				if (vTags.empty())
					continue;

				boost::property_tree::ptree tChild;
				for (auto& iID : vTags)
				{
					boost::property_tree::ptree t;
					t.put("", F::PlayerUtils.IndexToTag(iID));
					tChild.push_back({ "", t });
				}

				tSub.put_child(std::to_string(uAccountID), tChild);
			}
			tWrite.put_child(XS("Tags"), tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (auto& [uAccountID, sAlias] : F::PlayerUtils.m_mPlayerAliases)
			{
				if (!sAlias.empty())
					tSub.put(std::to_string(uAccountID), sAlias);
			}
			tWrite.put_child(XS("Aliases"), tSub);
		}

		write_json(F::Configs.m_sCorePath + XS("Players.json"), tWrite);

		F::PlayerUtils.m_bSave = false;
		SDK::Output(XS("Sparky"), XS("Saved playerlist"), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Save playerlist failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
	}
}

void CPlayerlistCore::LoadPlayerlist()
{
	if (!F::PlayerUtils.m_bLoad)
		return;

	try
	{
		if (!std::filesystem::exists(F::Configs.m_sCorePath + XS("Players.json")))
		{
			F::PlayerUtils.m_bLoad = false;
			return;
		}

		boost::property_tree::ptree tRead;
		read_json(F::Configs.m_sCorePath + XS("Players.json"), tRead);

		F::PlayerUtils.m_mPlayerTags.clear();
		F::PlayerUtils.m_mPlayerAliases.clear();
		F::PlayerUtils.m_vTags = {
			{ XS("Default"), { 200, 200, 200, 255 }, 0, false, false, true },
			{ XS("Ignored"), { 200, 200, 200, 255 }, -1, false, true, true },
			{ XS("Cheater"), { 255, 100, 100, 255 }, 1, false, true, true },
			{ XS("Friend"), { 100, 255, 100, 255 }, 0, true, false, true },
			{ XS("Party"), { 100, 50, 255, 255 }, 0, true, false, true },
			{ XS("F2P"), { 255, 255, 255, 255 }, 0, true, false, true }
		};

		if (auto tSub = tRead.get_child_optional(XS("Config")))
		{
			for (auto& [sName, tChild] : *tSub)
			{
				PriorityLabel_t tTag = {};
				F::Configs.LoadJson(tChild, XS("Name"), tTag.m_sName);
				F::Configs.LoadJson(tChild, XS("Color"), tTag.m_tColor);
				F::Configs.LoadJson(tChild, XS("Priority"), tTag.m_iPriority);
				F::Configs.LoadJson(tChild, XS("Label"), tTag.m_bLabel);

				int iID = F::PlayerUtils.TagToIndex(std::stoi(sName));
				if (iID > -1 && iID < F::PlayerUtils.m_vTags.size())
				{
					F::PlayerUtils.m_vTags[iID].m_sName = tTag.m_sName;
					F::PlayerUtils.m_vTags[iID].m_tColor = tTag.m_tColor;
					F::PlayerUtils.m_vTags[iID].m_iPriority = tTag.m_iPriority;
					F::PlayerUtils.m_vTags[iID].m_bLabel = tTag.m_bLabel;
				}
				else
					F::PlayerUtils.m_vTags.push_back(tTag);
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Playerlist config not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		if (auto tSub = tRead.get_child_optional(XS("Tags")))
		{
			for (auto& [sName, tChild] : *tSub)
			{
				uint32_t uAccountID = std::stoul(sName);
				for (auto& [_, tTag] : tChild)
				{
					const std::string& sTag = tTag.data();

					int iID = F::PlayerUtils.TagToIndex(std::stoi(sTag));
					auto pTag = F::PlayerUtils.GetTag(iID);
					if (!pTag || !pTag->m_bAssignable)
						continue;

					if (!F::PlayerUtils.HasTag(uAccountID, iID))
						F::PlayerUtils.AddTag(uAccountID, iID, false);
				}
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Playerlist tags not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		if (auto tSub = tRead.get_child_optional(XS("Aliases")))
		{
			for (auto& [sName, jAlias] : *tSub)
			{
				uint32_t uAccountID = std::stoul(sName);
				const std::string& sAlias = jAlias.data();

				if (!sAlias.empty())
					F::PlayerUtils.m_mPlayerAliases[uAccountID] = sAlias;
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Playerlist aliases not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		F::PlayerUtils.m_bLoad = false;
		SDK::Output(XS("Sparky"), XS("Loaded playerlist"), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Load playerlist failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
	}
}