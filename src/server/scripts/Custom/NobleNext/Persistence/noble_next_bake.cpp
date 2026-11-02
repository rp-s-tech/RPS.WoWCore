#include "noble_next_bake.h"
#include "noble_next_waypoints_service.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PhasingHandler.h"
#include "RBAC.h"
#include "RoleplayCommandPhaseGuard.h"
#include "RoleplayPhaseMgr.h"
#include "RoleplayVisibilityContext.h"
#include "TemporarySummon.h"
#include "WorldSession.h"
#include <map>
#include <memory>
#include <functional>
#include <limits>

namespace RoleplayCore::NobleNext
{
namespace
{
using Routes = WaypointsService;
struct Bake
{
    Routes::Key Source;
    ObjectGuid Author;
    RoleplayPhaseSpawnType Type;
    uint64 SpawnId = 0, PhaseId = 0;
    uint32 Entry = 0, Elapsed = 0, Poll = 0, PreviousPathId = 0;
    Difficulty DifficultyId = DIFFICULTY_NONE;
    Routes::Snapshot Route;
    bool QueryPending = false, ActivationAttempted = false, Done = false;
};
QueryCallbackProcessor Queries;
AsyncCallbackProcessor<TransactionCallback> Commits;
std::map<Routes::Key, std::shared_ptr<Bake>> Pending;
std::map<Routes::Key, uint64> Completed;
constexpr char NextRoutePathSql[] = "SELECT GREATEST(COALESCE((SELECT MAX(PathId) FROM waypoint_path), 0), COALESCE((SELECT MAX(PathId) FROM waypoint_path_node), 0), COALESCE((SELECT MAX(PathId) FROM rps_waypoint_action), 0), COALESCE((SELECT MAX(PathId) FROM creature_addon), 0), COALESCE((SELECT MAX(PathId) FROM creature_template_addon), 0)) + 1, COALESCE(a.PathId, 0) FROM creature c LEFT JOIN creature_addon a ON a.guid = c.guid WHERE c.guid = {} AND c.map = {} AND c.id = {}";
constexpr char ReadRouteSql[] = "SELECT c.spawnDifficulties, c.MovementType, COALESCE(a.PathId, 0), (SELECT COUNT(*) FROM waypoint_path_node WHERE PathId = a.PathId), (SELECT COUNT(*) FROM rps_waypoint_action WHERE PathId = a.PathId) FROM creature c LEFT JOIN creature_addon a ON a.guid = c.guid WHERE c.guid = {} AND c.map = {} AND c.id = {}";

bool SavedRouteMatches(QueryResult const& rows, Routes::Snapshot const& route)
{
    return rows && (*rows)[1].GetUInt8() == (route.PathId ? 2 : 0) && (*rows)[2].GetUInt32() == route.PathId
        && (*rows)[3].GetUInt64() == route.Points.size() && (*rows)[4].GetUInt64() == route.Points.size();
}

void AllocatePath(uint64 spawn, uint32 map, uint32 entry, bool hasRoute,
    std::function<void(uint32, uint32)> ready, std::function<void()> failed)
{
    Queries.AddCallback(WorldDatabase.AsyncQuery(Trinity::StringFormat(NextRoutePathSql, spawn, map, entry).c_str()))
        .WithCallback([hasRoute, ready = std::move(ready), failed = std::move(failed)](QueryResult rows)
    {
        if (!rows || (hasRoute && (*rows)[0].GetUInt64() > std::numeric_limits<uint32>::max())) { failed(); return; }
        ready(hasRoute ? (*rows)[0].GetUInt32() : 0, (*rows)[1].GetUInt32());
    });
}

bool Error(ChatHandler* h, std::string_view text) { h->SendSysMessage(text); h->SetSentErrorMessage(true); return false; }
void Reply(ObjectGuid guid, std::string_view text)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid)) ChatHandler(player->GetSession()).SendSysMessage(text);
}
WorldObject* Source(Bake const& job)
{
    Map* map = sMapMgr->FindMap(job.Source.MapId, job.Source.InstanceId);
    if (!map) return nullptr;
    return job.Type == RoleplayPhaseSpawnType::Creature ? static_cast<WorldObject*>(map->GetCreature(job.Source.Guid))
        : static_cast<WorldObject*>(map->GetGameObject(job.Source.Guid));
}
bool Authorize(ChatHandler* h, WorldObject* object, uint64& phase)
{
    Player* player = h->GetPlayer();
    if (!object || object->GetMap() != player->GetMap() || player->GetExactDist(object) > 30.0f)
        return Error(h, "Выберите временный объект не дальше 30 метров.");
    if (!player->GetMap()->IsWorldMap() || player->GetInstanceId() || object->GetTransport())
        return Error(h, "Фиксация доступна на общих картах вне транспорта.");
    phase = sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), player->GetMapId());
    bool staff = sRoleplayPhaseMgr.CanMutateCommonWorld(player->GetSession()->GetSecurity());
    if ((!phase && !staff) || (phase && !sRoleplayPhaseMgr.CanEdit(phase, player->GetGUID().GetCounter(),
        player->GetSession()->GetAccountId(), player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES), staff)))
        return Error(h, "Нет права создавать постоянные спавны в текущей RP-фазе.");
    ObjectGuid owner = object->GetOwnerGUID();
    if (Creature* c = object->ToCreature())
        if (TempSummon* summon = c->ToTempSummon(); summon && !summon->GetSummonerGUID().IsEmpty())
            owner = summon->GetSummonerGUID();
    if (!owner.IsEmpty() && owner != player->GetGUID())
        return Error(h, "Нельзя фиксировать временный объект другого владельца.");
    if (!player->GetPhaseShift().CanSee(object->GetPhaseShift())
        || (RoleplayVisibilityContext::Resolve(*object) != phase && owner != player->GetGUID()))
        return Error(h, "Временный объект принадлежит другому контексту фазы.");
    return true;
}
void Abort(std::shared_ptr<Bake> const& job, std::string_view reason)
{
    Routes::Instance().Unlock(job->Source);
    job->Done = true;
    if (!job->ActivationAttempted)
    {
        if (job->Type == RoleplayPhaseSpawnType::Creature) Creature::DeleteFromDB(job->SpawnId);
        else GameObject::DeleteFromDB(job->SpawnId);
    }
    else
        Completed[job->Source] = job->SpawnId; // An uncertain commit must never create a second permanent copy on retry.
    Reply(job->Author, Trinity::StringFormat("{} Исходник не заменён. GUID записи: {}.", reason, job->SpawnId));
}
void Publish(std::shared_ptr<Bake> const& job)
{
    Completed[job->Source] = job->SpawnId;
    Map* map = sMapMgr->FindMap(job->Source.MapId, job->Source.InstanceId);
    if (job->Type == RoleplayPhaseSpawnType::Creature)
    {
        CreatureData& data = sObjectMgr->NewOrExistCreatureData(job->SpawnId);
        data.spawnDifficulties = {job->DifficultyId};
        if (!job->Route.Points.empty()) Routes::Instance().RememberSaved(job->SpawnId, job->Source.MapId, job->Route);
        sObjectMgr->AddCreatureToGrid(&data);
        if (map)
        {
            if (Creature* permanent = Creature::CreateCreatureFromDB(job->SpawnId, map, true, true))
            {
                if (!job->Route.Points.empty()) Routes::Instance().InstallSaved(permanent, job->Route);
                if (WorldObject* old = Source(*job))
                {
                    Routes::Instance().Unlock(job->Source);
                    Routes::Instance().StopRoute(old->ToCreature());
                    old->ToCreature()->DespawnOrUnsummon();
                }
                if (Player* player = ObjectAccessor::FindConnectedPlayer(job->Author); player && player->GetMap() == map)
                    player->SetSelection(permanent->GetGUID());
            }
            else Reply(job->Author, "Запись сохранена, но загрузить новый NPC в мир не удалось; исходник оставлен.");
        }
        if (job->Route.PathId) data.movementType = WAYPOINT_MOTION_TYPE;
        if (CreatureAddon const* addon = sObjectMgr->GetCreatureAddon(job->SpawnId)) const_cast<CreatureAddon*>(addon)->PathId = job->Route.PathId;
    }
    else
    {
        GameObjectData& data = sObjectMgr->NewOrExistGameObjectData(job->SpawnId);
        data.spawnDifficulties = {job->DifficultyId};
        sObjectMgr->AddGameobjectToGrid(&data);
        if (map)
        {
            if (GameObject* permanent = GameObject::CreateGameObjectFromDB(job->SpawnId, map))
            {
                if (WorldObject* old = Source(*job)) old->ToGameObject()->Delete();
                if (Player* player = ObjectAccessor::FindConnectedPlayer(job->Author); player && player->GetMap() == map)
                {
                    player->SetLastTargetedGO(permanent->GetSpawnId());
                    player->SetLastTargetedGO2(permanent->GetSpawnId());
                }
            }
            else Reply(job->Author, "Запись сохранена, но загрузить новый объект в мир не удалось; исходник оставлен.");
        }
    }
    Routes::Instance().Unlock(job->Source);
    job->Done = true;
    Reply(job->Author, Trinity::StringFormat("Зафиксировано в БД: GUID {}{}. Сохраняется после перезапуска.",
        job->SpawnId, job->Route.Points.empty() ? "" : " с маршрутом"));
}
void Activate(std::shared_ptr<Bake> const& job)
{
    WorldObject* object = Source(*job);
    Player* author = ObjectAccessor::FindConnectedPlayer(job->Author);
    uint64 phase = 0;
    if (!object || !author) { Abort(job, "Сохранение отменено: исходник или автор вышел из мира."); return; }
    ChatHandler h(author->GetSession());
    if (!Authorize(&h, object, phase) || phase != job->PhaseId)
    { Abort(job, "Контекст изменился до завершения сохранения."); return; }
    auto transaction = WorldDatabase.BeginTransaction();
    char const* table = job->Type == RoleplayPhaseSpawnType::Creature ? "creature" : "gameobject";
    if (job->Type == RoleplayPhaseSpawnType::Creature)
    {
        if (!job->Route.Points.empty() && !Routes::AppendSave(transaction, job->SpawnId, job->Source.MapId, job->Route, job->PreviousPathId))
        { Abort(job, "Спавн изменился до записи маршрута."); return; }
    }
    transaction->PAppend("UPDATE {} SET spawnDifficulties = '{}' WHERE guid = {} AND spawnDifficulties = ''",
        table, uint32(job->DifficultyId), job->SpawnId);
    job->ActivationAttempted = true;
    auto callback = WorldDatabase.AsyncCommitTransaction(transaction);
    callback.AfterComplete([job](bool /*committed*/)
    {
        // Read back even an uncertain commit outcome. The disabled row cannot leak into the common world.
        char const* table = job->Type == RoleplayPhaseSpawnType::Creature ? "creature" : "gameobject";
        std::string query = job->Type == RoleplayPhaseSpawnType::Creature && job->Route.PathId
            ? Trinity::StringFormat(ReadRouteSql, job->SpawnId, job->Source.MapId, job->Entry)
            : Trinity::StringFormat("SELECT spawnDifficulties FROM {} WHERE guid = {}", table, job->SpawnId);
        Queries.AddCallback(WorldDatabase.AsyncQuery(query.c_str()))
            .WithCallback([job](QueryResult rows)
        {
            if (rows && (*rows)[0].GetString() == std::to_string(uint32(job->DifficultyId))
                && (!job->Route.PathId || SavedRouteMatches(rows, job->Route))) Publish(job);
            else if (rows && (*rows)[0].GetString().empty())
            {
                job->ActivationAttempted = false;
                Abort(job, "Запись не активирована: транзакция не завершилась. Проверьте схему маршрутов и ошибки БД.");
            }
            else Abort(job, "Фиксация не подтверждена; повтор заблокирован до сверки записи.");
        });
    });
    Commits.AddCallback(std::move(callback));
}
void PrepareActivation(std::shared_ptr<Bake> const& job)
{
    if (job->Route.Points.empty()) { Activate(job); return; }
    AllocatePath(job->SpawnId, job->Source.MapId, job->Entry, true, [job](uint32 path, uint32 previous)
    {
        job->Route.PathId = path;
        job->PreviousPathId = previous;
        Activate(job);
    }, [job] { Abort(job, "Не удалось подготовить штатный путь."); });
}
void BindPhase(std::shared_ptr<Bake> const& job)
{
    if (!job->PhaseId) { PrepareActivation(job); return; }
    auto transaction = RoleplayDatabase.BeginTransaction();
    auto* statement = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE_SPAWN);
    statement->setUInt64(0, job->PhaseId);
    statement->setString(1, std::string(job->Type == RoleplayPhaseSpawnType::Creature ? "creature" : "gameobject"));
    statement->setUInt64(2, job->SpawnId);
    statement->setUInt32(3, job->Source.MapId);
    transaction->Append(statement);
    auto callback = RoleplayDatabase.AsyncCommitTransaction(transaction);
    callback.AfterComplete([job](bool success)
    {
        // The existing public reload publishes verified roleplay mappings, including native map 0.
        if (!success || !sRoleplayPhaseMgr.Reload()
            || sRoleplayPhaseMgr.GetSpawnPhaseId(job->Type, job->SpawnId, job->Source.MapId) != job->PhaseId)
        { Abort(job, "RP-фаза не сохранена."); return; }
        PrepareActivation(job);
    });
    Commits.AddCallback(std::move(callback));
}
bool Begin(ChatHandler* h, WorldObject* object, RoleplayPhaseSpawnType type, bool requireRoute)
{
    uint64 phase = 0;
    if (!Authorize(h, object, phase)) return false;
    Routes::Key key{object->GetMapId(), object->GetInstanceId(), object->GetGUID()};
    if (auto it = Completed.find(key); it != Completed.end())
        return Error(h, Trinity::StringFormat("Этот исходник уже обработан. GUID постоянной записи: {}.", it->second));
    if (Pending.contains(key)) return Error(h, "Дождитесь подтверждения текущей фиксации.");
    auto job = std::make_shared<Bake>();
    job->Source = key; job->Author = h->GetPlayer()->GetGUID(); job->Type = type;
    job->PhaseId = phase; job->Entry = object->GetEntry(); job->DifficultyId = object->GetMap()->GetDifficultyID();
    if (type == RoleplayPhaseSpawnType::Creature)
    {
        Creature* source = object->ToCreature();
        if (source->GetSpawnId()) return Error(h, "NPC уже постоянный. Для маршрута используйте «Запечь маршрут».");
        if (!source->IsAlive() || source->IsPet() || source->IsGuardian() || source->IsTotem() || source->IsVehicle() || source->IsInCombat() || source->IsCharmed())
            return Error(h, "Выберите живого временного NPC вне боя; питомцы, тотемы и транспорт не фиксируются.");
        if (!Routes::Instance().LockForBake(source, job->Route)) return Error(h, "NPC или маршрут уже сохраняется.");
        if (requireRoute && job->Route.Points.empty()) { Routes::Instance().Unlock(key); return Error(h, "Временный маршрут пуст."); }
        std::unique_ptr<Creature> clone(Creature::CreateCreature(source->GetEntry(), source->GetMap(), source->GetPosition(), InitialPhaseShift(source)));
        if (!clone) { Routes::Instance().Unlock(key); return Error(h, "Не удалось подготовить постоянного NPC."); }
        clone->SetDisplayId(source->GetNativeDisplayId(), true);
        clone->SetObjectScale(source->GetObjectScale());
        clone->SetCurrentEquipmentId(source->GetCurrentEquipmentId());
        clone->SetDefaultMovementType(IDLE_MOTION_TYPE);
        clone->SetDBPhase(source->GetDBPhase());
        // Native SaveToDB serializes the creature. Empty difficulties keep the staging row unloadable after a crash.
        clone->SaveToDB(key.MapId, {});
        job->SpawnId = clone->GetSpawnId();
        clone->CleanupsBeforeDelete();
    }
    else
    {
        GameObject* source = object->ToGameObject();
        if (source->GetSpawnId()) return Error(h, "Объект уже постоянный.");
        std::unique_ptr<GameObject> clone(GameObject::CreateGameObject(source->GetEntry(), source->GetMap(), source->GetPosition(), source->GetLocalRotation(), InitialPhaseShift(source), source->GetGoAnimProgress(), source->GetGoState()));
        if (!clone) return Error(h, "Не удалось подготовить постоянный объект.");
        clone->SetObjectScale(source->GetObjectScale());
        clone->SetVisibilityDistanceOverride(source->GetVisibilityRange());
        clone->SetDBPhase(source->GetDBPhase());
        clone->SaveToDB(key.MapId, {});
        job->SpawnId = clone->GetSpawnId();
        sObjectMgr->NewOrExistGameObjectData(job->SpawnId).visibility = source->GetVisibilityRange();
    }
    Pending[key] = job;
    h->SendSysMessage("Фиксация начата. Исходник останется временным до подтверждённой записи.");
    return true;
}
}

bool HandleNpcBake(ChatHandler* h) { return Begin(h, h->getSelectedCreature(), RoleplayPhaseSpawnType::Creature, false); }
bool HandleGoBakeList(ChatHandler* h)
{
    std::vector<GameObject*> objects;
    h->GetPlayer()->GetGameObjectListWithOptionsInGrid(objects, 30.0f, {});
    uint64 viewerPhase = RoleplayVisibilityContext::Resolve(*h->GetPlayer());
    for (GameObject* object : objects)
        if (!object->GetSpawnId() && h->GetPlayer()->GetPhaseShift().CanSee(object->GetPhaseShift())
            && (RoleplayVisibilityContext::Resolve(*object) == viewerPhase || object->GetOwnerGUID() == h->GetPlayer()->GetGUID()))
            h->PSendSysMessage("Временный объект: ID %llu, entry %u, расстояние %.1f", object->GetGUID().GetCounter(), object->GetEntry(), h->GetPlayer()->GetExactDist(object));
    return true;
}
bool HandleGoBake(ChatHandler* h, Optional<uint64> runtimeId)
{
    uint64 id = runtimeId.value_or(h->GetPlayer()->GetLastTargetedGO());
    std::vector<GameObject*> objects;
    h->GetPlayer()->GetGameObjectListWithOptionsInGrid(objects, 30.0f, {});
    GameObject* source = nullptr;
    bool persistentCollision = false;
    for (GameObject* object : objects)
    {
        if (object->GetSpawnId() == id && id) persistentCollision = true;
        if (!object->GetSpawnId() && (runtimeId ? object->GetGUID().GetCounter() == id : uint32(object->GetGUID().GetCounter()) == id))
        {
            if (source) return Error(h, "Найдено несколько кандидатов. Укажите точный ID из .rps persist go-list.");
            source = object;
        }
    }
    if (!source || (!runtimeId && persistentCollision))
        return Error(h, "Укажите ID временного объекта из .rps persist go-list. После .gobject add temp можно вызвать без ID.");
    return Begin(h, source, RoleplayPhaseSpawnType::GameObject, false);
}
bool HandleRouteBake(ChatHandler* h)
{
    Creature* c = h->getSelectedCreature();
    if (!c) return Error(h, "Выберите NPC с временным маршрутом.");
    if (!c->GetSpawnId())
    {
        if (!h->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_NPC_ADD)) return Error(h, "Для временного NPC требуется право .npc add.");
        return Begin(h, c, RoleplayPhaseSpawnType::Creature, true);
    }
    if (!c->GetMap()->IsWorldMap() || c->GetInstanceId() || c->GetTransport()) return Error(h, "Маршрут фиксируется на общей карте вне транспорта.");
    if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(h->GetPlayer(), h, RoleplayPhaseSpawnType::Creature, c->GetSpawnId(), false, "bake_route")) return false;
    Routes::Snapshot route;
    if (!Routes::Instance().LockForBake(c, route, true)) return Error(h, "Нет черновика маршрута либо он уже сохраняется.");
    Routes::Key key = Routes::KeyFor(c);
    uint64 spawn = c->GetSpawnId();
    uint32 entry = c->GetEntry();
    ObjectGuid author = h->GetPlayer()->GetGUID();
    AllocatePath(spawn, key.MapId, entry, !route.Points.empty(), [key, route, spawn, entry, author](uint32 path, uint32 previous) mutable
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(author);
        Map* map = sMapMgr->FindMap(key.MapId, key.InstanceId);
        Creature* source = map ? map->GetCreature(key.Guid) : nullptr;
        if (!player || !source || source->GetSpawnId() != spawn || player->GetMap() != map)
        { Routes::Instance().Unlock(key); Reply(author, "Сохранение отменено: исходник или автор вышел из мира."); return; }
        ChatHandler handler(player->GetSession());
        if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, &handler, RoleplayPhaseSpawnType::Creature, spawn, false, "bake_route"))
        { Routes::Instance().Unlock(key); return; }
        route.PathId = path;
        auto transaction = WorldDatabase.BeginTransaction();
        if (!Routes::AppendSave(transaction, spawn, key.MapId, route, previous))
        { Routes::Instance().Unlock(key); Reply(author, "Спавн больше не доступен для сохранения."); return; }
        auto callback = WorldDatabase.AsyncCommitTransaction(transaction);
        callback.AfterComplete([key, route, spawn, entry, author](bool success)
        {
            if (!success) { Routes::Instance().Unlock(key); Reply(author, "Маршрут не сохранён; временные точки оставлены."); return; }
            Queries.AddCallback(WorldDatabase.AsyncQuery(Trinity::StringFormat(ReadRouteSql, spawn, key.MapId, entry).c_str()))
                .WithCallback([key, route, spawn, author](QueryResult result)
            {
                Routes::Instance().Unlock(key);
                if (!SavedRouteMatches(result, route))
                { Reply(author, "Запись маршрута не подтверждена; перечитайте NPC перед повтором."); return; }
                if (CreatureData const* data = sObjectMgr->GetCreatureData(spawn)) const_cast<CreatureData*>(data)->movementType = route.PathId ? WAYPOINT_MOTION_TYPE : IDLE_MOTION_TYPE;
                if (CreatureAddon const* addon = sObjectMgr->GetCreatureAddon(spawn)) const_cast<CreatureAddon*>(addon)->PathId = route.PathId;
                Routes::Instance().RememberSaved(spawn, key.MapId, route);
                if (Map* map = sMapMgr->FindMap(key.MapId, key.InstanceId))
                    if (Creature* c = map->GetCreature(key.Guid)) Routes::Instance().InstallSaved(c, route);
                Reply(author, Trinity::StringFormat("Маршрут NPC {} сохранён: {} действий, PathId {}.", spawn, route.Points.size(), route.PathId));
            });
        });
        Commits.AddCallback(std::move(callback));
    }, [key, author] { Routes::Instance().Unlock(key); Reply(author, "Не удалось подготовить штатный путь; черновик оставлен."); });
    h->SendSysMessage("Маршрут сохраняется; дождитесь подтверждения в чате.");
    return true;
}
void UpdateBakes(uint32 diff)
{
    Queries.ProcessReadyCallbacks();
    Commits.ProcessReadyCallbacks();
    for (auto it = Pending.begin(); it != Pending.end();)
    {
        auto job = it->second;
        if (job->Done) { it = Pending.erase(it); continue; }
        ++it;
        if (job->QueryPending) continue;
        job->Elapsed += diff;
        if (job->Elapsed > 15000) { Abort(job, "Запись временного спавна не подтверждена."); continue; }
        job->Poll += diff;
        if (job->Poll < 200) continue;
        job->Poll = 0; job->QueryPending = true;
        char const* table = job->Type == RoleplayPhaseSpawnType::Creature ? "creature" : "gameobject";
        Queries.AddCallback(WorldDatabase.AsyncQuery(Trinity::StringFormat("SELECT id, map, spawnDifficulties FROM {} WHERE guid = {}", table, job->SpawnId).c_str()))
            .WithCallback([job](QueryResult row)
        {
            if (row && (*row)[0].GetUInt32() == job->Entry && (*row)[1].GetUInt32() == job->Source.MapId && (*row)[2].GetString().empty()) BindPhase(job);
            else job->QueryPending = false;
        });
    }
}
void LoadBakedRoutes() { Routes::Instance().LoadSaved(); }
}
