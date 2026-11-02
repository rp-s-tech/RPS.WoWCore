#pragma once
#include "Define.h"
#include "Optional.h"
class ChatHandler;
namespace RoleplayCore::NobleNext
{
bool HandleNpcBake(ChatHandler* handler);
bool HandleRouteBake(ChatHandler* handler);
bool HandleGoBake(ChatHandler* handler, Optional<uint64> runtimeId);
bool HandleGoBakeList(ChatHandler* handler);
void UpdateBakes(uint32 diff);
void LoadBakedRoutes();
}
