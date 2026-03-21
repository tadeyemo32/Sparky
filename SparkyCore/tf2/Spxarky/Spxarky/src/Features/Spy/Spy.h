#pragma once
#include "../../SDK/SDK.h"

class CSpy
{
public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

namespace F { inline CSpy Spy; }
