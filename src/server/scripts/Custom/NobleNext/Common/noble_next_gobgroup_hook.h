#pragma once

#include "ObjectGuid.h"

#include <string>

using NobleNextGameObjectTransformSavedHook = void (*)(ObjectGuid::LowType);
using NobleNextGameObjectCanEditHook = bool (*)(ObjectGuid::LowType, std::string&);
using NobleNextGameObjectDeletedHook = void (*)(ObjectGuid::LowType);

inline NobleNextGameObjectTransformSavedHook NobleNextGameObjectTransformSavedCallback = nullptr;
inline NobleNextGameObjectCanEditHook NobleNextGameObjectCanEditCallback = nullptr;
inline NobleNextGameObjectDeletedHook NobleNextGameObjectDeletedCallback = nullptr;

inline void NobleNext_RegisterGameObjectTransformSavedHook(NobleNextGameObjectTransformSavedHook callback)
{
    NobleNextGameObjectTransformSavedCallback = callback;
}

inline void NobleNext_RegisterGameObjectMutationHooks(
    NobleNextGameObjectCanEditHook canEditCallback, NobleNextGameObjectDeletedHook deletedCallback)
{
    NobleNextGameObjectCanEditCallback = canEditCallback;
    NobleNextGameObjectDeletedCallback = deletedCallback;
}

inline void NobleNext_OnGameObjectTransformSaved(ObjectGuid::LowType spawnId)
{
    if (NobleNextGameObjectTransformSavedCallback)
        NobleNextGameObjectTransformSavedCallback(spawnId);
}

inline bool NobleNext_CanGameObjectBeEdited(ObjectGuid::LowType spawnId, std::string& error)
{
    return !NobleNextGameObjectCanEditCallback
        || NobleNextGameObjectCanEditCallback(spawnId, error);
}

inline void NobleNext_OnGameObjectDeleted(ObjectGuid::LowType spawnId)
{
    if (NobleNextGameObjectDeletedCallback)
        NobleNextGameObjectDeletedCallback(spawnId);
}
