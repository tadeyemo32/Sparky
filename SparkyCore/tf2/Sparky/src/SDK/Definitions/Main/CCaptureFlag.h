#pragma once
#include "CBaseEntity.h"

class CCaptureFlag : public CBaseEntity
{
public:
	NETVAR(m_bDisabled, bool, XS("CCaptureFlag"), XS("m_bDisabled"));
	NETVAR(m_bVisibleWhenDisabled, bool, XS("CCaptureFlag"), XS("m_bVisibleWhenDisabled"));
	NETVAR(m_nType, int, XS("CCaptureFlag"), XS("m_nType"));
	NETVAR(m_nFlagStatus, int, XS("CCaptureFlag"), XS("m_nFlagStatus"));
	NETVAR(m_flResetTime, float, XS("CCaptureFlag"), XS("m_flResetTime"));
	NETVAR(m_flNeutralTime, float, XS("CCaptureFlag"), XS("m_flNeutralTime"));
	NETVAR(m_flMaxResetTime, float, XS("CCaptureFlag"), XS("m_flMaxResetTime"));
	NETVAR(m_hPrevOwner, EHANDLE, XS("CCaptureFlag"), XS("m_hPrevOwner"));
	NETVAR(m_szModel, const char*, XS("CCaptureFlag"), XS("m_szModel"));
	NETVAR(m_szHudIcon, const char*, XS("CCaptureFlag"), XS("m_szHudIcon"));
	NETVAR(m_szPaperEffect, const char*, XS("CCaptureFlag"), XS("m_szPaperEffect"));
	NETVAR(m_szTrailEffect, const char*, XS("CCaptureFlag"), XS("m_szTrailEffect"));
	NETVAR(m_nUseTrailEffect, int, XS("CCaptureFlag"), XS("m_nUseTrailEffect"));
	NETVAR(m_nPointValue, int, XS("CCaptureFlag"), XS("m_nPointValue"));
	NETVAR(m_flAutoCapTime, float, XS("CCaptureFlag"), XS("m_flAutoCapTime"));
	NETVAR(m_bGlowEnabled, bool, XS("CCaptureFlag"), XS("m_bGlowEnabled"));
	NETVAR(m_flTimeToSetPoisonous, float, XS("CCaptureFlag"), XS("m_flTimeToSetPoisonous"));
};