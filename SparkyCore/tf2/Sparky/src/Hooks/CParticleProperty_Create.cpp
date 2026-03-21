#include "../SDK/SDK.h"

#include "../Features/Simulation/ProjectileSimulation/ProjectileSimulation.h"

MAKE_SIGNATURE(CParticleProperty_Create_Name, XS("client.dll"), XS("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B 59 ? 49 8B F1"), 0x0);
MAKE_SIGNATURE(CParticleProperty_Create_Point, XS("client.dll"), XS("44 89 4C 24 ? 44 89 44 24 ? 53"), 0x0);
MAKE_SIGNATURE(CParticleProperty_AddControlPoint_Pointer, XS("client.dll"), XS("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 55 41 56 41 57 48 83 EC ? 4C 8B BC 24"), 0x0);
MAKE_SIGNATURE(CWeaponMedigun_UpdateEffects_CreateName_Call1, XS("client.dll"), XS("49 8B CC F3 0F 11 74 24 ? 48 8B D8"), 0x0);
MAKE_SIGNATURE(CWeaponMedigun_UpdateEffects_CreateName_Call2, XS("client.dll"), XS("41 8B 14 24 48 8B D8"), 0x0);
MAKE_SIGNATURE(CWeaponMedigun_ManageChargeEffect_CreateName_Call, XS("client.dll"), XS("48 89 86 ? ? ? ? 48 89 BE ? ? ? ? 48 83 BE"), 0x0);

MAKE_HOOK(CParticleProperty_Create_Name, S::CParticleProperty_Create_Name(), void*,
	void* rcx, const char* pszParticleName, ParticleAttachment_t iAttachType, const char* pszAttachmentName)
{
    DEBUG_RETURN(CParticleProperty_Create_Name, rcx, pszParticleName, iAttachType, pszAttachmentName);

    const auto dwRetAddr = uintptr_t(_ReturnAddress());
    const auto dwUpdateEffects1 = S::CWeaponMedigun_UpdateEffects_CreateName_Call1();
    const auto dwUpdateEffects2 = S::CWeaponMedigun_UpdateEffects_CreateName_Call2();
    const auto dwManageChargeEffect = S::CWeaponMedigun_ManageChargeEffect_CreateName_Call();

    bool bUpdateEffects = dwRetAddr == dwUpdateEffects1 || dwRetAddr == dwUpdateEffects2, bManageChargeEffect = dwRetAddr == dwManageChargeEffect;
    if (bUpdateEffects || bManageChargeEffect)
    {
        auto pLocal = H::Entities.GetLocal();
        if (!pLocal)
            return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);

        /* // probably not needed
        auto pWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
        if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MEDIGUN)
            return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);
        */

        auto pModel = pLocal->GetRenderedWeaponModel();
        if (!pModel || rcx != pModel->m_Particles())
            return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);

        bool bBlue = pLocal->m_iTeamNum() == TF_TEAM_BLUE;
        if (bUpdateEffects)
        {
            switch (FNV1A::Hash32(Vars::Visuals::Effects::MedigunBeam.Value.c_str()))
            {
            case FNV1A::Hash32Const(XS("Default")): break;
            case FNV1A::Hash32Const(XS("None")): return nullptr;
            case FNV1A::Hash32Const(XS("Uber")): pszParticleName = bBlue ? XS("medicgun_beam_blue_invun") : XS("medicgun_beam_red_invun"); break;
            case FNV1A::Hash32Const(XS("Dispenser")): pszParticleName = bBlue ? XS("dispenser_heal_blue") : XS("dispenser_heal_red"); break;
            case FNV1A::Hash32Const(XS("Passtime")): pszParticleName = XS("passtime_beam"); break;
            case FNV1A::Hash32Const(XS("Bombonomicon")): pszParticleName = XS("bombonomicon_spell_trail"); break;
            case FNV1A::Hash32Const(XS("White")): pszParticleName = XS("medicgun_beam_machinery_stage3"); break;
            case FNV1A::Hash32Const(XS("Orange")): pszParticleName = XS("medicgun_beam_red_trail_stage3"); break;
            default: pszParticleName = Vars::Visuals::Effects::MedigunBeam.Value.c_str();
            }
        }
        else if (bManageChargeEffect)
        {
            switch (FNV1A::Hash32(Vars::Visuals::Effects::MedigunCharge.Value.c_str()))
            {
            case FNV1A::Hash32Const(XS("Default")): break;
            case FNV1A::Hash32Const(XS("None")): return nullptr;
            case FNV1A::Hash32Const(XS("Electrocuted")): pszParticleName = bBlue ? XS("electrocuted_blue") : XS("electrocuted_red"); break;
            case FNV1A::Hash32Const(XS("Halloween")): pszParticleName = XS("ghost_pumpkin"); break;
            case FNV1A::Hash32Const(XS("Fireball")): pszParticleName = bBlue ? XS("spell_fireball_small_trail_blue") : XS("spell_fireball_small_trail_red"); break;
            case FNV1A::Hash32Const(XS("Teleport")): pszParticleName = bBlue ? XS("spell_teleport_blue") : XS("spell_teleport_red"); break;
            case FNV1A::Hash32Const(XS("Burning")): pszParticleName = XS("superrare_burning1"); break;
            case FNV1A::Hash32Const(XS("Scorching")): pszParticleName = XS("superrare_burning2"); break;
            case FNV1A::Hash32Const(XS("Purple energy")): pszParticleName = XS("superrare_purpleenergy"); break;
            case FNV1A::Hash32Const(XS("Green energy")): pszParticleName = XS("superrare_greenenergy"); break;
            case FNV1A::Hash32Const(XS("Nebula")): pszParticleName = XS("unusual_invasion_nebula"); break;
            case FNV1A::Hash32Const(XS("Purple stars")): pszParticleName = XS("unusual_star_purple_parent"); break;
            case FNV1A::Hash32Const(XS("Green stars")): pszParticleName = XS("unusual_star_green_parent"); break;
            case FNV1A::Hash32Const(XS("Sunbeams")): pszParticleName = XS("superrare_beams1"); break;
            case FNV1A::Hash32Const(XS("Spellbound")): pszParticleName = XS("unusual_spellbook_circle_purple"); break;
            case FNV1A::Hash32Const(XS("Purple sparks")): pszParticleName = XS("unusual_robot_orbiting_sparks2"); break;
            case FNV1A::Hash32Const(XS("Yellow sparks")): pszParticleName = XS("unusual_robot_orbiting_sparks"); break;
            case FNV1A::Hash32Const(XS("Green zap")): pszParticleName = XS("unusual_zap_green"); break;
            case FNV1A::Hash32Const(XS("Yellow zap")): pszParticleName = XS("unusual_zap_yellow"); break;
            case FNV1A::Hash32Const(XS("Plasma")): pszParticleName = XS("superrare_plasma1"); break;
            case FNV1A::Hash32Const(XS("Frostbite")): pszParticleName = XS("unusual_eotl_frostbite"); break;
            case FNV1A::Hash32Const(XS("Time warp")): pszParticleName = bBlue ? XS("unusual_robot_time_warp2") : XS("unusual_robot_time_warp"); break;
            case FNV1A::Hash32Const(XS("Purple souls")): pszParticleName = XS("unusual_souls_purple_parent"); break;
            case FNV1A::Hash32Const(XS("Green souls")): pszParticleName = XS("unusual_souls_green_parent"); break;
            case FNV1A::Hash32Const(XS("Bubbles")): pszParticleName = XS("unusual_bubbles"); break;
            case FNV1A::Hash32Const(XS("Hearts")): pszParticleName = XS("unusual_hearts_bubbling"); break;
            default: pszParticleName = Vars::Visuals::Effects::MedigunCharge.Value.c_str();
            }
        }
    }

	return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, pszAttachmentName);
}

MAKE_HOOK(CParticleProperty_Create_Point, S::CParticleProperty_Create_Point(), void*,
	void* rcx, const char* pszParticleName, ParticleAttachment_t iAttachType, int iAttachmentPoint, Vector vecOriginOffset)
{
    DEBUG_RETURN(CParticleProperty_Create_Point, rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);

    if (pszParticleName)
    {
        switch (FNV1A::Hash32(pszParticleName))
        {
        case FNV1A::Hash32Const(XS("kart_impact_sparks")):
            if (I::Prediction->InPrediction() && !I::Prediction->m_bFirstTimePredicted)
                return nullptr;
        }
    }

    if (FNV1A::Hash32(Vars::Visuals::Effects::ProjectileTrail.Value.c_str()) != FNV1A::Hash32Const(XS("Default")) && pszParticleName)
    {
        switch (FNV1A::Hash32(pszParticleName))
        {
        // any trails we want to replace
        case FNV1A::Hash32Const(XS("peejar_trail_blu")):
        case FNV1A::Hash32Const(XS("peejar_trail_red")):
        case FNV1A::Hash32Const(XS("peejar_trail_blu_glow")):
        case FNV1A::Hash32Const(XS("peejar_trail_red_glow")):
        case FNV1A::Hash32Const(XS("stunballtrail_blue")):
        case FNV1A::Hash32Const(XS("stunballtrail_red")):
        case FNV1A::Hash32Const(XS("rockettrail")):
        case FNV1A::Hash32Const(XS("rockettrail_airstrike")):
        case FNV1A::Hash32Const(XS("drg_cow_rockettrail_normal_blue")):
        case FNV1A::Hash32Const(XS("drg_cow_rockettrail_normal")):
        case FNV1A::Hash32Const(XS("drg_cow_rockettrail_charged_blue")):
        case FNV1A::Hash32Const(XS("drg_cow_rockettrail_charged")):
        case FNV1A::Hash32Const(XS("rockettrail_RocketJumper")):
        case FNV1A::Hash32Const(XS("rockettrail_underwater")):
        case FNV1A::Hash32Const(XS("halloween_rockettrail")):
        case FNV1A::Hash32Const(XS("eyeboss_projectile")):
        case FNV1A::Hash32Const(XS("drg_bison_projectile")):
        case FNV1A::Hash32Const(XS("flaregun_trail_blue")):
        case FNV1A::Hash32Const(XS("flaregun_trail_red")):
        case FNV1A::Hash32Const(XS("scorchshot_trail_blue")):
        case FNV1A::Hash32Const(XS("scorchshot_trail_red")):
        case FNV1A::Hash32Const(XS("drg_manmelter_projectile")):
        case FNV1A::Hash32Const(XS("pipebombtrail_blue")):
        case FNV1A::Hash32Const(XS("pipebombtrail_red")):
        case FNV1A::Hash32Const(XS("stickybombtrail_blue")):
        case FNV1A::Hash32Const(XS("stickybombtrail_red")):
        case FNV1A::Hash32Const(XS("healshot_trail_blue")):
        case FNV1A::Hash32Const(XS("healshot_trail_red")):
        case FNV1A::Hash32Const(XS("flaming_arrow")):
        case FNV1A::Hash32Const(XS("spell_fireball_small_trail_blue")):
        case FNV1A::Hash32Const(XS("spell_fireball_small_trail_red")):
        {
            auto pLocal = H::Entities.GetLocal();
            if (!pLocal)
                return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);

            bool bValid = false;
            for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
            {
                auto pOwner = F::ProjSim.GetEntities(pEntity).second;
                if (bValid = pLocal == pOwner && rcx == pEntity->m_Particles())
                    break;
            }
            if (!bValid)
                return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);

            bool bBlue = pLocal->m_iTeamNum() == TF_TEAM_BLUE;
            switch (FNV1A::Hash32(Vars::Visuals::Effects::ProjectileTrail.Value.c_str()))
            {
            case FNV1A::Hash32Const(XS("None")): return nullptr;
            case FNV1A::Hash32Const(XS("Rocket")): pszParticleName = XS("rockettrail"); break;
            case FNV1A::Hash32Const(XS("Critical")): pszParticleName = bBlue ? XS("critical_rocket_blue") : XS("critical_rocket_red"); break;
            case FNV1A::Hash32Const(XS("Energy")): pszParticleName = bBlue ? XS("drg_cow_rockettrail_normal_blue") : XS("drg_cow_rockettrail_normal"); break;
            case FNV1A::Hash32Const(XS("Charged")): pszParticleName = bBlue ? XS("drg_cow_rockettrail_charged_blue") : XS("drg_cow_rockettrail_charged"); break;
            case FNV1A::Hash32Const(XS("Ray")): pszParticleName = XS("drg_manmelter_projectile"); break;
            case FNV1A::Hash32Const(XS("Fireball")): pszParticleName = bBlue ? XS("spell_fireball_small_trail_blue") : XS("spell_fireball_small_trail_red"); break;
            case FNV1A::Hash32Const(XS("Teleport")): pszParticleName = bBlue ? XS("spell_teleport_blue") : XS("spell_teleport_red"); break;
            case FNV1A::Hash32Const(XS("Fire")): pszParticleName = XS("flamethrower"); break;
            case FNV1A::Hash32Const(XS("Flame")): pszParticleName = XS("flying_flaming_arrow"); break;
            case FNV1A::Hash32Const(XS("Sparks")): pszParticleName = bBlue ? XS("critical_rocket_bluesparks") : XS("critical_rocket_redsparks"); break;
            case FNV1A::Hash32Const(XS("Flare")): pszParticleName = bBlue ? XS("flaregun_trail_blue") : XS("flaregun_trail_red"); break;
            case FNV1A::Hash32Const(XS("Trail")): pszParticleName = bBlue ? XS("stickybombtrail_blue") : XS("stickybombtrail_red"); break;
            case FNV1A::Hash32Const(XS("Health")): pszParticleName = bBlue ? XS("healshot_trail_blue") : XS("healshot_trail_red"); break;
            case FNV1A::Hash32Const(XS("Smoke")): pszParticleName = XS("rockettrail_airstrike_line"); break;
            case FNV1A::Hash32Const(XS("Bubbles")): pszParticleName = bBlue ? XS("pyrovision_scorchshot_trail_blue") : XS("pyrovision_scorchshot_trail_red"); break;
            case FNV1A::Hash32Const(XS("Halloween")): pszParticleName = XS("halloween_rockettrail"); break;
            case FNV1A::Hash32Const(XS("Monoculus")): pszParticleName = XS("eyeboss_projectile"); break;
            case FNV1A::Hash32Const(XS("Sparkles")): pszParticleName = bBlue ? XS("burningplayer_rainbow_blue") : XS("burningplayer_rainbow_red"); break;
            case FNV1A::Hash32Const(XS("Rainbow")): pszParticleName = XS("flamethrower_rainbow"); break;
            default: pszParticleName = Vars::Visuals::Effects::ProjectileTrail.Value.c_str();
            }
            break;
        }
        /*
        // any additional trails
        case FNV1A::Hash32Const("stunballtrail_blue_crit"):
        case FNV1A::Hash32Const("stunballtrail_red_crit"):
        case FNV1A::Hash32Const("critical_rocket_blue"):
        case FNV1A::Hash32Const("critical_rocket_red"):
        case FNV1A::Hash32Const("critical_rocket_bluesparks"):
        case FNV1A::Hash32Const("critical_rocket_redsparks"):
        case FNV1A::Hash32Const("flaregun_trail_crit_blue"):
        case FNV1A::Hash32Const("flaregun_trail_crit_red"):
        case FNV1A::Hash32Const("critical_pipe_blue"):
        case FNV1A::Hash32Const("critical_pipe_red"):
        case FNV1A::Hash32Const("critical_grenade_blue"):
        case FNV1A::Hash32Const("critical_grenade_red"):
        */
        case FNV1A::Hash32Const(XS("rockettrail_airstrike_line")): return nullptr;
        }
    }

	return CALL_ORIGINAL(rcx, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);
}

MAKE_HOOK(CParticleProperty_AddControlPoint_Pointer, S::CParticleProperty_AddControlPoint_Pointer(), void,
    void* rcx, void* pEffect, int iPoint, CBaseEntity* pEntity, ParticleAttachment_t iAttachType, const char* pszAttachmentName, Vector vecOriginOffset)
{
    DEBUG_RETURN(CParticleProperty_AddControlPoint_Pointer, rcx, pEffect, iPoint, pEntity, iAttachType, pszAttachmentName, vecOriginOffset);

    if (!pEffect)
        return; // crash fix

    CALL_ORIGINAL(rcx, pEffect, iPoint, pEntity, iAttachType, pszAttachmentName, vecOriginOffset);
}