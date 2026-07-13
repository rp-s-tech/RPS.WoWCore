#ifndef _Roleplay_H
#define _Roleplay_H

#include "ObjectMgr.h"
#include "GameObject.h"
#include "Creature.h"
#include "Log.h"
#include "CreatureOutfit.h"
#include "Item.h"
#include "RaceMask.h"
#include "Optional.h"
#include "UpdateField.h"
#include <fmt/printf.h>

struct PlayerExtraData
{
    ObjectGuid::LowType selectedCreatureGuid;
    WorldLocation markerLocation;
};

typedef std::unordered_map<ObjectGuid::LowType, PlayerExtraData> PlayerExtraDataContainer;

struct CustomNpcData
{
    std::string key;
    uint32 templateId;
    uint32 ownerBnetAccountId = 0;
    std::string ownerAlias;
    std::vector<ObjectGuid::LowType> spawns;
};

typedef std::unordered_map<std::string, CustomNpcData> CustomNpcDataContainer;

struct CustomNpcReloadBundle
{
    CreatureTemplate creatureTemplate;
    std::vector<CreatureModel> models;
    std::unordered_map<uint32, std::shared_ptr<CreatureOutfit>> outfits;
    std::unordered_map<uint8, EquipmentInfo> equipment;
    Optional<CreatureAddon> addon;
};

struct CustomNpcReloadSummary
{
    uint32 successCount = 0;
    uint32 failedCount = 0;
};

struct CustomNpcRaceDescriptor
{
    uint32 listIndex = 0;
    Races race = RACE_NONE;
    std::string name;
    bool available = false;
    uint32 maleChrModelId = 0;
    uint32 femaleChrModelId = 0;
    uint32 maleDisplayId = 0;
    uint32 femaleDisplayId = 0;
};

struct CreatureExtraData
{
    CreatureExtraData() : scale(-1.0f), creatorBnetAccId(0), creatorPlayerId(0),
        modifierBnetAccId(0), modifierPlayerId(0), created(0), modified(0), phaseMask(1),
        displayLock(false), displayId(0), nativeDisplayId(0), genderLock(false), gender(0),
        swim(true), gravity(true), fly(false) { }

    float scale;
    uint32 creatorBnetAccId;
    uint64 creatorPlayerId;
    uint32 modifierBnetAccId;
    uint64 modifierPlayerId;
    time_t created;
    time_t modified;
    uint32 phaseMask;
    bool displayLock;
    uint32 displayId;
    uint32 nativeDisplayId;
    bool genderLock;
    uint8 gender;
    bool swim;
    bool gravity;
    bool fly;
};

typedef std::unordered_map<uint64, CreatureExtraData> CreatureExtraContainer;

struct CreatureTemplateExtraData
{
    bool disabled;
};

typedef std::unordered_map<uint32, CreatureTemplateExtraData> CreatureTemplateExtraContainer;

class Map;

using G3D::Vector3;

enum RotationAxis
{
    AXIS_ROLL,
    AXIS_PITCH,
    AXIS_YAW
};

class TC_GAME_API Roleplay
{
private:
    Roleplay();
    ~Roleplay();
public:
    static Roleplay* instance();
    void LoadAllTables();

    // Creature
    void LoadCreatureExtras();
    void LoadCreatureTemplateExtras();
    void SetCreatureTemplateExtraDisabledFlag(uint32 entryId, bool disabled);
    void SaveCreature(Creature* creature);
    void CreatureSetModifyHistory(Creature* creature, Player* modifier);
    void CreatureMove(Creature* creature, float x, float y, float z, float o);
    void CreatureTurn(Creature* creature, float o);
    void CreatureScale(Creature* creature, float scale);
    void CreatureDelete(Creature* creature);
    void CreatureSetEmote(Creature* creature, uint32 emoteId);
    void CreatureSetMount(Creature* creature, uint32 mountId);
    void CreatureSetAuraToggle(Creature* creature, uint32 auraId, bool toggle);
    void CreatureSetGravity(Creature* creature, bool toggle);
    void CreatureSetSwim(Creature* creature, bool toggle);
    void CreatureSetFly(Creature* creature, bool toggle);
    void CreatureSetAnimKitId(Creature* creature, uint16 animKitId);
    void CreatureSetModel(Creature* creature, uint32 displayId);
    bool CreatureCanSwim(Creature const* creature);
    bool CreatureCanWalk(Creature const* creature);
    bool CreatureCanFly(Creature const* creature);
    Creature* CreatureCreate(Player* creator, CreatureTemplate const* creatureTemplate);
    void CreatureRefresh(Creature* creature);
    CreatureExtraData const* GetCreatureExtraData(uint64 guid);
    CreatureTemplateExtraData const* GetCreatureTemplateExtraData(uint32 entry);
    void SetCreatureSelectionForPlayer(ObjectGuid::LowType playerId, ObjectGuid::LowType creatureId);
    ObjectGuid::LowType GetSelectedCreatureGuidFromPlayer(ObjectGuid::LowType playerId);
    Creature* GetAnyCreature(ObjectGuid::LowType lowguid);
    Creature* GetAnyCreature(Map* map, ObjectGuid::LowType lowguid, uint32 entry);
    Unit* GetAnyUnit(ObjectGuid::LowType lowguid);

    // Misc
    std::string GetMapName(uint32 mapId);
    std::string ToChatLink(std::string type, uint64 key, std::string name) { return "|cffffffff|" + type + ":" + fmt::sprintf("%llu", key) + "|h[" + name + "]|h|r"; }
    std::string GetChatLinkKey(std::string const& src, std::string type);
    std::string ToDateTimeString(time_t t);
    std::string ToDateString(time_t t);

    // Custom NPCs
    CustomNpcDataContainer GetCustomNpcContainer() { return _customNpcStore; }
    bool CustomNpcNameExists(std::string const& key) const { return _customNpcStore.count(key) > 0; }
    uint32 GetEntryIdForNpc(std::string const& key) { return _customNpcStore[key].templateId; }
    uint8 GetModelVariationCountForNpc(std::string const& key) const;
    uint8 GetEquipmentVariationCountForNpc(std::string const& key) const;
    uint8 GetEquipmentVariationCountForNpc(uint32 templateId) const;
    void LoadCustomNpcs();
    std::string GenerateCustomNpcKey(Player const* owner) const;
    bool CreateBlankCustomNpc(Player* owner, std::string const& key, std::string const& displayName);
    bool CreateCustomNpcFromPlayer(Player* player, std::string const& key, std::string const& displayName, Player* owner = nullptr);
    bool CreateCustomNpcFromCreature(Creature* creature, std::string const& key, std::string const& displayName, Player* owner = nullptr);
    bool ImportCustomNpcFromEntry(uint32 entry, std::string const& key);
    bool ReloadCustomNpcFromDb(std::string const& key);
    CustomNpcReloadSummary ReloadAllCustomNpcsFromDb();
    std::string const* GetCustomNpcKeyForEntry(uint32 templateId) const;
    std::vector<Creature*> GetLiveCustomNpcCreatures(std::string const& key) const;
    void RebuildCustomNpcSpawnsFromObjectMgr(std::string const& key);
    bool RefreshCustomNpcCreatures(std::string const& key, Optional<uint8> variationId = {});
    bool DiagnoseCustomNpc(std::string const& key, std::ostringstream& report) const;
    bool ApplyCustomNpcToCreature(std::string const& key, Creature* creature, uint8 variationId);
    std::vector<CustomNpcRaceDescriptor> BuildCustomNpcRaceList(bool availableOnly = false) const;
    std::vector<ChrCustomizationOptionEntry const*> BuildCustomizationOptionList(Races race, Gender gender) const;
    std::vector<ChrCustomizationChoiceEntry const*> BuildCustomizationChoiceList(uint32 optionId) const;
    std::vector<ChrCustomizationChoiceEntry const*> BuildCustomizationChoiceListForOutfit(uint32 optionId, CreatureOutfit const& outfit) const;
    uint32 FindCustomizationChoiceIndex(CreatureOutfit const& outfit, ChrCustomizationOptionEntry const* option) const;
    std::string GetCustomizationOptionLabel(ChrCustomizationOptionEntry const* option, LocaleConstant locale) const;
    std::string GetCustomizationChoiceLabel(ChrCustomizationChoiceEntry const* choice, LocaleConstant locale) const;
    CreatureOutfit const* GetCustomNpcOutfit(std::string const& key, uint8 variation) const;
    bool SetCustomNpcCustomizationByIndex(std::string const& key, uint8 variation, uint32 optionIndex, uint32 choiceIndex, std::string& error);
    bool SetCustomNpcFaceFromSelf(std::string const& key, uint8 variation, Player* player, std::string& error);
    bool CopyCustomNpcCustomizationOptionByIndex(std::string const& key, uint8 variation, uint32 optionIndex, Player* player, std::string& error);
    bool AddCustomNpcModelVariationBlank(std::string const& key, Optional<uint8> sourceVariation, uint8& newVariation, std::string& error);
    bool AddCustomNpcModelVariationFromPlayer(std::string const& key, Player* source, uint8& newVariation, std::string& error);
    bool AddCustomNpcModelVariationFromCreature(std::string const& key, Creature* source, uint8& newVariation, std::string& error);
    bool AddCustomNpcModelVariationFromCustomNpc(std::string const& key, std::string const& sourceKey, Optional<uint8> sourceVariation, uint8& newVariation, std::string& error);
    bool CopyCustomNpcModelVariation(std::string const& key, uint8 fromVariation, Optional<uint8> toVariation, uint8& actualVariation, std::string& error, std::string const& sourceKey = {});
    bool ApplyCustomNpcModelVariationToCreature(std::string const& key, Creature* creature, uint8 variation, std::string& error);
    bool CloneCustomNpcFromKey(std::string const& sourceKey, std::string const& newKey, std::string const& displayName, Player* owner, Optional<uint8> sourceVariation, std::string& error);
    bool CloneCustomNpcFromEntry(uint32 entry, std::string const& key, std::string const& displayName, Player* owner, Optional<uint8> variation, std::string& error);
    bool CopyCustomNpcEquipmentFromCustomNpc(std::string const& key, std::string const& sourceKey, uint8 variation, Optional<uint8> sourceEquipmentVariation, std::string& error);
    bool CanEditCustomNpc(std::string const& key, Player* player, bool allowAdmin) const;
    void EnsureCustomNpcOwner(std::string const& key, Player* owner);
    std::string GetCustomNpcOwnerDisplay(std::string const& key) const;
    void CopyCustomNpcArmorFromPlayer(std::string const& key, uint8 variationId, Player* player);
    void CopyCustomNpcArmorSlotFromPlayer(std::string const& key, uint8 variationId, EquipmentSlots slot, Player* player);
    bool CopyCustomNpcWeaponFromPlayer(std::string const& key, uint8 variationId, uint8 equipmentSlot, Player* player);
    int32 GetItemDisplayIdFromTemplate(ItemTemplate const* item, uint32 appearanceModId = 0) const;
    void SetCustomNpcOutfitEquipmentSlot(std::string const& key, uint8 variationId, EquipmentSlots slot, int32 displayId);
    bool SetCustomNpcOutfitRace(std::string const& key, uint8 variationId, Races race, std::string& error);
    bool SetCustomNpcOutfitGender(std::string const& key, uint8 variationId, Gender gender, std::string& error);
    void SetCustomNpcLeftHand(std::string const& key, uint8 variationId, int32 itemId, int32 appearanceModId);
    void SetCustomNpcRightHand(std::string const& key, uint8 variationId, int32 itemId, int32 appearanceModId);
    void SetCustomNpcRanged(std::string const& key, uint8 variationId, int32 itemId, int32 appearanceModId);
    void SetCustomNpcName(std::string const& key, std::string const& displayName);
    void SetCustomNpcSubName(std::string const& key, std::string const& subName);
    void SetCustomNpcCustomizations(std::string const& key, uint8 variationId, Player* player);
    bool SetCustomNpcCustomizationOption(std::string const& key, uint8 variationId, uint32 optionId, uint32 choiceId, std::string& error);
    void CopyCustomNpcCustomizationOptionFromPlayer(std::string const& key, uint8 variationId, uint32 optionId, Player* player);
    void SetCustomNpcStandState(std::string const& key, uint8 standState);
    void SetCustomNpcDisplayId(std::string const& key, uint8 variationId, uint32 displayId);
    void SetCustomNpcModelScale(std::string const& key, uint8 variationId, float displayScale);
    void SetCustomNpcGuild(std::string const& key, uint8 variationId, uint64 guildId);
    void SetCustomNpcTameable(std::string const& key, bool tameable);
    void LoadCustomNpcSpawn(uint32 templateId, ObjectGuid::LowType spawn);
    void RemoveCustomNpcVariation(std::string const& key, uint8 variationId);
    void DeleteCustomNpc(std::string const& key);

    void SetNpcLeftHand(uint32 templateId, uint8 variationId, int32 itemId, int32 appearanceModId);
    void SetNpcRightHand(uint32 templateId, uint8 variationId, int32 itemId, int32 appearanceModId);
    void SetNpcRanged(uint32 templateId, uint8 variationId, int32 itemId, int32 appearanceModId);

    // Marker
    void StoreMarkerLocationForPlayer(Player* player, const WorldLocation* marker);
    WorldLocation* GetMarketLocationForPlayer(Player* player) { return &_playerExtraDataStore[player->GetGUID().GetCounter()].markerLocation; }

protected:
    PlayerExtraDataContainer _playerExtraDataStore;
    CreatureExtraContainer _creatureExtraStore;
    CreatureTemplateExtraContainer _creatureTemplateExtraStore;
    CustomNpcDataContainer _customNpcStore;

private:
    void SaveNpcOutfitToDb(uint32 templateId, uint8 variationId);
    void SaveCustomNpcDataToDb(CustomNpcData outfitData);
    void SaveNpcCreatureTemplateToDb(CreatureTemplate& cTemplate);
    void SaveNpcEquipmentInfoToDb(uint32 templateId, uint8 variationId);
    void SaveNpcCreatureTemplateAddonToDb(uint32 templateId, CreatureAddon cAddon);
    void SaveCustomNpcOwnerToDb(CustomNpcData const& npcData);
    void SaveNpcModelInfo(CreatureModel model, uint32 creatureTemplateId, uint8 variationId);
    void ReloadSpawnedCustomNpcs(std::string const& key, Optional<uint8> variationId = {});
    bool TryLoadCustomNpcBundleFromDb(uint32 templateId, CustomNpcReloadBundle& out, std::string& error) const;
    bool CommitCustomNpcBundle(uint32 templateId, CustomNpcReloadBundle&& bundle);
    bool ValidateCreatureOutfit(CreatureOutfit const& outfit, std::string& error) const;
    bool LoadCreatureOutfitFromFields(Field* fields, std::shared_ptr<CreatureOutfit>& out, std::string& error) const;
    void LoadCreatureTemplateFromFields(Field* fields, CreatureTemplate& creatureTemplate) const;
    CreatureOutfit* GetMutableCustomNpcOutfit(std::string const& key, uint8 variation, std::string& error);
    bool TryGetCustomNpcOutfitCopy(std::string const& key, uint8 variation, CreatureOutfit& out, std::string& error);
    bool ValidateAndSaveCustomNpcOutfit(std::string const& key, uint8 variation, CreatureOutfit& outfit, std::string& error);
    void ApplyCustomNpcStateToCreature(Creature* creature, uint32 templateId, uint8 variation);
    void ReplaceCustomization(std::vector<UF::ChrCustomizationChoice>& customizations, uint32 optionId, uint32 choiceId);
    void SortCustomizationsByOptions(std::vector<UF::ChrCustomizationChoice>& customizations, std::vector<ChrCustomizationOptionEntry const*> const& options);
    void NormalizeOutfitCustomizations(CreatureOutfit& outfit);
    bool MeetsCustomizationReqForOutfit(ChrCustomizationReqEntry const* req, CreatureOutfit const& outfit, bool checkRequiredDependentChoices) const;
    std::shared_ptr<CreatureOutfit> DuplicateOutfit(CreatureOutfit const& source, uint32 newOutfitId);
    bool PopulateOutfitFromPlayer(Player* player, CreatureOutfit& outfit);
    bool PopulateOutfitFromCreature(Creature* creature, CreatureOutfit& outfit);
    int32 GetPlayerVisibleSlotDisplayId(Player const* player, EquipmentSlots slot) const;
    bool PopulateEquipmentItemFromPlayer(Player const* player, EquipmentSlots visibleSlot, EquipmentItem& item) const;
    void PopulateEquipmentFromUnit(Unit const* unit, EquipmentInfo& equip) const;
    void PopulateCustomNpcOwner(CustomNpcData& npcData, Player* owner) const;
    uint32 AllocateNextOutfitId();
    uint32 AllocateNextCustomNpcTemplateId();
    bool LoadOutfitRowFromDb(uint32 outfitId);
    void InitializeDefaultCustomNpcTemplate(CreatureTemplate& creatureTemplate, std::string const& key, uint32 entry);
    bool PopulateDefaultOutfit(CreatureOutfit& co, Races race, Gender gender);
    void InitializeDefaultCustomNpcAddon(uint32 templateId);
    void ApplyCreatureAddonState(Creature* creature, CreatureAddon const& addon);
    void EnsureNpcModelExists(uint32 templateId, uint8 variationId);
    void EnsureNpcOutfitExists(uint32 templateId, uint8 variationId, float displayScale = 1.0f);
    void EnsureEquipmentInfoExists(uint32 templateId, uint8 variationId);
};

#define sRoleplay Roleplay::instance()

#endif
