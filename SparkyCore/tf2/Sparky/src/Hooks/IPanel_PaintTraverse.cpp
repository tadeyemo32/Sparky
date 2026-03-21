#include "../SDK/SDK.h"

MAKE_HOOK(IPanel_PaintTraverse, U::Memory.GetVirtual(I::Panel, 41), void,
	void* rcx, VPANEL vguiPanel, bool forceRepaint, bool allowForce)
{
	DEBUG_RETURN(IPanel_PaintTraverse, rcx, vguiPanel, forceRepaint, allowForce);

	if (!Vars::Visuals::UI::StreamerMode.Value)
		return CALL_ORIGINAL(rcx, vguiPanel, forceRepaint, allowForce);

	switch (FNV1A::Hash32(I::Panel->GetName(vguiPanel)))
	{
	case FNV1A::Hash32Const(XS("SteamFriendsList")):
	case FNV1A::Hash32Const(XS("avatar")):
	case FNV1A::Hash32Const(XS("RankPanel")):
	case FNV1A::Hash32Const(XS("ModelContainer")):
	case FNV1A::Hash32Const(XS("ServerLabelNew")):
		return;
	}

	CALL_ORIGINAL(rcx, vguiPanel, forceRepaint, allowForce);
}