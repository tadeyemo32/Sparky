#pragma once
#include "CBaseEntity.h"
#include "../Misc/IGameResources.h"
#include "../Misc/String.h"

#define PLAYER_UNCONNECTED_NAME	"unconnected"
#define PLAYER_ERROR_NAME "ERRORNAME"

class CPlayerResource : public CBaseEntity, public IGameResources
{
public:
	NETVAR_ARRAY(m_iPing, int, XS("CPlayerResource"), XS("m_iPing"));
	NETVAR_ARRAY(m_iScore, int, XS("CPlayerResource"), XS("m_iScore"));
	NETVAR_ARRAY(m_iDeaths, int, XS("CPlayerResource"), XS("m_iDeaths"));
	NETVAR_ARRAY(m_bConnected, bool, XS("CPlayerResource"), XS("m_bConnected"));
	NETVAR_ARRAY(m_iTeam, int, XS("CPlayerResource"), XS("m_iTeam"));
	NETVAR_ARRAY(m_bAlive, bool, XS("CPlayerResource"), XS("m_bAlive"));
	NETVAR_ARRAY(m_iHealth, int, XS("CPlayerResource"), XS("m_iHealth"));
	NETVAR_ARRAY(m_iAccountID, unsigned, XS("CPlayerResource"), XS("m_iAccountID"));
	NETVAR_ARRAY(m_bValid, bool, XS("CPlayerResource"), XS("m_bValid"));
	NETVAR_ARRAY(m_iUserID, int, XS("CPlayerResource"), XS("m_iUserID"));

	NETVAR_ARRAY_OFF(m_szName, const char*, XS("CPlayerResource"), XS("m_iPing"), -816);
	
	inline bool IsValid(int iIndex)
	{
		if (iIndex < 0 || iIndex > MAX_PLAYERS)
			return false;

		return m_bValid(iIndex);
	}

	inline bool IsFakePlayer(int iIndex, bool bPlayerInfo = false)
	{
		if (!bPlayerInfo)
			return !m_iPing(iIndex);

		player_info_t tInfo;
		if (I::EngineClient->GetPlayerInfo(iIndex, &tInfo))
			return tInfo.fakeplayer;
		return false;
	}

	inline const char* GetName(int iIndex)
	{
		const char* sName = m_szName(iIndex);
		return sName ? sName : PLAYER_ERROR_NAME;
	}
};