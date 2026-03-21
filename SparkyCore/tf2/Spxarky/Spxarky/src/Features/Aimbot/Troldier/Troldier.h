#pragma once
#include "../../../SDK/SDK.h"

class CTroldier
{
public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CTroldier, Troldier);
