#pragma once
#include "CPlayerResource.h"

enum MM_PlayerConnectionState_t
{
	MM_DISCONNECTED = 0,
	MM_CONNECTED,
	MM_CONNECTING,
	MM_LOADING,
	MM_WAITING_FOR_PLAYER
};

enum ETFStreak
{
	kTFStreak_Kills = 0,
	kTFStreak_KillsAll = 1,
	kTFStreak_Ducks = 2,
	kTFStreak_Duck_levelup = 3,
	kTFStreak_COUNT = 4
};

class CTFPlayerResource : public CPlayerResource
{
public:
	NETVAR_ARRAY(m_iTotalScore, int, XS("CTFPlayerResource"), XS("m_iTotalScore"));
	NETVAR_ARRAY(m_iMaxHealth, int, XS("CTFPlayerResource"), XS("m_iMaxHealth"));
	NETVAR_ARRAY(m_iMaxBuffedHealth, int, XS("CTFPlayerResource"), XS("m_iMaxBuffedHealth"));
	NETVAR_ARRAY(m_iPlayerClass, int, XS("CTFPlayerResource"), XS("m_iPlayerClass"));
	NETVAR_ARRAY(m_bArenaSpectator, bool, XS("CTFPlayerResource"), XS("m_bArenaSpectator"));
	NETVAR_ARRAY(m_iActiveDominations, int, XS("CTFPlayerResource"), XS("m_iActiveDominations"));
	NETVAR_ARRAY(m_flNextRespawnTime, float, XS("CTFPlayerResource"), XS("m_flNextRespawnTime"));
	NETVAR_ARRAY(m_iChargeLevel, int, XS("CTFPlayerResource"), XS("m_iChargeLevel"));
	NETVAR_ARRAY(m_iDamage, int, XS("CTFPlayerResource"), XS("m_iDamage"));
	NETVAR_ARRAY(m_iDamageAssist, int, XS("CTFPlayerResource"), XS("m_iDamageAssist"));
	NETVAR_ARRAY(m_iDamageBoss, int, XS("CTFPlayerResource"), XS("m_iDamageBoss"));
	NETVAR_ARRAY(m_iHealing, int, XS("CTFPlayerResource"), XS("m_iHealing"));
	NETVAR_ARRAY(m_iHealingAssist, int, XS("CTFPlayerResource"), XS("m_iHealingAssist"));
	NETVAR_ARRAY(m_iDamageBlocked, int, XS("CTFPlayerResource"), XS("m_iDamageBlocked"));
	NETVAR_ARRAY(m_iCurrencyCollected, int, XS("CTFPlayerResource"), XS("m_iCurrencyCollected"));
	NETVAR_ARRAY(m_iBonusPoints, int, XS("CTFPlayerResource"), XS("m_iBonusPoints"));
	NETVAR_ARRAY(m_iPlayerLevel, int, XS("CTFPlayerResource"), XS("m_iPlayerLevel"));
	NETVAR_ARRAY(m_iStreaks, int, XS("CTFPlayerResource"), XS("m_iStreaks"));
	NETVAR_ARRAY(m_iUpgradeRefundCredits, int, XS("CTFPlayerResource"), XS("m_iUpgradeRefundCredits"));
	NETVAR_ARRAY(m_iBuybackCredits, int, XS("CTFPlayerResource"), XS("m_iBuybackCredits"));
	NETVAR(m_iPartyLeaderRedTeamIndex, int, XS("CTFPlayerResource"), XS("m_iPartyLeaderRedTeamIndex"));
	NETVAR(m_iPartyLeaderBlueTeamIndex, int, XS("CTFPlayerResource"), XS("m_iPartyLeaderBlueTeamIndex"));
	NETVAR(m_iEventTeamStatus, int, XS("CTFPlayerResource"), XS("m_iEventTeamStatus"));
	NETVAR_ARRAY(m_iPlayerClassWhenKilled, int, XS("CTFPlayerResource"), XS("m_iPlayerClassWhenKilled"));
	NETVAR_ARRAY(m_iConnectionState, int, XS("CTFPlayerResource"), XS("m_iConnectionState"));
	NETVAR_ARRAY(m_flConnectTime, float, XS("CTFPlayerResource"), XS("m_flConnectTime"));
};