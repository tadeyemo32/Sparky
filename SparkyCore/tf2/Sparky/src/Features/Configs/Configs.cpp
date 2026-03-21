#include "Configs.h"

#include "../Binds/Binds.h"
#include "../Visuals/Groups/Groups.h"
#include "../Visuals/Materials/Materials.h"

template <class T> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const T& v)
{
	t.put(s, v);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const IntRange_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("Min"), v.Min);
	SaveJson(tChild, XS("Max"), v.Max);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const FloatRange_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("Min"), v.Min);
	SaveJson(tChild, XS("Max"), v.Max);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Color_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("r"), v.r);
	SaveJson(tChild, XS("g"), v.g);
	SaveJson(tChild, XS("b"), v.b);
	SaveJson(tChild, XS("a"), v.a);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const std::vector<std::pair<std::string, Color_t>>& v)
{
	boost::property_tree::ptree tChild;
	for (auto& [m, c] : v)
	{
		boost::property_tree::ptree tLayer;
		SaveJson(tLayer, XS("Material"), m);
		SaveJson(tLayer, XS("Color"), c);

		tChild.push_back({ "", tLayer });
	}

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Gradient_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("StartColor"), v.StartColor);
	SaveJson(tChild, XS("EndColor"), v.EndColor);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const DragBox_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("x"), v.x);
	SaveJson(tChild, XS("y"), v.y);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const WindowBox_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("x"), v.x);
	SaveJson(tChild, XS("y"), v.y);
	SaveJson(tChild, XS("w"), v.w);
	SaveJson(tChild, XS("h"), v.h);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Chams_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("Visible"), v.Visible);
	SaveJson(tChild, XS("Occluded"), v.Occluded);

	t.put_child(s, tChild);
}

template <> void CConfigs::SaveJson(boost::property_tree::ptree& t, const std::string& s, const Glow_t& v)
{
	boost::property_tree::ptree tChild;
	SaveJson(tChild, XS("Stencil"), v.Stencil);
	SaveJson(tChild, XS("Blur"), v.Blur);

	t.put_child(s, tChild);
}



template <class T> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, T& v)
{
	if (auto o = t.get_optional<T>(s))
		v = *o;
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, IntRange_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("Min"), v.Min);
		LoadJson(*tChild, XS("Max"), v.Max);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, FloatRange_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("Min"), v.Min);
		LoadJson(*tChild, XS("Max"), v.Max);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Color_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("r"), v.r);
		LoadJson(*tChild, XS("g"), v.g);
		LoadJson(*tChild, XS("b"), v.b);
		LoadJson(*tChild, XS("a"), v.a);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, std::vector<std::pair<std::string, Color_t>>& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		v.clear();
		for (auto& [_, tLayer] : *tChild)
		{
			if (auto o = tLayer.get_optional<std::string>(XS("Material")))
			{
				std::string& m = *o;
				Color_t c; LoadJson(tLayer, XS("Color"), c);
				v.emplace_back(m, c);
			}
		}
	}

	// remove invalid/duplicate materials
	for (auto it = v.begin(); it != v.end();)
	{
		auto uHash = FNV1A::Hash32(it->first.c_str());
		bool bValid = uHash != FNV1A::Hash32Const(XS("None")) && (uHash == FNV1A::Hash32Const(XS("Original")) || F::Materials.m_mMaterials.contains(uHash));
		if (bValid)
		{
			int i = 0; for (auto& [s, _] : v)
			{
				auto uHash2 = FNV1A::Hash32(s.c_str());
				if (uHash == uHash2)
					i++;
			}
			bValid = i <= 1;
		}

		if (bValid)
			++it;
		else
			it = v.erase(it);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Gradient_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("StartColor"), v.StartColor);
		LoadJson(*tChild, XS("EndColor"), v.EndColor);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, DragBox_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("x"), v.x);
		LoadJson(*tChild, XS("y"), v.y);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, WindowBox_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("x"), v.x);
		LoadJson(*tChild, XS("y"), v.y);
		LoadJson(*tChild, XS("w"), v.w);
		LoadJson(*tChild, XS("h"), v.h);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Chams_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("Visible"), v.Visible);
		LoadJson(*tChild, XS("Occluded"), v.Occluded);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, Glow_t& v)
{
	if (auto tChild = t.get_child_optional(s))
	{
		LoadJson(*tChild, XS("Stencil"), v.Stencil);
		LoadJson(*tChild, XS("Blur"), v.Blur);
	}
}



template <class T> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<T>* c, int i)
{
	LoadJson(t, s, c->Map[i]);
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<int>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!c->m_vValues.empty())
	{
		if (c->m_iFlags & DROPDOWN_NOSANITIZATION)
			return;

		if (!(c->m_iFlags & DROPDOWN_MULTI))
			v = std::clamp(v, 0, int(c->m_vValues.size() - 1));
		else
		{
			for (int i = 0; i < sizeof(int) * 8; i++)
			{
				bool bFound = v & (1 << i) && i < c->m_vValues.size();
				if (!bFound)
					v &= ~(1 << i);
			}
		}
	}
	else if (c->m_sExtra)
	{
		if (!(c->m_iFlags & SLIDER_PRECISION))
			v = float(v) - fnmodf(float(v) - c->m_unStep.i / 2.f, c->m_unStep.i) + c->m_unStep.i / 2.f;
		if (c->m_iFlags & SLIDER_CLAMP)
			v = std::clamp(v, c->m_unMin.i, c->m_unMax.i);
		else if (c->m_iFlags & SLIDER_MIN)
			v = std::max(v, c->m_unMin.i);
		else if (c->m_iFlags & SLIDER_MAX)
			v = std::min(v, c->m_unMax.i);
	}
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<float>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!(c->m_iFlags & SLIDER_PRECISION))
		v = v - fnmodf(v - c->m_unStep.f / 2, c->m_unStep.f) + c->m_unStep.f / 2;
	if (c->m_iFlags & SLIDER_CLAMP)
		v = std::clamp(v, c->m_unMin.f, c->m_unMax.f);
	else if (c->m_iFlags & SLIDER_MIN)
		v = std::max(v, c->m_unMin.f);
	else if (c->m_iFlags & SLIDER_MAX)
		v = std::min(v, c->m_unMax.f);
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<IntRange_t>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!(c->m_iFlags & SLIDER_PRECISION))
	{
		v.Min = float(v.Min) - fnmodf(float(v.Min) - c->m_unStep.i / 2.f, c->m_unStep.i) + c->m_unStep.i / 2.f;
		v.Max = float(v.Max) - fnmodf(float(v.Max) - c->m_unStep.i / 2.f, c->m_unStep.i) + c->m_unStep.i / 2.f;
	}
	if (c->m_iFlags & SLIDER_CLAMP)
	{
		v.Min = std::clamp(v.Min, c->m_unMin.i, c->m_unMax.i - c->m_unStep.i);
		v.Max = std::clamp(v.Max, c->m_unMin.i + c->m_unStep.i, c->m_unMax.i);
	}
	else if (c->m_iFlags & SLIDER_MIN)
	{
		v.Min = std::max(v.Min, c->m_unMin.i);
		v.Max = std::max(v.Max, c->m_unMin.i + c->m_unStep.i);
	}
	else if (c->m_iFlags & SLIDER_MAX)
	{
		v.Min = std::min(v.Min, c->m_unMax.i - c->m_unStep.i);
		v.Max = std::min(v.Max, c->m_unMax.i);
	}
	v.Max = std::max(v.Max, v.Min + c->m_unStep.i);
}

template <> void CConfigs::LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<FloatRange_t>* c, int i)
{
	auto& v = c->Map[i];
	LoadJson(t, s, v);

	if (!(c->m_iFlags & SLIDER_PRECISION))
	{
		v.Min = v.Min - fnmodf(v.Min - c->m_unStep.f / 2, c->m_unStep.f) + c->m_unStep.f / 2;
		v.Max = v.Max - fnmodf(v.Max - c->m_unStep.f / 2, c->m_unStep.f) + c->m_unStep.f / 2;
	}
	if (c->m_iFlags & SLIDER_CLAMP)
	{
		v.Min = std::clamp(v.Min, c->m_unMin.f, c->m_unMax.f - c->m_unStep.f);
		v.Max = std::clamp(v.Max, c->m_unMin.f + c->m_unStep.f, c->m_unMax.f);
	}
	else if (c->m_iFlags & SLIDER_MIN)
	{
		v.Min = std::max(v.Min, c->m_unMin.f);
		v.Max = std::max(v.Max, c->m_unMin.f + c->m_unStep.f);
	}
	else if (c->m_iFlags & SLIDER_MAX)
	{
		v.Min = std::min(v.Min, c->m_unMax.f - c->m_unStep.f);
		v.Max = std::min(v.Max, c->m_unMax.f);
	}
	v.Max = std::max(v.Max, v.Min + c->m_unStep.f);
}



CConfigs::CConfigs()
{
	m_sConfigPath = std::filesystem::current_path().string() + XS("\\\\Sparky\\\\");
	m_sVisualsPath = m_sConfigPath + XS("Visuals\\");
	m_sCorePath = m_sConfigPath + XS("Core\\");
	m_sMaterialsPath = m_sConfigPath + XS("Materials\\");

	if (!std::filesystem::exists(m_sConfigPath))
		std::filesystem::create_directory(m_sConfigPath);

	if (!std::filesystem::exists(m_sVisualsPath))
		std::filesystem::create_directory(m_sVisualsPath);

	if (!std::filesystem::exists(m_sCorePath))
		std::filesystem::create_directory(m_sCorePath);

	if (!std::filesystem::exists(m_sMaterialsPath))
		std::filesystem::create_directory(m_sMaterialsPath);
}

#define IsType(t) pBase->m_iType == typeid(t).hash_code()

template <class T>
static inline void SaveMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	auto pVar = pBase->As<T>();

	boost::property_tree::ptree tMap;
	for (auto& [iBind, tValue] : pVar->Map)
		F::Configs.SaveJson(tMap, std::to_string(iBind), tValue);
	tTree.put_child(pVar->Name(), tMap);
}
#define Save(t, j) if (IsType(t)) SaveMain<t>(pBase, j);

template <class T>
static inline void LoadMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	auto pVar = pBase->As<T>();

	pVar->Map = { { DEFAULT_BIND, pVar->Default } };
	if (auto tMap = tTree.get_child_optional(pVar->Name()))
	{
		for (auto& [sKey, _] : *tMap)
		{
			int iBind = std::stoi(sKey);
			if (iBind == DEFAULT_BIND || F::Binds.m_vBinds.size() > iBind && !(pVar->m_iFlags & NOBIND))
			{
				F::Configs.LoadJson(*tMap, sKey, pVar, iBind);
				if (iBind != DEFAULT_BIND)
					std::next(F::Binds.m_vBinds.begin(), iBind)->m_vVars.push_back(pVar);
			}
		}
	}
	else if (!(pVar->m_iFlags & NOSAVE))
		SDK::Output(XS("Sparky"), std::vformat(XS("{} not found"), std::make_format_args( pVar->Name())).c_str(), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
}
#define Load(t, j) if (IsType(t)) LoadMain<t>(pBase, j);

bool CConfigs::SaveConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		boost::property_tree::ptree tWrite;

		{
			boost::property_tree::ptree tSub;
			for (int iID = 0; iID < F::Binds.m_vBinds.size(); iID++)
			{
				auto& tBind = F::Binds.m_vBinds[iID];

				boost::property_tree::ptree tChild;
				SaveJson(tChild, XS("Name"), tBind.m_sName);
				SaveJson(tChild, XS("Type"), tBind.m_iType);
				SaveJson(tChild, XS("Info"), tBind.m_iInfo);
				SaveJson(tChild, XS("Key"), tBind.m_iKey);
				SaveJson(tChild, XS("Enabled"), tBind.m_bEnabled);
				SaveJson(tChild, XS("Visibility"), tBind.m_iVisibility);
				SaveJson(tChild, XS("Not"), tBind.m_bNot);
				SaveJson(tChild, XS("Active"), tBind.m_bActive);
				SaveJson(tChild, XS("Parent"), tBind.m_iParent);

				tSub.put_child(std::to_string(iID), tChild);
			}
			tWrite.put_child(XS("Binds"), tSub);
		}

		{
			boost::property_tree::ptree tSub;
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				Save(bool, tSub)
				else Save(int, tSub)
				else Save(float, tSub)
				else Save(IntRange_t, tSub)
				else Save(FloatRange_t, tSub)
				else Save(std::string, tSub)
				else Save(VA_LIST(std::vector<std::pair<std::string, Color_t>>), tSub)
				else Save(Color_t, tSub)
				else Save(Gradient_t, tSub)
				else Save(DragBox_t, tSub)
				else Save(WindowBox_t, tSub)
			}
			tWrite.put_child(XS("Vars"), tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (int iID = 0; iID < F::Groups.m_vGroups.size(); iID++)
			{
				auto& tGroup = F::Groups.m_vGroups[iID];

				boost::property_tree::ptree tChild;
				SaveJson(tChild, XS("Name"), tGroup.m_sName);
				SaveJson(tChild, XS("Color"), tGroup.m_tColor);
				SaveJson(tChild, XS("TagsOverrideColor"), tGroup.m_bTagsOverrideColor);
				SaveJson(tChild, XS("Targets"), tGroup.m_iTargets);
				SaveJson(tChild, XS("Conditions"), tGroup.m_iConditions);
				SaveJson(tChild, XS("Players"), tGroup.m_iPlayers);
				SaveJson(tChild, XS("Buildings"), tGroup.m_iBuildings);
				SaveJson(tChild, XS("Projectiles"), tGroup.m_iProjectiles);
				SaveJson(tChild, XS("ESP"), tGroup.m_iESP);
				SaveJson(tChild, XS("Chams"), tGroup.m_tChams);
				SaveJson(tChild, XS("Glow"), tGroup.m_tGlow);
				SaveJson(tChild, XS("OffscreenArrows"), tGroup.m_bOffscreenArrows);
				SaveJson(tChild, XS("OffscreenArrowsOffset"), tGroup.m_iOffscreenArrowsOffset);
				SaveJson(tChild, XS("OffscreenArrowsMaxDistance"), tGroup.m_flOffscreenArrowsMaxDistance);
				SaveJson(tChild, XS("PickupTimer"), tGroup.m_bPickupTimer);
				SaveJson(tChild, XS("Backtrack"), tGroup.m_iBacktrack);
				SaveJson(tChild, XS("BacktrackChams"), tGroup.m_vBacktrackChams);
				SaveJson(tChild, XS("BacktrackGlow"), tGroup.m_tBacktrackGlow);
				SaveJson(tChild, XS("Trajectory"), tGroup.m_iTrajectory);
				SaveJson(tChild, XS("Sightlines"), tGroup.m_iSightlines);

				tSub.put_child(std::to_string(iID), tChild);
				if (F::Groups.m_vGroups.size() >= sizeof(int) * 8)
					break;
			}
			tWrite.put_child(XS("Groups"), tSub);
		}

		write_json(m_sConfigPath + sConfigName + m_sConfigExtension, tWrite);

		m_sCurrentConfig = sConfigName; m_sCurrentVisuals = "";
		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Config {} saved"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Save config failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
		return false;
	}

	return true;
}

bool CConfigs::LoadConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		if (!std::filesystem::exists(m_sConfigPath + sConfigName + m_sConfigExtension))
		{
			if (sConfigName == std::string(XS("default")))
			{
				SaveConfig(XS("default"), false);

				H::Fonts.Reload(Vars::Menu::Scale[DEFAULT_BIND]);
			}
			return false;
		}

		boost::property_tree::ptree tRead;
		read_json(m_sConfigPath + sConfigName + m_sConfigExtension, tRead);

		F::Binds.m_vBinds.clear();
		F::Groups.m_vGroups.clear();

		if (auto tSub = tRead.get_child_optional(XS("Binds")))
		{
			for (const auto& [_, tChild] : *tSub)
			{
				Bind_t tBind = {};
				LoadJson(tChild, XS("Name"), tBind.m_sName);
				LoadJson(tChild, XS("Type"), tBind.m_iType);
				LoadJson(tChild, XS("Info"), tBind.m_iInfo);
				LoadJson(tChild, XS("Key"), tBind.m_iKey);
				LoadJson(tChild, XS("Enabled"), tBind.m_bEnabled);
				LoadJson(tChild, XS("Visibility"), tBind.m_iVisibility);
				LoadJson(tChild, XS("Not"), tBind.m_bNot);
				LoadJson(tChild, XS("Active"), tBind.m_bActive);
				LoadJson(tChild, XS("Parent"), tBind.m_iParent);
				if (F::Binds.m_vBinds.size() == tBind.m_iParent)
					tBind.m_iParent = DEFAULT_BIND - 1; // prevent infinite loop

				F::Binds.m_vBinds.push_back(tBind);
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Config binds not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		if (auto tSub = tRead.get_child_optional(XS("Vars"));
			tSub || (tSub = tRead.get_child_optional(XS("ConVars"))))
		{
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				Load(bool, *tSub)
				else Load(int, *tSub)
				else Load(float, *tSub)
				else Load(IntRange_t, *tSub)
				else Load(FloatRange_t, *tSub)
				else Load(std::string, *tSub)
				else Load(VA_LIST(std::vector<std::pair<std::string, Color_t>>), *tSub)
				else Load(Color_t, *tSub)
				else Load(Gradient_t, *tSub)
				else Load(DragBox_t, *tSub)
				else Load(WindowBox_t, *tSub)
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Config vars not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		if (auto tSub = tRead.get_child_optional(XS("Groups")))
		{
			for (auto& [_, tChild] : *tSub)
			{
				Group_t tGroup = {};
				LoadJson(tChild, XS("Name"), tGroup.m_sName);
				LoadJson(tChild, XS("Color"), tGroup.m_tColor);
				LoadJson(tChild, XS("TagsOverrideColor"), tGroup.m_bTagsOverrideColor);
				LoadJson(tChild, XS("Targets"), tGroup.m_iTargets);
				LoadJson(tChild, XS("Conditions"), tGroup.m_iConditions);
				LoadJson(tChild, XS("Players"), tGroup.m_iPlayers);
				LoadJson(tChild, XS("Buildings"), tGroup.m_iBuildings);
				LoadJson(tChild, XS("Projectiles"), tGroup.m_iProjectiles);
				LoadJson(tChild, XS("ESP"), tGroup.m_iESP);
				LoadJson(tChild, XS("Chams"), tGroup.m_tChams);
				LoadJson(tChild, XS("Glow"), tGroup.m_tGlow);
				LoadJson(tChild, XS("OffscreenArrows"), tGroup.m_bOffscreenArrows);
				LoadJson(tChild, XS("OffscreenArrowsOffset"), tGroup.m_iOffscreenArrowsOffset);
				LoadJson(tChild, XS("OffscreenArrowsMaxDistance"), tGroup.m_flOffscreenArrowsMaxDistance);
				LoadJson(tChild, XS("PickupTimer"), tGroup.m_bPickupTimer);
				LoadJson(tChild, XS("Backtrack"), tGroup.m_iBacktrack);
				LoadJson(tChild, XS("BacktrackChams"), tGroup.m_vBacktrackChams);
				LoadJson(tChild, XS("BacktrackGlow"), tGroup.m_tBacktrackGlow);
				LoadJson(tChild, XS("Trajectory"), tGroup.m_iTrajectory);
				LoadJson(tChild, XS("Sightlines"), tGroup.m_iSightlines);

				F::Groups.m_vGroups.push_back(tGroup);
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Config groups not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		F::Binds.SetVars(nullptr, nullptr, false);
		H::Fonts.Reload(Vars::Menu::Scale[DEFAULT_BIND]);

		m_sCurrentConfig = sConfigName; m_sCurrentVisuals = "";
		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Config {} loaded"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Load config failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
		return false;
	}

	return true;
}

template <class T>
static inline void SaveMiscMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	F::Configs.SaveJson(tTree, pBase->Name(), pBase->As<T>()->Map[DEFAULT_BIND]);
}
#define SaveMisc(t, j) if (IsType(t)) SaveMiscMain<t>(pBase, j);

template <class T>
static inline void LoadMiscMain(BaseVar*& pBase, boost::property_tree::ptree& tTree)
{
	F::Configs.LoadJson(tTree, pBase->Name(), pBase->As<T>(), DEFAULT_BIND);
}
#define LoadMisc(t, j) if (IsType(t)) LoadMiscMain<t>(pBase, j);

bool CConfigs::SaveVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		boost::property_tree::ptree tWrite;

		{
			boost::property_tree::ptree tSub;
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!(pBase->m_iFlags & VISUAL) || !bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				SaveMisc(bool, tSub)
				else SaveMisc(int, tSub)
				else SaveMisc(float, tSub)
				else SaveMisc(IntRange_t, tSub)
				else SaveMisc(FloatRange_t, tSub)
				else SaveMisc(std::string, tSub)
				else SaveMisc(VA_LIST(std::vector<std::pair<std::string, Color_t>>), tSub)
				else SaveMisc(Color_t, tSub)
				else SaveMisc(Gradient_t, tSub)
				else SaveMisc(DragBox_t, tSub)
				else SaveMisc(WindowBox_t, tSub)
			}
			tWrite.put_child(XS("Vars"), tSub);
		}

		{
			boost::property_tree::ptree tSub;
			for (int iID = 0; iID < F::Groups.m_vGroups.size(); iID++)
			{
				auto& tGroup = F::Groups.m_vGroups[iID];

				boost::property_tree::ptree tChild;
				SaveJson(tChild, XS("Name"), tGroup.m_sName);
				SaveJson(tChild, XS("Color"), tGroup.m_tColor);
				SaveJson(tChild, XS("TagsOverrideColor"), tGroup.m_bTagsOverrideColor);
				SaveJson(tChild, XS("Targets"), tGroup.m_iTargets);
				SaveJson(tChild, XS("Conditions"), tGroup.m_iConditions);
				SaveJson(tChild, XS("Players"), tGroup.m_iPlayers);
				SaveJson(tChild, XS("Buildings"), tGroup.m_iBuildings);
				SaveJson(tChild, XS("Projectiles"), tGroup.m_iProjectiles);
				SaveJson(tChild, XS("ESP"), tGroup.m_iESP);
				SaveJson(tChild, XS("Chams"), tGroup.m_tChams);
				SaveJson(tChild, XS("Glow"), tGroup.m_tGlow);
				SaveJson(tChild, XS("OffscreenArrows"), tGroup.m_bOffscreenArrows);
				SaveJson(tChild, XS("OffscreenArrowsOffset"), tGroup.m_iOffscreenArrowsOffset);
				SaveJson(tChild, XS("OffscreenArrowsMaxDistance"), tGroup.m_flOffscreenArrowsMaxDistance);
				SaveJson(tChild, XS("PickupTimer"), tGroup.m_bPickupTimer);
				SaveJson(tChild, XS("Backtrack"), tGroup.m_iBacktrack);
				SaveJson(tChild, XS("BacktrackChams"), tGroup.m_vBacktrackChams);
				SaveJson(tChild, XS("BacktrackGlow"), tGroup.m_tBacktrackGlow);
				SaveJson(tChild, XS("Trajectory"), tGroup.m_iTrajectory);
				SaveJson(tChild, XS("Sightlines"), tGroup.m_iSightlines);

				tSub.put_child(std::to_string(iID), tChild);
				if (F::Groups.m_vGroups.size() >= sizeof(int) * 8)
					break;
			}
			tWrite.put_child(XS("Groups"), tSub);
		}

		write_json(m_sVisualsPath + sConfigName + m_sConfigExtension, tWrite);

		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Visual config {} saved"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Save visuals failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
		return false;
	}
	return true;
}

bool CConfigs::LoadVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		if (!std::filesystem::exists(m_sVisualsPath + sConfigName + m_sConfigExtension))
			return false;

		boost::property_tree::ptree tRead;
		read_json(m_sVisualsPath + sConfigName + m_sConfigExtension, tRead);

		F::Groups.m_vGroups.clear();

		if (auto tSub = tRead.get_child_optional(XS("Vars"));
			tSub || (tSub = tRead))
		{
			bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
			for (auto& pBase : G::Vars)
			{
				if (!(pBase->m_iFlags & VISUAL) || !bNoSave && pBase->m_iFlags & NOSAVE)
					continue;

				LoadMisc(bool, *tSub)
				else LoadMisc(int, *tSub)
				else LoadMisc(float, *tSub)
				else LoadMisc(IntRange_t, *tSub)
				else LoadMisc(FloatRange_t, *tSub)
				else LoadMisc(std::string, *tSub)
				else LoadMisc(VA_LIST(std::vector<std::pair<std::string, Color_t>>), *tSub)
				else LoadMisc(Color_t, *tSub)
				else LoadMisc(Gradient_t, *tSub)
				else LoadMisc(DragBox_t, *tSub)
				else LoadMisc(WindowBox_t, *tSub)
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Config vars not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		if (auto tSub = tRead.get_child_optional(XS("Groups")))
		{
			for (auto& [_, tChild] : *tSub)
			{
				Group_t tGroup = {};
				LoadJson(tChild, XS("Name"), tGroup.m_sName);
				LoadJson(tChild, XS("Color"), tGroup.m_tColor);
				LoadJson(tChild, XS("TagsOverrideColor"), tGroup.m_bTagsOverrideColor);
				LoadJson(tChild, XS("Targets"), tGroup.m_iTargets);
				LoadJson(tChild, XS("Conditions"), tGroup.m_iConditions);
				LoadJson(tChild, XS("Players"), tGroup.m_iPlayers);
				LoadJson(tChild, XS("Buildings"), tGroup.m_iBuildings);
				LoadJson(tChild, XS("Projectiles"), tGroup.m_iProjectiles);
				LoadJson(tChild, XS("ESP"), tGroup.m_iESP);
				LoadJson(tChild, XS("Chams"), tGroup.m_tChams);
				LoadJson(tChild, XS("Glow"), tGroup.m_tGlow);
				LoadJson(tChild, XS("OffscreenArrows"), tGroup.m_bOffscreenArrows);
				LoadJson(tChild, XS("OffscreenArrowsOffset"), tGroup.m_iOffscreenArrowsOffset);
				LoadJson(tChild, XS("OffscreenArrowsMaxDistance"), tGroup.m_flOffscreenArrowsMaxDistance);
				LoadJson(tChild, XS("PickupTimer"), tGroup.m_bPickupTimer);
				LoadJson(tChild, XS("Backtrack"), tGroup.m_iBacktrack);
				LoadJson(tChild, XS("BacktrackChams"), tGroup.m_vBacktrackChams);
				LoadJson(tChild, XS("BacktrackGlow"), tGroup.m_tBacktrackGlow);
				LoadJson(tChild, XS("Trajectory"), tGroup.m_iTrajectory);
				LoadJson(tChild, XS("Sightlines"), tGroup.m_iSightlines);

				F::Groups.m_vGroups.push_back(tGroup);
			}
		}
		else
			SDK::Output(XS("Sparky"), XS("Config groups not found"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);

		F::Binds.SetVars(nullptr, nullptr, false);

		m_sCurrentVisuals = sConfigName;
		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Visual config {} loaded"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Load visuals failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
		return false;
	}
	return true;
}

template <class T>
static inline void ResetMain(BaseVar*& pBase)
{
	auto pVar = pBase->As<T>();

	pVar->Map = { { DEFAULT_BIND, pVar->Default } };
}
#define Reset(t) if (IsType(t)) ResetMain<t>(pBase);

void CConfigs::DeleteConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		if (FNV1A::Hash32(sConfigName.c_str()) == FNV1A::Hash32Const(XS("default")))
		{
			ResetConfig(sConfigName);
			return;
		}

		std::filesystem::remove(m_sConfigPath + sConfigName + m_sConfigExtension);
		if (FNV1A::Hash32(m_sCurrentConfig.c_str()) == FNV1A::Hash32(sConfigName.c_str()))
			LoadConfig(XS("default"), false);

		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Config {} deleted"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Remove config failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
	}
}

void CConfigs::ResetConfig(const std::string& sConfigName, bool bNotify)
{
	try
	{
		F::Binds.m_vBinds.clear();
		F::Groups.m_vGroups.clear();

		bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
		for (auto& pBase : G::Vars)
		{
			if (!bNoSave && pBase->m_iFlags & NOSAVE)
				continue;

			Reset(bool)
			else Reset(int)
			else Reset(float)
			else Reset(IntRange_t)
			else Reset(FloatRange_t)
			else Reset(std::string)
			else Reset(std::vector<std::string>)
			else Reset(Color_t)
			else Reset(Gradient_t)
			else Reset(DragBox_t)
			else Reset(WindowBox_t)
		}

		SaveConfig(sConfigName, false);
		F::Binds.SetVars(nullptr, nullptr, false);
		H::Fonts.Reload(Vars::Menu::Scale[DEFAULT_BIND]);

		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Config {} reset"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Reset config failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
	}
}

void CConfigs::DeleteVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		std::filesystem::remove(m_sVisualsPath + sConfigName + m_sConfigExtension);

		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Visual config {} deleted"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Remove visuals failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
	}
}

void CConfigs::ResetVisual(const std::string& sConfigName, bool bNotify)
{
	try
	{
		F::Groups.m_vGroups.clear();

		bool bNoSave = GetAsyncKeyState(VK_SHIFT) & 0x8000;
		for (auto& pBase : G::Vars)
		{
			if (!(pBase->m_iFlags & VISUAL) || !bNoSave && pBase->m_iFlags & NOSAVE)
				continue;

			Reset(bool)
			else Reset(int)
			else Reset(float)
			else Reset(IntRange_t)
			else Reset(FloatRange_t)
			else Reset(std::string)
			else Reset(std::vector<std::string>)
			else Reset(Color_t)
			else Reset(Gradient_t)
			else Reset(DragBox_t)
			else Reset(WindowBox_t)
		}

		SaveVisual(sConfigName, false);
		F::Binds.SetVars(nullptr, nullptr, false);

		if (bNotify)
			SDK::Output(XS("Sparky"), std::vformat(XS("Visual config {} reset"), std::make_format_args( sConfigName)).c_str(), DEFAULT_COLOR, OUTPUT_CONSOLE | OUTPUT_TOAST | OUTPUT_MENU | OUTPUT_DEBUG);
	}
	catch (...)
	{
		SDK::Output(XS("Sparky"), XS("Reset visuals failed"), ALTERNATE_COLOR, OUTPUT_CONSOLE | OUTPUT_MENU | OUTPUT_DEBUG);
	}
}