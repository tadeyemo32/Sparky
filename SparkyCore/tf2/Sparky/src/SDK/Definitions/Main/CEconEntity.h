#pragma once
#include "CBaseAnimating.h"

class CEconEntity : public CBaseAnimating
{
public:
	NETVAR(m_iItemDefinitionIndex, int, XS("CEconEntity"), XS("m_iItemDefinitionIndex"));

	VIRTUAL(UpdateAttachmentModels, void, 213, this);
};