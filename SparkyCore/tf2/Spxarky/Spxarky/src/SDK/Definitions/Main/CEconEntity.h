#pragma once
#include "CBaseAnimating.h"

class CEconEntity : public CBaseAnimating
{
public:
	NETVAR(m_iItemDefinitionIndex, int, "CEconEntity", "m_iItemDefinitionIndex");
	NETVAR(m_iEntityQuality, int, "CEconEntity", "m_iEntityQuality");
	NETVAR(m_iItemIDHigh, int, "CEconEntity", "m_iItemIDHigh");
	NETVAR(m_iItemIDLow, int, "CEconEntity", "m_iItemIDLow");
	NETVAR(m_bInitialized, bool, "CEconEntity", "m_bInitialized");

	VIRTUAL(UpdateAttachmentModels, void, 213, this);
};