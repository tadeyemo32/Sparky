#pragma once
#include "CBaseCombatCharacter.h"

class CBaseObject : public CBaseCombatCharacter
{
public:
	NETVAR(m_iHealth, int, XS("CBaseObject"), XS("m_iHealth"));
	NETVAR(m_iMaxHealth, int, XS("CBaseObject"), XS("m_iMaxHealth"));
	NETVAR(m_bHasSapper, bool, XS("CBaseObject"), XS("m_bHasSapper"));
	NETVAR(m_iObjectType, int, XS("CBaseObject"), XS("m_iObjectType"));
	NETVAR(m_bBuilding, bool, XS("CBaseObject"), XS("m_bBuilding"));
	NETVAR(m_bPlacing, bool, XS("CBaseObject"), XS("m_bPlacing"));
	NETVAR(m_bCarried, bool, XS("CBaseObject"), XS("m_bCarried"));
	NETVAR(m_bCarryDeploy, bool, XS("CBaseObject"), XS("m_bCarryDeploy"));
	NETVAR(m_bMiniBuilding, bool, XS("CBaseObject"), XS("m_bMiniBuilding"));
	NETVAR(m_flPercentageConstructed, float, XS("CBaseObject"), XS("m_flPercentageConstructed"));
	NETVAR(m_fObjectFlags, int, XS("CBaseObject"), XS("m_fObjectFlags"));
	NETVAR(m_hBuiltOnEntity, EHANDLE, XS("CBaseObject"), XS("m_hBuiltOnEntity"));
	NETVAR(m_bDisabled, bool, XS("CBaseObject"), XS("m_bDisabled"));
	NETVAR(m_hBuilder, EHANDLE, XS("CBaseObject"), XS("m_hBuilder"));
	NETVAR(m_vecBuildMaxs, Vec3, XS("CBaseObject"), XS("m_vecBuildMaxs"));
	NETVAR(m_vecBuildMins, Vec3, XS("CBaseObject"), XS("m_vecBuildMins"));
	NETVAR(m_iDesiredBuildRotations, int, XS("CBaseObject"), XS("m_iDesiredBuildRotations"));
	NETVAR(m_bServerOverridePlacement, bool, XS("CBaseObject"), XS("m_bServerOverridePlacement"));
	NETVAR(m_iUpgradeLevel, int, XS("CBaseObject"), XS("m_iUpgradeLevel"));
	NETVAR(m_iUpgradeMetal, int, XS("CBaseObject"), XS("m_iUpgradeMetal"));
	NETVAR(m_iUpgradeMetalRequired, int, XS("CBaseObject"), XS("m_iUpgradeMetalRequired"));
	NETVAR(m_iHighestUpgradeLevel, int, XS("CBaseObject"), XS("m_iHighestUpgradeLevel"));
	NETVAR(m_iObjectMode, int, XS("CBaseObject"), XS("m_iObjectMode"));
	NETVAR(m_bDisposableBuilding, bool, XS("CBaseObject"), XS("m_bDisposableBuilding"));
	NETVAR(m_bWasMapPlaced, bool, XS("CBaseObject"), XS("m_bWasMapPlaced"));
	NETVAR(m_bPlasmaDisable, bool, XS("CBaseObject"), XS("m_bPlasmaDisable"));

	void* IHasBuildPoints()
	{
		return reinterpret_cast<void*>(uintptr_t(this) + 4584);
	}
	VIRTUAL(GetNumBuildPoints, int, 0, IHasBuildPoints());
	VIRTUAL_ARGS(GetBuildPoint, bool, 1, (int iPoint, Vector& vecOrigin, QAngle& vecAngles), IHasBuildPoints(), iPoint, std::ref(vecOrigin), std::ref(vecAngles));
	VIRTUAL_ARGS(GetBuildPointAttachmentIndex, int, 2, (int iPoint), IHasBuildPoints(), iPoint);
	VIRTUAL_ARGS(CanBuildObjectOnBuildPoint, bool, 3, (int iPoint, int iObjectType), IHasBuildPoints(), iPoint, iObjectType);
	VIRTUAL_ARGS(SetObjectOnBuildPoint, void, 4, (int iPoint, CBaseObject* pObject), IHasBuildPoints(), iPoint, pObject);
	VIRTUAL(GetNumObjectsOnMe, int, 5, IHasBuildPoints());
	VIRTUAL_ARGS(GetObjectOfTypeOnMe, CBaseObject*, 6, (int iObjectType), IHasBuildPoints(), iObjectType);
	VIRTUAL_ARGS(GetMaxSnapDistance, float, 8, (/*int iPoint*/), IHasBuildPoints()/*, iPoint*/);
	VIRTUAL(ShouldCheckForMovement, bool, 9, IHasBuildPoints());
	VIRTUAL_ARGS(FindObjectOnBuildPoint, int, 10, (CBaseObject* pObject), IHasBuildPoints(), pObject);

	bool IsDisabled();
};

class CObjectSentrygun : public CBaseObject
{
public:
	NETVAR(m_iAmmoShells, int, XS("CObjectSentrygun"), XS("m_iAmmoShells"));
	NETVAR(m_iAmmoRockets, int, XS("CObjectSentrygun"), XS("m_iAmmoRockets"));
	NETVAR(m_iState, int, XS("CObjectSentrygun"), XS("m_iState"));
	NETVAR(m_bPlayerControlled, bool, XS("CObjectSentrygun"), XS("m_bPlayerControlled"));
	NETVAR(m_nShieldLevel, int, XS("CObjectSentrygun"), XS("m_nShieldLevel"));
	NETVAR(m_bShielded, bool, XS("CObjectSentrygun"), XS("m_bShielded"));
	NETVAR(m_hEnemy, EHANDLE, XS("CObjectSentrygun"), XS("m_hEnemy"));
	NETVAR(m_hAutoAimTarget, EHANDLE, XS("CObjectSentrygun"), XS("m_hAutoAimTarget"));
	NETVAR(m_iKills, int, XS("CObjectSentrygun"), XS("m_iKills"));
	NETVAR(m_iAssists, int, XS("CObjectSentrygun"), XS("m_iAssists"));

	int MaxAmmoShells();
	void GetAmmoCount(int& iShells, int& iMaxShells, int& iRockets, int& iMaxRockets);
};

class CObjectDispenser : public CBaseObject
{
public:
	NETVAR(m_iState, int, XS("CObjectDispenser"), XS("m_iState"));
	NETVAR(m_iAmmoMetal, int, XS("CObjectDispenser"), XS("m_iAmmoMetal"));
	NETVAR(m_iMiniBombCounter, int, XS("CObjectDispenser"), XS("m_iMiniBombCounter"));
};

class CObjectTeleporter : public CBaseObject
{
public:
	NETVAR(m_iState, int, XS("CObjectTeleporter"), XS("m_iState"));
	NETVAR(m_flRechargeTime, float, XS("CObjectTeleporter"), XS("m_flRechargeTime"));
	NETVAR(m_flCurrentRechargeDuration, float, XS("CObjectTeleporter"), XS("m_flCurrentRechargeDuration"));
	NETVAR(m_iTimesUsed, int, XS("CObjectTeleporter"), XS("m_iTimesUsed"));
	NETVAR(m_flYawToExit, float, XS("CObjectTeleporter"), XS("m_flYawToExit"));
	NETVAR(m_bMatchBuilding, bool, XS("CObjectTeleporter"), XS("m_bMatchBuilding"));
};