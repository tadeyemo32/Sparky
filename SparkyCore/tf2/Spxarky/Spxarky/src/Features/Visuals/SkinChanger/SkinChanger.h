#pragma once
#include "../../../SDK/SDK.h"

class CSkinChanger
{
public:
	void Run(CBaseEntity* pLocal, int nStage);
};

ADD_FEATURE(CSkinChanger, SkinChanger);
