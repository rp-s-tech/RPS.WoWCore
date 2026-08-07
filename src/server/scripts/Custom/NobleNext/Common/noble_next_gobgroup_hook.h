#pragma once

#include "ObjectGuid.h"

#include <string>

using NobleNextGameObjectTransformSavedHook = void (*)(ObjectGuid::LowType);
using NobleNextGameObjectCanDeleteHook = bool (*)(ObjectGuid::LowType, std::string&);
using NobleNextGameObjectDeletedHook = void (*)(ObjectGuid::LowType);

inline NobleNextGameObjectTransformSavedHook NobleNextGameObjectTransformSavedCallback = nullptr;
inline NobleNextGameObjectCanDeleteHook NobleNextGameObjectCanDeleteCallback = nullptr;
inline NobleNextGameObjectDeletedHook NobleNextGameObjectDeletedCallback = nullptr;

inline void NobleNext_RegisterGameObjectTransformSavedHook(NobleNextGameObjectTransformSavedHook callback)
{
    NobleNextGameObjectTransformSavedCallback = callback;
}

inline void NobleNext_RegisterGameObjectDeleteHooks(
    NobleNextGameObjectCanDeleteHook canDeleteCallback, NobleNextGameObjectDeletedHook deletedCallback)
{
    NobleNextGameObjectCanDeleteCallback = canDeleteCallback;
    NobleNextGameObjectDeletedCallback = deletedCallback;
}

inline void NobleNext_OnGameObjectTransformSaved(ObjectGuid::LowType spawnId)
{
    if (NobleNextGameObjectTransformSavedCallback)
        NobleNextGameObjectTransformSavedCallback(spawnId);
}

inline bool NobleNext_CanGameObjectBeDeleted(ObjectGuid::LowType spawnId, std::string& error)
{
    return !NobleNextGameObjectCanDeleteCallback
        || NobleNextGameObjectCanDeleteCallback(spawnId, error);
}

inline void NobleNext_OnGameObjectDeleted(ObjectGuid::LowType spawnId)
{
    if (NobleNextGameObjectDeletedCallback)
        NobleNextGameObjectDeletedCallback(spawnId);
}
