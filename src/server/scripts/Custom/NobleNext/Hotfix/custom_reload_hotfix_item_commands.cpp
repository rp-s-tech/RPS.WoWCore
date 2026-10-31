/*
 * NobleNext — .reload item <item_id> (live hotfix reload for a single item).
 */

#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "HotfixPackets.h"
#include "MapUtils.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "Timer.h"
#include "TransmogMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <fmt/format.h>
#include <set>
#include <shared_mutex>
#include <vector>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    class ReloadHotfixItemCommands : public CommandScript
    {
    public:
        ReloadHotfixItemCommands() : CommandScript("noble_next_reload_hotfix_item_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "reload item", HandleReloadItemCommand, rbac::RBAC_PERM_COMMAND_RELOAD_ALL, Console::Yes },
            };
            return commandTable;
        }

        static char const* ToString(DB2RecordLoadResult result)
        {
            switch (result)
            {
                case DB2RecordLoadResult::Loaded:
                    return "loaded";
                case DB2RecordLoadResult::NotFound:
                    return "no_row";
                case DB2RecordLoadResult::SchemaError:
                    return "schema_error";
            }
            return "unknown";
        }

        static bool HandleReloadItemCommand(ChatHandler* handler, uint32 itemId)
        {
            uint32 t0 = getMSTime();

            uint32 schemaErrors = 0;
            auto loadRecord = [&schemaErrors](DB2StorageBase& store, uint32 id, std::string_view tableName)
            {
                DB2RecordLoadResult result = store.LoadRecordFromDB(id, tableName);
                if (result == DB2RecordLoadResult::SchemaError)
                    ++schemaErrors;
                return result;
            };

            DB2RecordLoadResult item = loadRecord(sItemStore, itemId, "item");
            DB2RecordLoadResult sparse = loadRecord(sItemSparseStore, itemId, "item_sparse");
            DB2RecordLoadResult search = loadRecord(sItemSearchNameStore, itemId, "item_search_name");
            bool exists = sItemStore.HasRecord(itemId) && sItemSparseStore.HasRecord(itemId);

            if (!exists && item != DB2RecordLoadResult::Loaded && sparse != DB2RecordLoadResult::Loaded && search != DB2RecordLoadResult::Loaded)
            {
                if (schemaErrors)
                    handler->SendSysMessage(fmt::format("Предмет {} не загружен: hotfix-таблицы не совпадают со структурой сервера, подробности в логе sql.sql.", itemId));
                else
                    handler->SendSysMessage(fmt::format("Предмет {} не найден в hotfix-таблицах и не загружен в память сервера.", itemId));
                return true;
            }

            // Collect IMA IDs: from hotfix DB and from current memory before reloading
            std::set<uint32> imaIds;
            QueryResult imaResult = HotfixDatabase.Query(Trinity::StringFormat("SELECT ID FROM item_modified_appearance WHERE ItemID = {}", itemId).c_str());
            if (imaResult)
            {
                do
                {
                    Field* fields = imaResult->Fetch();
                    imaIds.insert(fields[0].GetUInt32());
                } while (imaResult->NextRow());
            }

            for (ItemModifiedAppearanceEntry const* ima : sItemModifiedAppearanceStore)
            {
                if (ima->ItemID == itemId)
                    imaIds.insert(ima->ID);
            }

            uint32 loadedImaCount = 0;
            for (uint32 id : imaIds)
            {
                if (loadRecord(sItemModifiedAppearanceStore, id, "item_modified_appearance") == DB2RecordLoadResult::Loaded)
                    ++loadedImaCount;
            }

            // Collect ItemAppearance IDs from reloaded IMAs
            std::set<uint32> iaIds;
            for (uint32 id : imaIds)
            {
                if (ItemModifiedAppearanceEntry const* e = sItemModifiedAppearanceStore.LookupEntry(id))
                    if (e->ItemID == itemId)
                        iaIds.insert(e->ItemAppearanceID);
            }

            uint32 loadedIaCount = 0;
            for (uint32 id : iaIds)
            {
                if (loadRecord(sItemAppearanceStore, id, "item_appearance") == DB2RecordLoadResult::Loaded)
                    ++loadedIaCount;
            }

            // Build records list for push reload
            std::vector<std::pair<uint32, int32>> records;
            records.reserve(3 + imaIds.size() + iaIds.size());
            records.emplace_back(sItemStore.GetTableHash(), itemId);
            records.emplace_back(sItemSparseStore.GetTableHash(), itemId);
            records.emplace_back(sItemSearchNameStore.GetTableHash(), itemId);
            for (uint32 id : imaIds)
                records.emplace_back(sItemModifiedAppearanceStore.GetTableHash(), id);
            for (uint32 id : iaIds)
                records.emplace_back(sItemAppearanceStore.GetTableHash(), id);

            uint32 localeMask = sWorld->GetAvailableDbcLocaleMask();
            std::vector<int32> reloadedPushIds = sDB2Manager.ReloadHotfixRecords(records, localeMask);

            // A RecordRemoved hotfix may have just erased the item. Its template stays: Item::GetTemplate() of live items must not start returning nullptr
            bool removed = exists && !(sItemStore.HasRecord(itemId) && sItemSparseStore.HasRecord(itemId));

            bool tmpl = false;
            if (exists && !removed)
                tmpl = sObjectMgr->LoadItemTemplate(itemId);

            std::vector<uint32> explicitIma(imaIds.begin(), imaIds.end());
            TransmogMgr::ReloadItemModifiedAppearances(itemId, &explicitIma);

            // Send ONLY the reloaded push IDs to online players instead of the full hotfix list
            // to avoid client-side freezes
            uint32 sessions = 0;
            if (!reloadedPushIds.empty())
            {
                // Предварительно строим пакеты под каждую локаль вне лока игроков
                std::unordered_map<LocaleConstant, WorldPacket> localePackets;
                for (uint8 loc = 0; loc < TOTAL_LOCALES; ++loc)
                {
                    LocaleConstant locale = static_cast<LocaleConstant>(loc);
                    if (!(localeMask & (1 << locale)))
                        continue;

                    WorldPackets::Hotfix::AvailableHotfixes packet;
                    packet.VirtualRealmAddress = GetVirtualRealmAddress();
                    uint32 sessionLocaleBit = 1 << locale;

                    for (int32 pushId : reloadedPushIds)
                    {
                        if (DB2Manager::HotfixPush const* push = Trinity::Containers::MapGetValuePtr(sDB2Manager.GetHotfixData(), pushId))
                        {
                            if (push->AvailableLocalesMask & sessionLocaleBit)
                                if (!push->Records.empty())
                                    packet.Hotfixes.insert(push->Records.front().ID);
                        }
                    }

                    if (!packet.Hotfixes.empty())
                        localePackets.emplace(locale, *packet.Write());
                }

                if (!localePackets.empty())
                {
                    std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
                    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                    {
                        if (!player)
                            continue;

                        WorldSession* session = player->GetSession();
                        if (!session)
                            continue;

                        auto itr = localePackets.find(session->GetSessionDbcLocale());
                        if (itr == localePackets.end())
                            continue;

                        session->SendPacket(&itr->second);
                        ++sessions;
                    }
                }
            }

            uint32 diff = GetMSTimeDiffToNow(t0);
            TC_LOG_INFO("misc", "Item {} reloaded: item={} sparse={} search={} ima={}/{} ia={}/{} pushes={} template={} sessions={} in {} ms",
                itemId, ToString(item), ToString(sparse), ToString(search), loadedImaCount, imaIds.size(), loadedIaCount, iaIds.size(), reloadedPushIds.size(), tmpl, sessions, diff);

            handler->SendSysMessage(fmt::format("Item {}: item={} sparse={} search={} ima={}/{} ia={}/{} pushes={} template={} sessions={} ({} ms)",
                itemId, ToString(item), ToString(sparse), ToString(search), loadedImaCount, imaIds.size(), loadedIaCount, iaIds.size(), reloadedPushIds.size(), tmpl, sessions, diff));

            if (schemaErrors)
                handler->SendSysMessage(fmt::format("Внимание: {} hotfix-записей не загружено из-за несовпадения структуры таблиц, подробности в логе sql.sql.", schemaErrors));

            if (removed)
                handler->SendSysMessage(fmt::format("Предмет {} удалён хотфиксом (RecordRemoved). Старый шаблон оставлен в памяти до перезапуска сервера.", itemId));

            if (reloadedPushIds.empty())
                handler->SendSysMessage("Примечание: в hotfix_data нет строк для этого предмета, онлайн-клиенты с закэшированным предметом не получат обновление до перезапуска или сброса кэша.");

            return true;
        }
    };
}

void AddSC_NobleNextReloadHotfixItemCommands()
{
    new RoleplayCore::NobleNext::ReloadHotfixItemCommands();
}
