#pragma once
#include "CEconEntity.h"

class CBaseCombatWeapon : public CEconEntity
{
public:
	NETVAR(m_iClip1, int, XS("CBaseCombatWeapon"), XS("m_iClip1"));
	NETVAR(m_iClip2, int, XS("CBaseCombatWeapon"), XS("m_iClip2"));
	NETVAR(m_iPrimaryAmmoType, int, XS("CBaseCombatWeapon"), XS("m_iPrimaryAmmoType"));
	NETVAR(m_iSecondaryAmmoType, int, XS("CBaseCombatWeapon"), XS("m_iSecondaryAmmoType"));
	NETVAR(m_nViewModelIndex, int, XS("CBaseCombatWeapon"), XS("m_nViewModelIndex"));
	NETVAR(m_bFlipViewModel, bool, XS("CBaseCombatWeapon"), XS("m_bFlipViewModel"));
	NETVAR(m_flNextPrimaryAttack, float, XS("CBaseCombatWeapon"), XS("m_flNextPrimaryAttack"));
	NETVAR(m_flNextSecondaryAttack, float, XS("CBaseCombatWeapon"), XS("m_flNextSecondaryAttack"));
	NETVAR(m_nNextThinkTick, int, XS("CBaseCombatWeapon"), XS("m_nNextThinkTick"));
	NETVAR(m_flTimeWeaponIdle, float, XS("CBaseCombatWeapon"), XS("m_flTimeWeaponIdle"));
	NETVAR(m_iViewModelIndex, int, XS("CBaseCombatWeapon"), XS("m_iViewModelIndex"));
	NETVAR(m_iWorldModelIndex, int, XS("CBaseCombatWeapon"), XS("m_iWorldModelIndex"));
	NETVAR(m_iState, int, XS("CBaseCombatWeapon"), XS("m_iState"));
	NETVAR(m_hOwner, EHANDLE, XS("CBaseCombatWeapon"), XS("m_hOwner"));

	NETVAR_OFF(m_bInReload, bool, XS("CBaseCombatWeapon"), XS("m_flTimeWeaponIdle"), 4);
	NETVAR_OFF(m_bRemoveable, bool, XS("CBaseCombatWeapon"), XS("m_iState"), -12);
	NETVAR_OFF(m_bReloadsSingly, bool, XS("CBaseCombatWeapon"), XS("m_iClip2"), 24);

	VIRTUAL(CanBeSelected, bool, 233, this);
	VIRTUAL(CheckReload, void, 278, this);
	VIRTUAL(GetMaxClip1, int, 322, this);
	VIRTUAL(GetMaxClip2, int, 323, this);
	VIRTUAL(GetName, const char*, 334, this);
};