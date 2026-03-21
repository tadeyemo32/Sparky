#pragma once
#include "CBaseCombatCharacter.h"
#include "CEconWearable.h"
#include "CUserCmd.h"

MAKE_SIGNATURE(CBasePlayer_GetAmmoCount, XS("client.dll"), XS("48 89 5C 24 ? 57 48 83 EC ? 48 63 DA 48 8B F9 83 FB"), 0x0);

class CBasePlayer : public CBaseCombatCharacter
{
public:
	NETVAR_EMBED(m_Local, void*, XS("CBasePlayer"), XS("m_Local"));
	NETVAR(m_chAreaBits, void*, XS("CBasePlayer"), XS("m_chAreaBits"));
	NETVAR(m_chAreaPortalBits, void*, XS("CBasePlayer"), XS("m_chAreaPortalBits"));
	NETVAR(m_iHideHUD, int, XS("CBasePlayer"), XS("m_iHideHUD"));
	NETVAR(m_flFOVRate, float, XS("CBasePlayer"), XS("m_flFOVRate"));
	NETVAR(m_bDucked, bool, XS("CBasePlayer"), XS("m_bDucked"));
	NETVAR(m_bDucking, bool, XS("CBasePlayer"), XS("m_bDucking"));
	NETVAR(m_bInDuckJump, bool, XS("CBasePlayer"), XS("m_bInDuckJump"));
	NETVAR(m_flDucktime, float, XS("CBasePlayer"), XS("m_flDucktime"));
	NETVAR(m_flDuckJumpTime, float, XS("CBasePlayer"), XS("m_flDuckJumpTime"));
	NETVAR(m_flJumpTime, float, XS("CBasePlayer"), XS("m_flJumpTime"));
	NETVAR(m_flFallVelocity, float, XS("CBasePlayer"), XS("m_flFallVelocity"));
	NETVAR(m_vecPunchAngle, Vec3, XS("CBasePlayer"), XS("m_vecPunchAngle"));
	NETVAR(m_vecPunchAngleVel, Vec3, XS("CBasePlayer"), XS("m_vecPunchAngleVel"));
	NETVAR(m_bDrawViewmodel, bool, XS("CBasePlayer"), XS("m_bDrawViewmodel"));
	NETVAR(m_bWearingSuit, bool, XS("CBasePlayer"), XS("m_bWearingSuit"));
	NETVAR(m_bPoisoned, bool, XS("CBasePlayer"), XS("m_bPoisoned"));
	NETVAR(m_flStepSize, float, XS("CBasePlayer"), XS("m_flStepSize"));
	NETVAR(m_bAllowAutoMovement, bool, XS("CBasePlayer"), XS("m_bAllowAutoMovement"));
	NETVAR(m_vecViewOffset, Vec3, XS("CBasePlayer"), XS("m_vecViewOffset[0]"));
	NETVAR(m_flFriction, float, XS("CBasePlayer"), XS("m_flFriction"));
	NETVAR(m_iAmmo, void*, XS("CBasePlayer"), XS("m_iAmmo"));
	NETVAR(m_fOnTarget, int, XS("CBasePlayer"), XS("m_fOnTarget"));
	NETVAR(m_nTickBase, int, XS("CBasePlayer"), XS("m_nTickBase"));
	NETVAR(m_nNextThinkTick, int, XS("CBasePlayer"), XS("m_nNextThinkTick"));
	NETVAR(m_hLastWeapon, EHANDLE, XS("CBasePlayer"), XS("m_hLastWeapon"));
	NETVAR(m_hGroundEntity, EHANDLE, XS("CBasePlayer"), XS("m_hGroundEntity"));
	NETVAR(m_vecVelocity, Vec3, XS("CBasePlayer"), XS("m_vecVelocity[0]"));
	NETVAR(m_vecBaseVelocity, Vec3, XS("CBasePlayer"), XS("m_vecBaseVelocity"));
	NETVAR(m_hConstraintEntity, EHANDLE, XS("CBasePlayer"), XS("m_hConstraintEntity"));
	NETVAR(m_vecConstraintCenter, Vec3, XS("CBasePlayer"), XS("m_vecConstraintCenter"));
	NETVAR(m_flConstraintRadius, float, XS("CBasePlayer"), XS("m_flConstraintRadius"));
	NETVAR(m_flConstraintWidth, float, XS("CBasePlayer"), XS("m_flConstraintWidth"));
	NETVAR(m_flConstraintSpeedFactor, float, XS("CBasePlayer"), XS("m_flConstraintSpeedFactor"));
	NETVAR(m_flDeathTime, float, XS("CBasePlayer"), XS("m_flDeathTime"));
	NETVAR(m_nWaterLevel, byte, XS("CBasePlayer"), XS("m_nWaterLevel"));
	NETVAR(m_flLaggedMovementValue, float, XS("CBasePlayer"), XS("m_flLaggedMovementValue"));
	NETVAR_EMBED(m_AttributeList, void*, XS("CBasePlayer"), XS("m_AttributeList"));
	NETVAR_EMBED(pl, void*, XS("CBasePlayer"), XS("pl"));
	NETVAR(deadflag, int, XS("CBasePlayer"), XS("deadflag"));
	NETVAR(m_iFOV, int, XS("CBasePlayer"), XS("m_iFOV"));
	NETVAR(m_iFOVStart, int, XS("CBasePlayer"), XS("m_iFOVStart"));
	NETVAR(m_flFOVTime, float, XS("CBasePlayer"), XS("m_flFOVTime"));
	NETVAR(m_iDefaultFOV, int, XS("CBasePlayer"), XS("m_iDefaultFOV"));
	NETVAR(m_hZoomOwner, EHANDLE, XS("CBasePlayer"), XS("m_hZoomOwner"));
	NETVAR(m_hVehicle, EHANDLE, XS("CBasePlayer"), XS("m_hVehicle"));
	NETVAR(m_hUseEntity, EHANDLE, XS("CBasePlayer"), XS("m_hUseEntity"));
	NETVAR(m_iHealth, int, XS("CBasePlayer"), XS("m_iHealth"));
	NETVAR(m_lifeState, byte, XS("CBasePlayer"), XS("m_lifeState"));
	NETVAR(m_iBonusProgress, int, XS("CBasePlayer"), XS("m_iBonusProgress"));
	NETVAR(m_iBonusChallenge, int, XS("CBasePlayer"), XS("m_iBonusChallenge"));
	NETVAR(m_flMaxspeed, float, XS("CBasePlayer"), XS("m_flMaxspeed"));
	NETVAR(m_fFlags, int, XS("CBasePlayer"), XS("m_fFlags"));
	NETVAR(m_iObserverMode, int, XS("CBasePlayer"), XS("m_iObserverMode"));
	NETVAR(m_hObserverTarget, EHANDLE, XS("CBasePlayer"), XS("m_hObserverTarget"));
	NETVAR(m_hViewModel, EHANDLE, XS("CBasePlayer"), XS("m_hViewModel[0]"));
	NETVAR(m_szLastPlaceName, const char*, XS("CBasePlayer"), XS("m_szLastPlaceName"));

	NETVAR_OFF(m_flPhysics, int, XS("CBasePlayer"), XS("m_nTickBase"), -4);
	NETVAR_OFF(m_nFinalPredictedTick, int, XS("CBasePlayer"), XS("m_nTickBase"), 4);
	NETVAR_OFF(m_nButtons, int, XS("CBasePlayer"), XS("m_hConstraintEntity"), -12);
	NETVAR_OFF(m_pCurrentCommand, CUserCmd*, XS("CBasePlayer"), XS("m_hConstraintEntity"), -8);
	NETVAR_OFF(m_afButtonLast, int, XS("CBasePlayer"), XS("m_hConstraintEntity"), -24);
	NETVAR_OFF(m_flWaterJumpTime, float, XS("CBasePlayer"), XS("m_fOnTarget"), -60);
	NETVAR_OFF(m_flSwimSoundTime, float, XS("CBasePlayer"), XS("m_fOnTarget"), -44);
	NETVAR_OFF(m_vecLadderNormal, Vec3, XS("CBasePlayer"), XS("m_fOnTarget"), -36);
	NETVAR_OFF(m_surfaceProps, int, XS("CBasePlayer"), XS("m_szLastPlaceName"), 20);
	NETVAR_OFF(m_pSurfaceData, void*, XS("CBasePlayer"), XS("m_szLastPlaceName"), 24);
	NETVAR_OFF(m_surfaceFriction, float, XS("CBasePlayer"), XS("m_szLastPlaceName"), 32);
	NETVAR_OFF(m_chTextureType, char, XS("CBasePlayer"), XS("m_szLastPlaceName"), 36);
	NETVAR_OFF(m_hMyWearables, CUtlVector<CHandle<CEconWearable>>, XS("CBasePlayer"), XS("m_szLastPlaceName"), 56);

	CONDGET(IsOnGround, m_fFlags(), FL_ONGROUND);
	CONDGET(IsInWater, m_fFlags(), FL_INWATER);
	CONDGET(IsDucking, m_fFlags(), FL_DUCKING);
	CONDGET(IsFakeClient, m_fFlags(), FL_FAKECLIENT);

	VIRTUAL(PreThink, void, 262, this);
	VIRTUAL(Think, void, 122, this);
	VIRTUAL(PostThink, void, 263, this);
	VIRTUAL(GetRenderedWeaponModel, CBaseAnimating*, 252, this);
	VIRTUAL_ARGS(SelectItem, void, 272, (const char* ptr, int subtype), this, ptr, subtype);

	SIGNATURE_ARGS(GetAmmoCount, int, CBasePlayer, (int iAmmoType), this, iAmmoType);

	bool IsAlive();
	Vec3 GetShootPos();
	Vec3 GetEyePosition();
	bool OnSolid();
	bool IsSwimming();
	bool IsUnderwater();
};