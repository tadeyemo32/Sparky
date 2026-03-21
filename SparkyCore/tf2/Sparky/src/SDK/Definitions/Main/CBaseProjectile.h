#pragma once
#include "CBaseAnimating.h"

enum
{
	TF_GL_MODE_REGULAR = 0,
	TF_GL_MODE_REMOTE_DETONATE,
	TF_GL_MODE_REMOTE_DETONATE_PRACTICE,
	TF_GL_MODE_CANNONBALL
};

class CBaseProjectile : public CBaseAnimating
{
public:
	NETVAR(m_hOriginalLauncher, EHANDLE, XS("CBaseProjectile"), XS("m_hOriginalLauncher"));
};

class CBaseGrenade : public CBaseProjectile
{
public:
	NETVAR(m_flDamage, float, XS("CBaseGrenade"), XS("m_flDamage"));
	NETVAR(m_DmgRadius, float, XS("CBaseGrenade"), XS("m_DmgRadius"));
	NETVAR(m_bIsLive, bool, XS("CBaseGrenade"), XS("m_bIsLive"));
	NETVAR(m_hThrower, EHANDLE, XS("CBaseGrenade"), XS("m_hThrower"));
	NETVAR(m_vecVelocity, Vec3, XS("CBaseGrenade"), XS("m_vecVelocity"));
	NETVAR(m_fFlags, int, XS("CBaseGrenade"), XS("m_fFlags"));
};

class CTFBaseRocket : public CBaseProjectile
{
public:
	NETVAR(m_vInitialVelocity, Vec3, XS("CTFBaseRocket"), XS("m_vInitialVelocity"));
	NETVAR(m_vecOrigin, Vec3, XS("CTFBaseRocket"), XS("m_vecOrigin"));
	NETVAR(m_angRotation, Vec3, XS("CTFBaseRocket"), XS("m_angRotation"));
	NETVAR(m_iDeflected, int, XS("CTFBaseRocket"), XS("m_iDeflected"));
	NETVAR(m_hLauncher, EHANDLE, XS("CTFBaseRocket"), XS("m_hLauncher"));
};

class CTFBaseProjectile : public CBaseProjectile
{
public:
	NETVAR(m_hLauncher, EHANDLE, XS("CTFBaseProjectile"), XS("m_hLauncher"));
};

class CTFWeaponBaseGrenadeProj : public CBaseGrenade
{
public:
	NETVAR(m_vInitialVelocity, Vec3, XS("CTFWeaponBaseGrenadeProj"), XS("m_vInitialVelocity"));
	NETVAR(m_bCritical, bool, XS("CTFWeaponBaseGrenadeProj"), XS("m_bCritical"));
	NETVAR(m_iDeflected, int, XS("CTFWeaponBaseGrenadeProj"), XS("m_iDeflected"));
	NETVAR(m_vecOrigin, Vec3, XS("CTFWeaponBaseGrenadeProj"), XS("m_vecOrigin"));
	NETVAR(m_angRotation, Vec3, XS("CTFWeaponBaseGrenadeProj"), XS("m_angRotation"));
	NETVAR(m_hDeflectOwner, EHANDLE, XS("CTFWeaponBaseGrenadeProj"), XS("m_hDeflectOwner"));
};

class CTFProjectile_Rocket : public CTFBaseRocket
{
public:
	NETVAR(m_bCritical, bool, XS("CTFProjectile_Rocket"), XS("m_bCritical"));
};

class CTFProjectile_EnergyBall : public CTFBaseRocket
{
public:
	NETVAR(m_bChargedShot, bool, XS("CTFProjectile_EnergyBall"), XS("m_bChargedShot"));
	NETVAR(m_vColor1, Vec3, XS("CTFProjectile_EnergyBall"), XS("m_bChargedShot"));
	NETVAR(m_vColor2, Vec3, XS("CTFProjectile_EnergyBall"), XS("m_bChargedShot"));
};

class CTFProjectile_Flare : public CTFBaseRocket
{
public:
	NETVAR(m_bCritical, bool, XS("CTFProjectile_Flare"), XS("m_bCritical"));
};

class CTFProjectile_Arrow : public CTFBaseRocket
{
public:
	NETVAR(m_bArrowAlight, bool, XS("CTFProjectile_Arrow"), XS("m_bArrowAlight"));
	NETVAR(m_bCritical, bool, XS("CTFProjectile_Arrow"), XS("m_bCritical"));
	NETVAR(m_iProjectileType, int, XS("CTFProjectile_Arrow"), XS("m_iProjectileType"));

	bool CanHeadshot();
};

class CTFGrenadePipebombProjectile : public CTFWeaponBaseGrenadeProj
{
public:
	NETVAR(m_bTouched, bool, XS("CTFGrenadePipebombProjectile"), XS("m_bTouched"));
	NETVAR(m_iType, int, XS("CTFGrenadePipebombProjectile"), XS("m_iType"));
	NETVAR(m_hLauncher, EHANDLE, XS("CTFGrenadePipebombProjectile"), XS("m_hLauncher"));
	NETVAR(m_bDefensiveBomb, int, XS("CTFGrenadePipebombProjectile"), XS("m_bDefensiveBomb"));

	NETVAR_OFF(m_flCreationTime, float, XS("CTFGrenadePipebombProjectile"), XS("m_iType"), 4);
	NETVAR_OFF(m_bPulsed, bool, XS("CTFGrenadePipebombProjectile"), XS("m_iType"), 12);

	bool HasStickyEffects();
};