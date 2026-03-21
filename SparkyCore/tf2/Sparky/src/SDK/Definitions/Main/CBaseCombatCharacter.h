#pragma once
#include "CBaseFlex.h"

class CTFWeaponBase;

class CBaseCombatCharacter : public CBaseFlex
{
public:
	NETVAR(m_flNextAttack, float, XS("CBaseCombatCharacter"), XS("m_flNextAttack"));
	NETVAR(m_hActiveWeapon, EHANDLE, XS("CBaseCombatCharacter"), XS("m_hActiveWeapon"));
	//NETVAR(m_hMyWeapons, EHANDLE, "CBaseCombatCharacter", "m_hMyWeapons");
	NETVAR(m_bGlowEnabled, bool, XS("CBaseCombatCharacter"), XS("m_bGlowEnabled"));

	CHandle<CTFWeaponBase>(&m_hMyWeapons())[MAX_WEAPONS];
	CTFWeaponBase* GetWeaponFromSlot(int nSlot);
};