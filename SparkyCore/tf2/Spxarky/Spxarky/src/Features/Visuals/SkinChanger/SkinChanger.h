#pragma once
#include "../../../SDK/SDK.h"

class CSkinChanger
{
public:
	void Run(CTFPlayer* pLocal, int nStage);
};

ADD_FEATURE(CSkinChanger, SkinChanger);
