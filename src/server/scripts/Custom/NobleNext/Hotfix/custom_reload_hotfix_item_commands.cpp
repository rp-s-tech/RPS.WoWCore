/*
 * NobleNext: .reload item <item_id> (live hotfix reload for a single item).
 */

#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "HotfixPackets.h"
#include "Item.h"
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
#include <map>
#include <tuple>
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

        static bool HandleReloadItemCommand(ChatHandler* handler, uint32 itemId)
        {
            uint32 t0 = getMSTime();
            ItemTemplate const* oldTemplate = sObjectMgr->GetItemTemplate(itemId);
            std::vector<std::pair<DB2StorageBase*, DB2RecordUpdate>> updates;
            std::vector<std::pair<uint32, int32>> records;
            std::map<std::pair<uint32, uint32>, char const*> prepared;
            bool failed = false;
            bool mechanicsChanged = false;

            auto loadRecord = [&](DB2StorageBase& store, uint32 id, std::string_view table, bool protectMechanics)
            {
                auto key = std::pair(store.GetTableHash(), id);
                if (auto itr = prepared.find(key); itr != prepared.end())
                    return itr->second;
                auto snapshot = store.GetSnapshot();
                char const* oldRecord = snapshot && id < snapshot->TableSize ? snapshot->IndexTable[id] : nullptr;
                DB2RecordUpdate update = store.PrepareRecordFromDB(id, table);
                if (update.Result == DB2RecordLoadResult::SchemaError)
                    failed = true;
                char const* data = update.Data ? update.Data.get() : oldRecord;
                if (protectMechanics && oldRecord && store.HasNonStringChanges(oldRecord, data))
                    mechanicsChanged = true;
                records.emplace_back(store.GetTableHash(), id);
                prepared.emplace(key, data);
                if (update.Result == DB2RecordLoadResult::Loaded)
                    updates.emplace_back(&store, std::move(update));
                return data;
            };

            auto collectIds = [&](std::string_view table)
            {
                std::set<uint32> ids;
                QueryResult schema = HotfixDatabase.Query(Trinity::StringFormat(
                    "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() "
                    "AND TABLE_NAME = '{}' AND COLUMN_NAME IN ('ID', 'ItemID')", table).c_str());
                if (!schema || schema->Fetch()[0].GetUInt64() != 2)
                {
                    failed = true;
                    return ids;
                }
                QueryResult result = HotfixDatabase.Query(Trinity::StringFormat(
                    "SELECT ID FROM (SELECT 1) AS sentinel LEFT JOIN `{}` ON ItemID = {}", table, itemId).c_str());
                if (!result)
                    failed = true;
                else
                {
                    do
                    {
                        if (Optional<uint32> id = result->Fetch()[0].GetUInt32OrNull())
                            ids.insert(*id);
                    } while (result->NextRow());
                }
                return ids;
            };

            auto basic = reinterpret_cast<ItemEntry const*>(loadRecord(sItemStore, itemId, "item", true));
            auto sparse = reinterpret_cast<ItemSparseEntry const*>(loadRecord(sItemSparseStore, itemId, "item_sparse", true));
            loadRecord(sItemSearchNameStore, itemId, "item_search_name", true);
            if (!basic || !sparse)
            {
                handler->SendSysMessage("Предмет не обновлён: отсутствуют данные item или item_sparse. Подробности в sql.sql.");
                return true;
            }
            if (oldTemplate && (sItemStore.HasNonStringChanges(reinterpret_cast<char const*>(oldTemplate->BasicData), reinterpret_cast<char const*>(basic))
                || sItemSparseStore.HasNonStringChanges(reinterpret_cast<char const*>(oldTemplate->ExtendedData), reinterpret_cast<char const*>(sparse))))
                mechanicsChanged = true;

            std::set<uint32> imaIds = collectIds("item_modified_appearance");
            for (ItemModifiedAppearanceEntry const* ima : sItemModifiedAppearanceStore)
                if (ima->ItemID == itemId)
                    imaIds.insert(ima->ID);
            std::set<uint32> iaIds;
            for (uint32 id : imaIds)
            {
                auto ima = reinterpret_cast<ItemModifiedAppearanceEntry const*>(loadRecord(sItemModifiedAppearanceStore, id, "item_modified_appearance", false));
                // Moving a mapping would leave the previous item's transmog index stale.
                ItemModifiedAppearanceEntry const* oldIma = sItemModifiedAppearanceStore.LookupEntry(id);
                if (!ima || ima->ItemID != itemId || (oldIma && oldIma->ItemID != itemId))
                    mechanicsChanged = true;
                else
                    iaIds.insert(ima->ItemAppearanceID);
            }
            for (uint32 id : iaIds)
                if (!loadRecord(sItemAppearanceStore, id, "item_appearance", false))
                    failed = true;

            std::set<uint32> linkIds = collectIds("item_x_item_effect");
            for (ItemXItemEffectEntry const* link : sItemXItemEffectStore)
                if (link->ItemID == itemId)
                    linkIds.insert(link->ID);
            std::vector<ItemEffectEntry const*> effects;
            for (uint32 id : linkIds)
            {
                auto link = reinterpret_cast<ItemXItemEffectEntry const*>(loadRecord(sItemXItemEffectStore, id, "item_x_item_effect", true));
                if (!link || link->ItemID != itemId)
                {
                    failed = true;
                    continue;
                }
                auto effect = reinterpret_cast<ItemEffectEntry const*>(loadRecord(sItemEffectStore, link->ItemEffectID, "item_effect", true));
                if (!effect)
                    failed = true;
                else
                    effects.push_back(effect);
            }

            Optional<ItemTemplate> itemTemplate = sObjectMgr->PrepareItemTemplate(itemId, basic, sparse, std::move(effects));
            if (failed || !itemTemplate || itemTemplate->Effects.size() > BonusData{}.Effects.size())
            {
                handler->SendSysMessage("Предмет не обновлён: ошибка БД или отсутствует связанная запись. Память не изменена; подробности в sql.sql.");
                return true;
            }
            if (oldTemplate)
            {
                auto addonState = [](ItemTemplate const& item)
                {
                    return std::tuple(item.FlagsCu, item.MaxDurability, item.ScriptId, item.FoodType,
                        item.MinMoneyLoot, item.MaxMoneyLoot, item.ScrappingLootId, item.SpellPPMRate,
                        item.RandomBonusListTemplateId, item.QuestLogItemId);
                };
                if (addonState(*oldTemplate) != addonState(*itemTemplate) || oldTemplate->Effects.size() != itemTemplate->Effects.size())
                    mechanicsChanged = true;
                else
                    for (std::size_t i = 0; i < oldTemplate->Effects.size(); ++i)
                        if (sItemEffectStore.HasNonStringChanges(reinterpret_cast<char const*>(oldTemplate->Effects[i]),
                            reinterpret_cast<char const*>(itemTemplate->Effects[i])))
                            mechanicsChanged = true;
            }
            if (mechanicsChanged)
            {
                handler->SendSysMessage("Предмет не обновлён: изменена механика существующей вещи или общего эффекта. Требуется перезапуск сервера; память не изменена.");
                return true;
            }

            uint32 localeMask = sWorld->GetAvailableDbcLocaleMask();
            Optional<std::vector<int32>> reloaded = sDB2Manager.ReloadHotfixRecords(records, localeMask);
            if (!reloaded)
            {
                handler->SendSysMessage("Предмет не обновлён: ошибка чтения хотфиксов, удаление или недопустимый статус записи. Память не изменена; подробности в sql.sql.");
                return true;
            }
            // The command and hotfix requests run on WORLD. All reads have succeeded;
            // publish before returning to the session loop or sending notifications.
            for (auto& [store, update] : updates)
                store->CommitRecord(std::move(update));
            sObjectMgr->PublishItemTemplate(itemId, std::move(*itemTemplate));
            std::vector<uint32> explicitIma(imaIds.begin(), imaIds.end());
            TransmogMgr::ReloadItemModifiedAppearances(itemId, &explicitIma);
            std::vector<int32> const& reloadedPushIds = *reloaded;

            // Send ONLY the reloaded push IDs to online players instead of the full hotfix list
            // to avoid client-side freezes
            uint32 sessions = 0;
            if (!reloadedPushIds.empty())
            {
                // Build locale packets before locking the player map.
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
            TC_LOG_INFO("misc", "Item {} reloaded: records={} pushes={} sessions={} in {} ms",
                itemId, records.size(), reloadedPushIds.size(), sessions, diff);
            handler->SendSysMessage(fmt::format("Item {}: records={} pushes={} sessions={} ({} ms)",
                itemId, records.size(), reloadedPushIds.size(), sessions, diff));

            if (reloadedPushIds.empty())
                handler->SendSysMessage("В hotfix_data нет записей для предмета. Клиенты могут сохранить прежние данные в кеше.");

            return true;
        }
    };
}

void AddSC_NobleNextReloadHotfixItemCommands()
{
    new RoleplayCore::NobleNext::ReloadHotfixItemCommands();
}
