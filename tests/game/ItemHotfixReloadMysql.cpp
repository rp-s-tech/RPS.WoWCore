#include "DatabaseEnv.h"
#include "DB2LoadInfo.h"
#include "DB2Stores.h"
#include "ObjectMgr.h"
#include "catch2/catch_test_macros.hpp"
#include <cstdlib>

namespace
{
class HotfixDatabaseFixture
{
public:
    HotfixDatabaseFixture()
    {
        char const* socket = std::getenv("RPS_HOTFIX_TEST_SOCKET");
        REQUIRE(socket);
        // The runner creates this isolated socket and schema; no configured server is used.
        REQUIRE(std::string_view(socket).starts_with("/mnt/wsl/rps-starter-mysql-"));
        HotfixDatabase.SetConnectionInfo(std::string(".;") + socket + ";root;;hotfix_reload_test", 1, 1);
        REQUIRE(HotfixDatabase.Open() == 0);
        WorldDatabase.SetConnectionInfo(std::string(".;") + socket + ";root;;hotfix_reload_test", 1, 1);
        REQUIRE(WorldDatabase.Open() == 0);
        HotfixDatabase.DirectExecute("DELETE FROM item_sparse");
        HotfixDatabase.DirectExecute("DELETE FROM item_sparse_locale");
        HotfixDatabase.DirectExecute("DELETE FROM item_effect");
        HotfixDatabase.DirectExecute("DELETE FROM item_x_item_effect");
        WorldDatabase.DirectExecute("DELETE FROM item_template_addon");
        WorldDatabase.DirectExecute("DELETE FROM item_script_names");
    }
    ~HotfixDatabaseFixture()
    {
        WorldDatabase.Close();
        HotfixDatabase.Close();
    }
};
}

TEST_CASE_METHOD(HotfixDatabaseFixture, "SQL item preparation distinguishes absence and failure", "[.][hotfix-mysql]")
{
    DB2Storage<ItemSparseEntry> store("ItemSparse.db2", &ItemSparseLoadInfo::Instance);
    REQUIRE(store.PrepareRecordFromDB(1, "item_sparse").Result == DB2RecordLoadResult::NotFound);
    REQUIRE(store.PrepareRecordFromDB(1, "missing_item_table").Result == DB2RecordLoadResult::SchemaError);
    REQUIRE_FALSE(store.HasRecord(1));
    HotfixDatabase.DirectExecute("INSERT INTO item_sparse (ID, Display, ItemLevel, VerifiedBuild) VALUES (1, 'Initial', 10, 0)");
    store.CommitRecord(store.PrepareRecordFromDB(1, "item_sparse"));
    auto original = store.LookupEntry(1);
    REQUIRE(original->ItemLevel == 10);
    HotfixDatabase.DirectExecute("UPDATE item_sparse SET Display='Updated', ItemLevel=20 WHERE ID=1");
    {
        auto pending = store.PrepareRecordFromDB(1, "item_sparse");
        REQUIRE(pending.Result == DB2RecordLoadResult::Loaded);
        REQUIRE(store.LookupEntry(1) == original);
        REQUIRE(store.HasNonStringChanges(reinterpret_cast<char const*>(original), pending.Data.get()));
    }
    REQUIRE(store.LookupEntry(1)->ItemLevel == 10);
    HotfixDatabase.DirectExecute("RENAME TABLE item_sparse_locale TO unavailable_locale");
    auto failed = store.PrepareRecordFromDB(1, "item_sparse");
    HotfixDatabase.DirectExecute("RENAME TABLE unavailable_locale TO item_sparse_locale");
    REQUIRE(failed.Result == DB2RecordLoadResult::SchemaError);
    REQUIRE(store.LookupEntry(1) == original);
}

TEST_CASE_METHOD(HotfixDatabaseFixture, "SQL item reload applies custom precedence and clears translated text", "[.][hotfix-mysql]")
{
    DB2Storage<ItemSparseEntry> store("ItemSparse.db2", &ItemSparseLoadInfo::Instance);
    HotfixDatabase.DirectExecute("INSERT INTO item_sparse (ID, Display, ItemLevel, VerifiedBuild) VALUES (1, 'Verified', 5, 123), (1, 'Custom', 10, 0)");
    HotfixDatabase.DirectExecute("INSERT INTO item_sparse_locale (ID, locale, Display_lang, VerifiedBuild) VALUES (1, 'ruRU', 'Localized', 0)");
    store.CommitRecord(store.PrepareRecordFromDB(1, "item_sparse"));
    REQUIRE(store.LookupEntry(1)->ItemLevel == 10);
    REQUIRE(std::string_view(store.LookupEntry(1)->Display.Str[LOCALE_enUS]) == "Custom");
    REQUIRE(std::string_view(store.LookupEntry(1)->Display.Str[LOCALE_ruRU]) == "Localized");
    HotfixDatabase.DirectExecute("UPDATE item_sparse_locale SET Display_lang='' WHERE ID=1");
    auto pending = store.PrepareRecordFromDB(1, "item_sparse");
    REQUIRE_FALSE(store.HasNonStringChanges(reinterpret_cast<char const*>(store.LookupEntry(1)), pending.Data.get()));
    store.CommitRecord(std::move(pending));
    REQUIRE(std::string_view(store.LookupEntry(1)->Display.Str[LOCALE_ruRU]).empty());
}

TEST_CASE_METHOD(HotfixDatabaseFixture, "SQL item effects and links are prepared before publication", "[.][hotfix-mysql]")
{
    DB2Storage<ItemEffectEntry> effects("ItemEffect.db2", &ItemEffectLoadInfo::Instance);
    DB2Storage<ItemXItemEffectEntry> links("ItemXItemEffect.db2", &ItemXItemEffectLoadInfo::Instance);
    HotfixDatabase.DirectExecute("INSERT INTO item_effect (ID, SpellID, VerifiedBuild) VALUES (1, 16591, 0)");
    HotfixDatabase.DirectExecute("INSERT INTO item_x_item_effect (ID, ItemID, ItemEffectID, VerifiedBuild) VALUES (1, 2, 1, 0)");
    auto effect = effects.PrepareRecordFromDB(1, "item_effect");
    auto link = links.PrepareRecordFromDB(1, "item_x_item_effect");
    REQUIRE(effect.Result == DB2RecordLoadResult::Loaded);
    REQUIRE(link.Result == DB2RecordLoadResult::Loaded);
    REQUIRE_FALSE(effects.HasRecord(1));
    REQUIRE_FALSE(links.HasRecord(1));
    effects.CommitRecord(std::move(effect));
    links.CommitRecord(std::move(link));
    REQUIRE(effects.LookupEntry(links.LookupEntry(1)->ItemEffectID)->SpellID == 16591);
    HotfixDatabase.DirectExecute("UPDATE item_effect SET SpellID=1 WHERE ID=1");
    auto changed = effects.PrepareRecordFromDB(1, "item_effect");
    REQUIRE(effects.HasNonStringChanges(reinterpret_cast<char const*>(effects.LookupEntry(1)), changed.Data.get()));
}

TEST_CASE_METHOD(HotfixDatabaseFixture, "Hotfix SQL failure and deletion preserve pushes and skip publication", "[.][hotfix-mysql]")
{
    HotfixDatabase.DirectExecute("DELETE FROM hotfix_data");
    HotfixDatabase.DirectExecute("DELETE FROM hotfix_blob");
    HotfixDatabase.DirectExecute("INSERT INTO hotfix_blob (TableHash, RecordId, locale, `Blob`) VALUES (123, 1, 'enUS', 'x'), (123, 2, 'enUS', 'y')");
    sDB2Manager.LoadHotfixBlob(1);
    HotfixDatabase.DirectExecute("INSERT INTO hotfix_data (Id, UniqueId, TableHash, RecordId, Status) VALUES (77, 1, 123, 1, 1), (77, 2, 123, 2, 1)");
    DB2Storage<ItemSparseEntry> store("ItemSparse.db2", &ItemSparseLoadInfo::Instance);
    HotfixDatabase.DirectExecute("INSERT INTO item_sparse (ID, Display, VerifiedBuild) VALUES (1, 'Initial', 0)");
    store.CommitRecord(store.PrepareRecordFromDB(1, "item_sparse"));
    std::vector<std::pair<uint32, int32>> keys{{123, 1}};
    auto reload = [&]
    {
        auto pending = store.PrepareRecordFromDB(1, "item_sparse");
        REQUIRE(pending.Result == DB2RecordLoadResult::Loaded);
        auto pushes = sDB2Manager.ReloadHotfixRecords(keys, 1);
        if (pushes)
            store.CommitRecord(std::move(pending));
        return pushes;
    };
    HotfixDatabase.DirectExecute("UPDATE item_sparse SET Display='Published' WHERE ID=1");
    REQUIRE(reload());
    ItemSparseEntry const* original = store.LookupEntry(1);
    REQUIRE(std::string_view(original->Display.Str[LOCALE_enUS]) == "Published");
    REQUIRE(sDB2Manager.GetHotfixData().at(77).Records.size() == 2);
    HotfixDatabase.DirectExecute("UPDATE item_sparse SET Display='Retried' WHERE ID=1");
    HotfixDatabase.DirectExecute("RENAME TABLE hotfix_data TO unavailable_hotfix_data");
    auto failed = reload();
    HotfixDatabase.DirectExecute("RENAME TABLE unavailable_hotfix_data TO hotfix_data");
    REQUIRE_FALSE(failed);
    REQUIRE(store.LookupEntry(1) == original);
    REQUIRE(std::string_view(original->Display.Str[LOCALE_enUS]) == "Published");
    REQUIRE(sDB2Manager.GetHotfixData().at(77).Records.size() == 2);
    // Same columns, but the actual SELECT fails after schema validation.
    HotfixDatabase.DirectExecute("CREATE FUNCTION fail_reload() RETURNS TINYINT DETERMINISTIC "
        "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Injected hotfix read failure'");
    HotfixDatabase.DirectExecute("RENAME TABLE hotfix_data TO saved_hotfix_data");
    HotfixDatabase.DirectExecute("CREATE VIEW hotfix_data AS SELECT Id, UniqueId, TableHash, RecordId, "
        "fail_reload() AS Status, VerifiedBuild FROM saved_hotfix_data");
    auto readFailure = reload();
    HotfixDatabase.DirectExecute("DROP VIEW hotfix_data");
    HotfixDatabase.DirectExecute("RENAME TABLE saved_hotfix_data TO hotfix_data");
    HotfixDatabase.DirectExecute("DROP FUNCTION fail_reload");
    REQUIRE_FALSE(readFailure);
    REQUIRE(store.LookupEntry(1) == original);
    REQUIRE(std::string_view(original->Display.Str[LOCALE_enUS]) == "Published");
    REQUIRE(sDB2Manager.GetHotfixData().at(77).Records.size() == 2);
    HotfixDatabase.DirectExecute("UPDATE hotfix_data SET Status=2 WHERE RecordId=1");
    REQUIRE_FALSE(reload());
    REQUIRE(store.LookupEntry(1) == original);
    REQUIRE(sDB2Manager.GetHotfixData().at(77).Records.size() == 2);
    HotfixDatabase.DirectExecute("UPDATE hotfix_data SET Status=1, UniqueId=3 WHERE RecordId=1");
    REQUIRE(reload());
    REQUIRE(std::string_view(store.LookupEntry(1)->Display.Str[LOCALE_enUS]) == "Retried");
    REQUIRE(std::string_view(original->Display.Str[LOCALE_enUS]) == "Published");
    REQUIRE(sDB2Manager.GetHotfixData().at(77).Records.size() == 2);
}

TEST_CASE_METHOD(HotfixDatabaseFixture, "Item template reload preserves RP policy and published state on WORLD read failure", "[.][hotfix-mysql]")
{
    // Published templates retain these pointers for the lifetime of ObjectMgr.
    static ItemEntry basic{};
    static ItemSparseEntry sparse{};
    static ItemEffectEntry effect{};
    basic.ID = sparse.ID = 1900099;
    basic.ClassID = ITEM_CLASS_ARMOR;
    basic.SubclassID = ITEM_SUBCLASS_ARMOR_CLOTH;
    sparse.InventoryType = INVTYPE_CHEST;
    sparse.OverallQualityID = ITEM_QUALITY_NORMAL;
    sparse.ItemLevel = 10;
    effect.SpellID = 16591;
    WorldDatabase.DirectExecute("INSERT INTO item_template_addon (Id, FlagsCu, RandomBonusListTemplateId) VALUES (1900099, 16, 77)");
    WorldDatabase.DirectExecute("INSERT INTO item_script_names (Id, ScriptName) VALUES (1900099, '')");
    auto initial = sObjectMgr->PrepareItemTemplate(basic.ID, &basic, &sparse, {&effect});
    REQUIRE(initial);
    REQUIRE(initial->MaxDurability > 0);
    REQUIRE(initial->RandomBonusListTemplateId == 77);
    REQUIRE(initial->Effects.size() == 1);
    sObjectMgr->PublishItemTemplate(basic.ID, std::move(*initial));
    ItemTemplate const* published = sObjectMgr->GetItemTemplate(basic.ID);
    REQUIRE(published);

    WorldDatabase.DirectExecute("UPDATE item_template_addon SET FlagsCu=24 WHERE Id=1900099");
    auto pending = sObjectMgr->PrepareItemTemplate(basic.ID, &basic, &sparse, {&effect});
    REQUIRE(pending);
    REQUIRE(pending->IsRoleplayOnly());
    REQUIRE(pending->HasFlag(ITEM_FLAGS_CU_NO_VENDOR_VALUE));
    REQUIRE(pending->MaxDurability == 0);
    REQUIRE(pending->RandomBonusListTemplateId == 0);
    REQUIRE(pending->Effects.empty());
    REQUIRE_FALSE(published->IsRoleplayOnly());
    REQUIRE(published->MaxDurability > 0);
    REQUIRE(published->RandomBonusListTemplateId == 77);
    REQUIRE(published->Effects.size() == 1);

    WorldDatabase.DirectExecute("RENAME TABLE item_template_addon TO unavailable_addon");
    auto missingTable = sObjectMgr->PrepareItemTemplate(basic.ID, &basic, &sparse, {&effect});
    WorldDatabase.DirectExecute("RENAME TABLE unavailable_addon TO item_template_addon");
    REQUIRE_FALSE(missingTable);
    REQUIRE(sObjectMgr->GetItemTemplate(basic.ID) == published);
    REQUIRE_FALSE(published->IsRoleplayOnly());

    WorldDatabase.DirectExecute("CREATE FUNCTION fail_script_reload() RETURNS VARCHAR(64) DETERMINISTIC "
        "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Injected item script read failure'");
    WorldDatabase.DirectExecute("RENAME TABLE item_script_names TO saved_item_script_names");
    WorldDatabase.DirectExecute("CREATE VIEW item_script_names AS SELECT Id, fail_script_reload() AS ScriptName FROM saved_item_script_names");
    auto readFailure = sObjectMgr->PrepareItemTemplate(basic.ID, &basic, &sparse, {&effect});
    WorldDatabase.DirectExecute("DROP VIEW item_script_names");
    WorldDatabase.DirectExecute("RENAME TABLE saved_item_script_names TO item_script_names");
    WorldDatabase.DirectExecute("DROP FUNCTION fail_script_reload");
    REQUIRE_FALSE(readFailure);
    REQUIRE(sObjectMgr->GetItemTemplate(basic.ID) == published);
    REQUIRE_FALSE(published->IsRoleplayOnly());
    REQUIRE(published->Effects.size() == 1);

    pending = sObjectMgr->PrepareItemTemplate(basic.ID, &basic, &sparse, {&effect});
    REQUIRE(pending);
    sObjectMgr->PublishItemTemplate(basic.ID, std::move(*pending));
    REQUIRE(published->IsRoleplayOnly());
    REQUIRE(published->HasFlag(ITEM_FLAGS_CU_NO_VENDOR_VALUE));
    REQUIRE(published->MaxDurability == 0);
    REQUIRE(published->RandomBonusListTemplateId == 0);
    REQUIRE(published->Effects.empty());
}
