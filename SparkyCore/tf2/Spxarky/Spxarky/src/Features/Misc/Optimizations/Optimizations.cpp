#include "Optimizations.h"

void COptimizations::Run()
{
	if (!I::CVar) return;

	static auto cl_gib_allow = I::CVar->FindVar("cl_gib_allow");
	static auto cl_burninggibs = I::CVar->FindVar("cl_burninggibs");
	static auto cl_ragdoll_fade_time = I::CVar->FindVar("cl_ragdoll_fade_time");
	static auto cl_ragdoll_forcefade = I::CVar->FindVar("cl_ragdoll_forcefade");
	static auto r_decals = I::CVar->FindVar("r_decals");
	static auto mp_decals = I::CVar->FindVar("mp_decals");
	static auto r_drawmodeldecals = I::CVar->FindVar("r_drawmodeldecals");
	static auto r_rootlod = I::CVar->FindVar("r_rootlod");
	static auto cl_detaildist = I::CVar->FindVar("cl_detaildist");
	static auto cl_detailfade = I::CVar->FindVar("cl_detailfade");
	static auto r_drawdetailprops = I::CVar->FindVar("r_drawdetailprops");
	static auto r_shadows = I::CVar->FindVar("r_shadows");

	static bool bLastGibs = false;
	static bool bLastRagdolls = false;
	static bool bLastDecals = false;
	static bool bLastFoliage = false;
	static bool bLastNoProps = false;
	static int bLastLOD = -1;

	if (Vars::Misc::Optimizations::DisableGibs.Value != bLastGibs)
	{
		if (cl_gib_allow) cl_gib_allow->SetValue(!Vars::Misc::Optimizations::DisableGibs.Value);
		if (cl_burninggibs) cl_burninggibs->SetValue(!Vars::Misc::Optimizations::DisableGibs.Value);
		bLastGibs = Vars::Misc::Optimizations::DisableGibs.Value;
	}

	if (Vars::Misc::Optimizations::DisableRagdolls.Value != bLastRagdolls)
	{
		if (cl_ragdoll_fade_time) cl_ragdoll_fade_time->SetValue(Vars::Misc::Optimizations::DisableRagdolls.Value ? 0.f : 15.f);
		if (cl_ragdoll_forcefade) cl_ragdoll_forcefade->SetValue(Vars::Misc::Optimizations::DisableRagdolls.Value);
		bLastRagdolls = Vars::Misc::Optimizations::DisableRagdolls.Value;
	}

	if (Vars::Misc::Optimizations::DisableDecals.Value != bLastDecals)
	{
		if (r_decals) r_decals->SetValue(Vars::Misc::Optimizations::DisableDecals.Value ? 0 : 2048);
		if (mp_decals) mp_decals->SetValue(Vars::Misc::Optimizations::DisableDecals.Value ? 0 : 200);
		if (r_drawmodeldecals) r_drawmodeldecals->SetValue(!Vars::Misc::Optimizations::DisableDecals.Value);
		bLastDecals = Vars::Misc::Optimizations::DisableDecals.Value;
	}

	if (Vars::Misc::Optimizations::LowDetailModels.Value != bLastLOD)
	{
		if (r_rootlod) r_rootlod->SetValue(Vars::Misc::Optimizations::LowDetailModels.Value ? 2 : 0);
		bLastLOD = Vars::Misc::Optimizations::LowDetailModels.Value;
	}

	if (Vars::Misc::Optimizations::DisableFoliage.Value != bLastFoliage)
	{
		if (cl_detaildist) cl_detaildist->SetValue(Vars::Misc::Optimizations::DisableFoliage.Value ? 0.f : 1200.f);
		if (cl_detailfade) cl_detailfade->SetValue(Vars::Misc::Optimizations::DisableFoliage.Value ? 0.f : 400.f);
		if (r_drawdetailprops) r_drawdetailprops->SetValue(!Vars::Misc::Optimizations::DisableFoliage.Value);
		bLastFoliage = Vars::Misc::Optimizations::DisableFoliage.Value;
	}

	if (Vars::Misc::Optimizations::NoProps.Value != bLastNoProps)
	{
		static auto r_drawstaticprops = I::CVar->FindVar("r_drawstaticprops");
		if (r_drawstaticprops) r_drawstaticprops->SetValue(!Vars::Misc::Optimizations::NoProps.Value);
		bLastNoProps = Vars::Misc::Optimizations::NoProps.Value;
	}
}
