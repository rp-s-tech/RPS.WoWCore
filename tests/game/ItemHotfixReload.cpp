#include "DB2LoadInfo.h"
#include "DB2Store.h"
#include "DB2Structure.h"
#include "catch2/catch_test_macros.hpp"
#include <cstring>

namespace
{
DB2RecordUpdate Prepare(ItemSparseEntry const& record)
{
    DB2RecordUpdate update;
    update.Id = record.ID;
    update.Result = DB2RecordLoadResult::Loaded;
    update.Data = std::make_unique<char[]>(sizeof(record));
    memcpy(update.Data.get(), &record, sizeof(record));
    return update;
}
}

TEST_CASE("Item hotfix preparation and cancellation preserve published records", "[hotfix][items]")
{
    DB2Storage<ItemSparseEntry> store("ItemSparse.db2", &ItemSparseLoadInfo::Instance);
    ItemSparseEntry item{};
    item.ID = 1;
    item.ItemLevel = 10;
    store.CommitRecord(Prepare(item));
    ItemSparseEntry const* original = store.LookupEntry(1);
    {
        item.ItemLevel = 20;
        DB2RecordUpdate rejected = Prepare(item);
        REQUIRE(store.LookupEntry(1) == original);
        REQUIRE(store.LookupEntry(1)->ItemLevel == 10);
    }
    REQUIRE(store.LookupEntry(1) == original);
    store.CommitRecord(Prepare(item));
    REQUIRE(store.LookupEntry(1)->ItemLevel == 20);
    REQUIRE(original->ItemLevel == 10);
}

TEST_CASE("Live item reload permits translated text but rejects cached mechanics", "[hotfix][items]")
{
    DB2Storage<ItemSparseEntry> store("ItemSparse.db2", &ItemSparseLoadInfo::Instance);
    ItemSparseEntry before{};
    before.ID = 1;
    ItemSparseEntry after = before;
    auto changed = [&] { return store.HasNonStringChanges(reinterpret_cast<char const*>(&before), reinterpret_cast<char const*>(&after)); };
    after.Display.Str[LOCALE_ruRU] = "Renamed";
    after.Description.Str[LOCALE_ruRU] = "";
    REQUIRE_FALSE(changed());
    SECTION("level") { after.ItemLevel = 20; }
    SECTION("quality") { after.OverallQualityID = 4; }
    SECTION("required level") { after.RequiredLevel = 19; }
    SECTION("stat array") { after.StatPercentEditor[3] = 500; }
    SECTION("native flags") { after.Flags[1] = 16; }
    REQUIRE(changed());
}

TEST_CASE("Publishing an item hotfix can grow a store without invalidating old readers", "[hotfix][items]")
{
    DB2Storage<ItemSparseEntry> store("ItemSparse.db2", &ItemSparseLoadInfo::Instance);
    ItemSparseEntry item{};
    item.ID = 1;
    store.CommitRecord(Prepare(item));
    auto snapshot = store.GetSnapshot();
    item.ID = 100;
    store.CommitRecord(Prepare(item));
    REQUIRE(store.HasRecord(100));
    REQUIRE(snapshot->TableSize == 2);
    REQUIRE(reinterpret_cast<ItemSparseEntry const*>(snapshot->IndexTable[1])->ID == 1);
}
