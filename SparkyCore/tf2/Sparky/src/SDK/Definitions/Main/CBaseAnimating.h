#pragma once
#include "CBaseEntity.h"
#include "../Misc/Studio.h"

MAKE_SIGNATURE(CBaseAnimating_FrameAdvance, XS("client.dll"), XS("48 89 5C 24 ? 48 89 6C 24 ? 57 48 81 EC ? ? ? ? 44 0F 29 54 24"), 0x0);
MAKE_SIGNATURE(CBaseAnimating_GetBonePosition, XS("client.dll"), XS("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 8B DA 49 8B F1"), 0x0);
MAKE_SIGNATURE(CBaseAnimating_SequenceDuration, XS("client.dll"), XS("48 89 5C 24 ? 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B D9 8B B9"), 0x0);

class CBaseAnimating : public CBaseEntity
{
public:
	NETVAR(m_nSequence, int, XS("CBaseAnimating"), XS("m_nSequence"));
	NETVAR(m_nForceBone, int, XS("CBaseAnimating"), XS("m_nForceBone"));
	NETVAR(m_vecForce, Vec3, XS("CBaseAnimating"), XS("m_vecForce"));
	NETVAR(m_nSkin, int, XS("CBaseAnimating"), XS("m_nSkin"));
	NETVAR(m_nBody, int, XS("CBaseAnimating"), XS("m_nBody"));
	NETVAR(m_nHitboxSet, int, XS("CBaseAnimating"), XS("m_nHitboxSet"));
	NETVAR(m_flModelScale, float, XS("CBaseAnimating"), XS("m_flModelScale"));
	NETVAR(m_flModelWidthScale, float, XS("CBaseAnimating"), XS("m_flModelWidthScale"));
	NETVAR(m_flPlaybackRate, float, XS("CBaseAnimating"), XS("m_flPlaybackRate"));
	NETVAR(m_flEncodedController, void*, XS("CBaseAnimating"), XS("m_flEncodedController"));
	NETVAR(m_bClientSideAnimation, bool, XS("CBaseAnimating"), XS("m_bClientSideAnimation"));
	NETVAR(m_bClientSideFrameReset, bool, XS("CBaseAnimating"), XS("m_bClientSideFrameReset"));
	NETVAR(m_nNewSequenceParity, int, XS("CBaseAnimating"), XS("m_nNewSequenceParity"));
	NETVAR(m_nResetEventsParity, int, XS("CBaseAnimating"), XS("m_nResetEventsParity"));
	NETVAR(m_nMuzzleFlashParity, int, XS("CBaseAnimating"), XS("m_nMuzzleFlashParity"));
	NETVAR(m_hLightingOrigin, EHANDLE, XS("CBaseAnimating"), XS("m_hLightingOrigin"));
	NETVAR(m_hLightingOriginRelative, EHANDLE, XS("CBaseAnimating"), XS("m_hLightingOriginRelative"));
	NETVAR(m_flCycle, float, XS("CBaseAnimating"), XS("m_flCycle"));
	NETVAR(m_fadeMinDist, float, XS("CBaseAnimating"), XS("m_fadeMinDist"));
	NETVAR(m_fadeMaxDist, float, XS("CBaseAnimating"), XS("m_fadeMaxDist"));
	NETVAR(m_flFadeScale, float, XS("CBaseAnimating"), XS("m_flFadeScale"));
	inline std::array<float, 24>& m_flPoseParameter()
	{
		static int nOffset = U::NetVars.GetNetVar(XS("CBaseAnimating"), XS("m_flPoseParameter"));
		return *reinterpret_cast<std::array<float, 24>*>(uintptr_t(this) + nOffset);
	}

	NETVAR_OFF(GetModelPtr, CStudioHdr*, XS("CBaseAnimating"), XS("m_nMuzzleFlashParity"), 16);
	NETVAR_OFF(m_bSequenceLoops, bool, XS("CBaseAnimating"), XS("m_flFadeScale"), 13);
	NETVAR_OFF(m_CachedBoneData, CUtlVector<matrix3x4>, XS("CBaseAnimating"), XS("m_hLightingOrigin"), -88);

	VIRTUAL_ARGS(GetAttachment, bool, 71, (int number, Vec3& origin), this, number, std::ref(origin))
	VIRTUAL_ARGS(FireEvent, void, 175, (const Vector& origin, const QAngle& angles, int event, const char* options), this, std::ref(origin), std::ref(angles), event, options);

	SIGNATURE_ARGS(FrameAdvance, float, CBaseAnimating, (float flInterval), this, flInterval);
	SIGNATURE_ARGS(GetBonePosition, float, CBaseAnimating, (int iBone, Vector& origin, QAngle& angles), this, iBone, std::ref(origin), std::ref(angles));
	SIGNATURE(SequenceDuration, float, CBaseAnimating, this);
	inline float SequenceDuration(int iSequence)
	{
		int iOriginalSequence = m_nSequence();
		m_nSequence() = iSequence;
		bool bReturn = S::CBaseAnimating_SequenceDuration.Call<float>(this);
		m_nSequence() = iOriginalSequence;
		return bReturn;
	}

	int GetHitboxGroup(int nHitbox);
	int GetNumOfHitboxes();
	Vec3 GetHitboxOrigin(matrix3x4* aBones, int nHitbox, Vec3 vOffset = {});
	Vec3 GetHitboxCenter(matrix3x4* aBones, int nHitbox, Vec3 vOffset = {});
	void GetHitboxInfo(matrix3x4* aBones, int nHitbox, Vec3* pCenter = nullptr, Vec3* pMins = nullptr, Vec3* pMaxs = nullptr, matrix3x4* pMatrix = nullptr, Vec3 vOffset = {});
};

class CBaseAnimatingOverlay : public CBaseAnimating
{
public:

};

class CCurrencyPack : public CBaseAnimating
{
public:
	NETVAR(m_bDistributed, bool, XS("CCurrencyPack"), XS("m_bDistributed"));
};

class CHalloweenPickup : public CBaseAnimating
{
public:
};

class CHalloweenGiftPickup : public CHalloweenPickup
{
public:
	NETVAR(m_hTargetPlayer, EHANDLE, XS("CHalloweenGiftPickup"), XS("m_hTargetPlayer"));
};