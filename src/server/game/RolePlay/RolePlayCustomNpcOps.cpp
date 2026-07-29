#include "RolePlay.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Random.h"
#include "RaceMask.h"
#include "SmartEnum.h"
#include "TransmogMgr.h"
#include <sstream>

namespace
{
void CopyCreatureTemplateFields(CreatureTemplate const& src, CreatureTemplate& dst, uint32 newEntry, std::string const& displayName)
{
    dst.Entry = newEntry;
    for (uint8 i = 0; i < MAX_KILL_CREDIT; ++i)
        dst.KillCredit[i] = src.KillCredit[i];

    dst.Models.clear();
    dst.Name = displayName;
    dst.FemaleName = src.FemaleName;
    dst.SubName = src.SubName;
    dst.TitleAlt = src.TitleAlt;
    dst.IconName = src.IconName;
    dst.GossipMenuIds = src.GossipMenuIds;
    dst.difficultyStore = src.difficultyStore;
    dst.RequiredExpansion = src.RequiredExpansion;
    dst.VignetteID = src.VignetteID;
    dst.faction = src.faction;
    dst.npcflag = src.npcflag;
    dst.speed_walk = src.speed_walk;
    dst.speed_run = src.speed_run;
    dst.scale = src.scale;
    dst.Classification = src.Classification;
    dst.dmgschool = src.dmgschool;
    dst.BaseAttackTime = src.BaseAttackTime;
    dst.RangeAttackTime = src.RangeAttackTime;
    dst.BaseVariance = src.BaseVariance;
    dst.RangeVariance = src.RangeVariance;
    dst.unit_class = src.unit_class;
    dst.unit_flags = src.unit_flags;
    dst.unit_flags2 = src.unit_flags2;
    dst.unit_flags3 = src.unit_flags3;
    dst.family = src.family;
    dst.trainer_class = src.trainer_class;
    dst.type = src.type;
    memcpy(dst.resistance, src.resistance, sizeof(dst.resistance));
    memcpy(dst.spells, src.spells, sizeof(dst.spells));
    dst.VehicleId = src.VehicleId;
    dst.AIName = src.AIName;
    dst.MovementType = src.MovementType;
    dst.Movement = src.Movement;
    dst.ModExperience = src.ModExperience;
    dst.RacialLeader = src.RacialLeader;
    dst.movementId = src.movementId;
    dst.WidgetSetID = src.WidgetSetID;
    dst.WidgetSetUnitConditionID = src.WidgetSetUnitConditionID;
    dst.RegenHealth = src.RegenHealth;
    dst.CreatureImmunitiesId = src.CreatureImmunitiesId;
    dst.flags_extra = src.flags_extra;
    dst.ScriptID = src.ScriptID;
    dst.StringId = src.StringId;
}

bool MeetsCustomizationReqForOutfit(ChrCustomizationReqEntry const* req, CreatureOutfit const& outfit, bool checkRequiredDependentChoices)
{
    if (!req || !req->GetFlags().HasFlag(ChrCustomizationReqFlag::HasRequirements))
        return true;

    if (req->ClassMask && !(req->ClassMask & (1 << (outfit.GetClass() - 1))))
        return false;

    Races race = Races(outfit.GetRace());
    if (race != RACE_NONE && !req->RaceMask.IsEmpty() && req->RaceMask != RACEMASK_ALL_v<int32, 2> && !req->RaceMask.HasRace(race))
        return false;

    if (checkRequiredDependentChoices)
    {
        if (std::vector<std::pair<uint32, std::vector<uint32>>> const* requiredChoices = sDB2Manager.GetRequiredCustomizationChoices(req->ID))
        {
            for (auto const& [chrCustomizationOptionId, requiredChoicesForOption] : *requiredChoices)
            {
                bool hasRequiredChoiceForOption = false;
                for (uint32 requiredChoice : requiredChoicesForOption)
                {
                    auto choiceItr = std::find_if(outfit.Customizations.begin(), outfit.Customizations.end(), [requiredChoice](UF::ChrCustomizationChoice const& choice)
                    {
                        return choice.ChrCustomizationChoiceID == requiredChoice;
                    });

                    if (choiceItr != outfit.Customizations.end())
                    {
                        hasRequiredChoiceForOption = true;
                        break;
                    }
                }

                if (!hasRequiredChoiceForOption)
                    return false;
            }
        }
    }

    return true;
}

bool ValidateCreatureOutfitAppearance(CreatureOutfit const& outfit)
{
    if (outfit.Customizations.empty())
        return true;

    Races race = Races(outfit.GetRace());
    Gender gender = Gender(outfit.GetGender());

    std::vector<ChrCustomizationOptionEntry const*> options;
    if (ChrModelEntry const* model = sDB2Manager.GetChrModel(race, gender))
        if (std::vector<ChrCustomizationOptionEntry const*> const* modelOptions = sDB2Manager.GetCustomiztionOptions(model->ID))
            options = *modelOptions;

    if (options.empty())
    {
        if (std::vector<ChrCustomizationOptionEntry const*> const* raceOptions = sDB2Manager.GetCustomiztionOptions(race, gender))
            options = *raceOptions;
    }

    if (options.empty())
        return false;

    uint32 previousOption = 0;
    for (UF::ChrCustomizationChoice playerChoice : outfit.Customizations)
    {
        if (playerChoice.ChrCustomizationOptionID == previousOption)
            return false;

        previousOption = playerChoice.ChrCustomizationOptionID;

        auto customizationOptionDataItr = std::find_if(options.begin(), options.end(), [&](ChrCustomizationOptionEntry const* option)
        {
            return option->ID == playerChoice.ChrCustomizationOptionID;
        });

        if (customizationOptionDataItr == options.end())
            return false;

        if (!MeetsCustomizationReqForOutfit(sChrCustomizationReqStore.LookupEntry((*customizationOptionDataItr)->ChrCustomizationReqID), outfit, false))
            return false;

        std::vector<ChrCustomizationChoiceEntry const*> const* choicesForOption = sDB2Manager.GetCustomiztionChoices(playerChoice.ChrCustomizationOptionID);
        if (!choicesForOption)
            return false;

        auto customizationChoiceDataItr = std::find_if(choicesForOption->begin(), choicesForOption->end(), [&](ChrCustomizationChoiceEntry const* choice)
        {
            return choice->ID == playerChoice.ChrCustomizationChoiceID;
        });

        if (customizationChoiceDataItr == choicesForOption->end())
            return false;

        if (!MeetsCustomizationReqForOutfit(sChrCustomizationReqStore.LookupEntry((*customizationChoiceDataItr)->ChrCustomizationReqID), outfit, true))
            return false;
    }

    return true;
}
}

std::shared_ptr<CreatureOutfit> Roleplay::DuplicateOutfit(CreatureOutfit const& source, uint32 newOutfitId)
{
    std::shared_ptr<CreatureOutfit> co(new CreatureOutfit(source.GetRace(), Gender(source.GetGender())));
    co->id = newOutfitId;
    co->Class = source.Class;
    co->npcsoundsid = source.npcsoundsid;
    co->SpellVisualKitID = source.SpellVisualKitID;
    co->guild = source.guild;
    co->Customizations = source.Customizations;
    memcpy(co->outfitdisplays, source.outfitdisplays, sizeof(co->outfitdisplays));
    return co;
}

bool Roleplay::ValidateCreatureOutfit(CreatureOutfit const& outfit, std::string& error) const
{
    if (!CreatureOutfit::IsFake(outfit.GetId()))
    {
        error = Trinity::StringFormat("outfit entry {} is not a fake outfit id", outfit.GetId());
        return false;
    }

    if (!sChrRacesStore.LookupEntry(outfit.GetRace()))
    {
        error = Trinity::StringFormat("outfit {} has invalid race {}", outfit.GetId(), uint32(outfit.GetRace()));
        return false;
    }

    if (!sChrClassesStore.LookupEntry(outfit.GetClass()))
    {
        error = Trinity::StringFormat("outfit {} has invalid class {}", outfit.GetId(), uint32(outfit.GetClass()));
        return false;
    }

    if (outfit.GetGender() != GENDER_MALE && outfit.GetGender() != GENDER_FEMALE)
    {
        error = Trinity::StringFormat("outfit {} has invalid gender {}", outfit.GetId(), uint32(outfit.GetGender()));
        return false;
    }

    if (!sDB2Manager.GetChrModel(outfit.GetRace(), GENDER_MALE) || !sDB2Manager.GetChrModel(outfit.GetRace(), GENDER_FEMALE))
    {
        error = Trinity::StringFormat("outfit {} has no chr model for race {}", outfit.GetId(), uint32(outfit.GetRace()));
        return false;
    }

    if (!ValidateCreatureOutfitAppearance(outfit))
    {
        error = Trinity::StringFormat("outfit {} has invalid customizations for race/gender/class", outfit.GetId());
        return false;
    }

    return true;
}

bool Roleplay::LoadCreatureOutfitFromFields(Field* fields, std::shared_ptr<CreatureOutfit>& out, std::string& error) const
{
    uint32 i = 0;
    uint32 entry = fields[i++].GetUInt32();

    if (!CreatureOutfit::IsFake(entry))
    {
        error = Trinity::StringFormat("outfit entry {} is too low (entry <= {})", entry, CreatureOutfit::max_real_modelid);
        return false;
    }

    std::shared_ptr<CreatureOutfit> co(new CreatureOutfit());

    co->id = entry;
    co->npcsoundsid = fields[i++].GetUInt32();
    if (co->npcsoundsid && !sNPCSoundsStore.HasRecord(co->npcsoundsid))
        co->npcsoundsid = 0;

    co->race = fields[i++].GetUInt8();
    if (!sChrRacesStore.LookupEntry(co->race))
    {
        error = Trinity::StringFormat("outfit {} has incorrect race {}", entry, uint32(co->race));
        return false;
    }

    co->Class = fields[i++].GetUInt8();
    if (!sChrClassesStore.LookupEntry(co->Class))
    {
        error = Trinity::StringFormat("outfit {} has incorrect class {}", entry, uint32(co->Class));
        return false;
    }

    auto* maleModel = sDB2Manager.GetChrModel(co->race, GENDER_MALE);
    auto* femaleModel = sDB2Manager.GetChrModel(co->race, GENDER_FEMALE);
    if (!maleModel || !femaleModel)
    {
        error = Trinity::StringFormat("outfit {} has no chr model for race {}", entry, uint32(co->race));
        return false;
    }

    co->gender = fields[i++].GetUInt8();
    switch (co->gender)
    {
    case GENDER_FEMALE: co->displayId = femaleModel->DisplayID; break;
    case GENDER_MALE:   co->displayId = maleModel->DisplayID; break;
    default:
        error = Trinity::StringFormat("outfit {} has invalid gender {}", entry, uint32(co->gender));
        return false;
    }

    co->SpellVisualKitID = fields[i++].GetInt32();

    std::istringstream customizations_iss(fields[i++].GetString());
    UF::ChrCustomizationChoice customization;
    while ((customizations_iss >> customization.ChrCustomizationOptionID) && (customizations_iss >> customization.ChrCustomizationChoiceID))
        co->Customizations.push_back(customization);

    if (!ValidateCreatureOutfit(*co, error))
        return false;

    for (EquipmentSlots slot : CreatureOutfit::item_slots)
    {
        int64 displayInfo = fields[i++].GetInt64();
        uint32 appearancemodid = fields[i++].GetUInt32();
        if (displayInfo > 0)
        {
            uint32 item_entry = static_cast<uint32>(displayInfo);
            uint32 display = 0;

            if (ItemModifiedAppearanceEntry const* modifiedAppearance = TransmogMgr::GetItemModifiedAppearance(item_entry, appearancemodid))
                if (ItemAppearanceEntry const* itemAppearance = sItemAppearanceStore.LookupEntry(modifiedAppearance->ItemAppearanceID))
                    display = itemAppearance->ItemDisplayInfoID;

            co->outfitdisplays[slot] = display;
        }
        else
            co->outfitdisplays[slot] = static_cast<uint32>(-displayInfo);
    }

    co->guild = fields[i++].GetUInt64();
    out = std::move(co);
    return true;
}

void Roleplay::LoadCreatureTemplateFromFields(Field* fields, CreatureTemplate& creatureTemplate) const
{
    uint32 entry = fields[0].GetUInt32();
    creatureTemplate.Entry = entry;

    for (uint8 i = 0; i < MAX_KILL_CREDIT; ++i)
        creatureTemplate.KillCredit[i] = fields[1 + i].GetUInt32();

    creatureTemplate.Name = fields[3].GetString();
    creatureTemplate.FemaleName = fields[4].GetString();
    creatureTemplate.SubName = fields[5].GetString();
    creatureTemplate.TitleAlt = fields[6].GetString();
    creatureTemplate.IconName = fields[7].GetString();
    creatureTemplate.RequiredExpansion = fields[8].GetUInt32();
    creatureTemplate.VignetteID = fields[9].GetUInt32();
    creatureTemplate.faction = fields[10].GetUInt16();
    creatureTemplate.npcflag = fields[11].GetUInt64();
    creatureTemplate.speed_walk = fields[12].GetFloat();
    creatureTemplate.speed_run = fields[13].GetFloat();
    creatureTemplate.scale = fields[14].GetFloat();
    creatureTemplate.Classification = CreatureClassifications(fields[15].GetUInt8());
    creatureTemplate.dmgschool = uint32(fields[16].GetInt8());
    creatureTemplate.BaseAttackTime = fields[17].GetUInt32();
    creatureTemplate.RangeAttackTime = fields[18].GetUInt32();
    creatureTemplate.BaseVariance = fields[19].GetFloat();
    creatureTemplate.RangeVariance = fields[20].GetFloat();
    creatureTemplate.unit_class = uint32(fields[21].GetUInt8());
    creatureTemplate.unit_flags = fields[22].GetUInt32();
    creatureTemplate.unit_flags2 = fields[23].GetUInt32();
    creatureTemplate.unit_flags3 = fields[24].GetUInt32();
    creatureTemplate.family = CreatureFamily(fields[25].GetInt32());
    creatureTemplate.trainer_class = uint32(fields[26].GetUInt8());
    creatureTemplate.type = uint32(fields[27].GetUInt8());

    for (uint8 i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        creatureTemplate.resistance[i] = 0;

    for (uint8 i = 0; i < MAX_CREATURE_SPELLS; ++i)
        creatureTemplate.spells[i] = 0;

    creatureTemplate.VehicleId = fields[28].GetUInt32();
    creatureTemplate.AIName = fields[29].GetString();
    creatureTemplate.MovementType = uint32(fields[30].GetUInt8());

    if (!fields[31].IsNull())
        creatureTemplate.Movement.HoverInitiallyEnabled = fields[31].GetBool();

    if (Optional<uint8> chaseMovementType = fields[32].GetUInt8OrNull())
        creatureTemplate.Movement.Chase = static_cast<CreatureChaseMovementType>(*chaseMovementType);

    if (Optional<uint8> randomMovementType = fields[33].GetUInt8OrNull())
        creatureTemplate.Movement.Random = static_cast<CreatureRandomMovementType>(*randomMovementType);

    if (Optional<uint32> interactionPauseTimer = fields[34].GetUInt32OrNull())
        creatureTemplate.Movement.InteractionPauseTimer = *interactionPauseTimer;

    creatureTemplate.ModExperience = fields[35].GetFloat();
    creatureTemplate.RacialLeader = fields[36].GetBool();
    creatureTemplate.movementId = fields[37].GetUInt32();
    creatureTemplate.WidgetSetID = fields[38].GetInt32();
    creatureTemplate.WidgetSetUnitConditionID = fields[39].GetInt32();
    creatureTemplate.RegenHealth = fields[40].GetBool();
    creatureTemplate.CreatureImmunitiesId = fields[41].GetInt32();
    creatureTemplate.flags_extra = fields[42].GetUInt32();
    creatureTemplate.ScriptID = sObjectMgr->GetScriptId(fields[43].GetStringView());
    creatureTemplate.StringId = fields[44].GetString();
}

std::string const* Roleplay::GetCustomNpcKeyForEntry(uint32 templateId) const
{
    for (auto const& pair : _customNpcStore)
    {
        if (pair.second.templateId == templateId)
            return &pair.first;
    }
    return nullptr;
}

Optional<uint8> Roleplay::ResolveCustomNpcSpawnVariation(CreatureTemplate const* creatureTemplate, CreatureData const* data) const
{
    if (!creatureTemplate || !IsCustomNpcEntry(creatureTemplate->Entry))
        return {};

    uint32 const modelCount = creatureTemplate->Models.size();
    if (!modelCount)
        return {};

    if (data)
    {
        if (data->equipmentId > 0 && uint32(data->equipmentId) <= modelCount)
            return uint8(data->equipmentId);

        if (data->display)
        {
            for (uint32 i = 0; i < modelCount; ++i)
            {
                if (creatureTemplate->Models[i].CreatureDisplayID == data->display->CreatureDisplayID)
                    return uint8(i + 1);
            }
        }
    }

    if (modelCount > 1)
        return uint8(urand(1, modelCount));

    return 1;
}

void Roleplay::ApplyCustomNpcSpawnAppearance(Creature* creature, uint32 templateId, uint8 variation)
{
    ApplyCustomNpcStateToCreature(creature, templateId, variation);
}

CustomNpcEntryInitResult Roleplay::TryInitCustomNpcEntry(Creature* creature, uint32 entry, CreatureTemplate const* creatureInfo, CreatureData const* data)
{
    CustomNpcEntryInitResult result;

    Optional<uint8> customNpcVariation = ResolveCustomNpcSpawnVariation(creatureInfo, data);
    if (!customNpcVariation)
        return result;

    CreatureModel const* chosenModel = creatureInfo->GetModelByIdx(*customNpcVariation - 1);
    if (!chosenModel)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) custom NPC variation {} is invalid.", entry, uint32(*customNpcVariation));
        result.status = CustomNpcEntryInitResult::Status::Failed;
        return result;
    }

    CreatureModel model = *chosenModel;
    CreatureModelInfo const* minfo = sObjectMgr->GetCreatureModelRandomGender(&model, creatureInfo);
    if (!minfo && !CreatureOutfit::IsFake(model.CreatureDisplayID))
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid model {} defined in table `creature_template_model`, can't load.", entry, model.CreatureDisplayID);
        result.status = CustomNpcEntryInitResult::Status::Failed;
        return result;
    }

    ApplyCustomNpcSpawnAppearance(creature, entry, *customNpcVariation);
    result.status = CustomNpcEntryInitResult::Status::Success;
    result.variation = *customNpcVariation;
    return result;
}

std::vector<Creature*> Roleplay::GetLiveCustomNpcCreatures(std::string const& key) const
{
    std::vector<Creature*> result;
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
        return result;

    uint32 entry = itr->second.templateId;
    sMapMgr->DoForAllMaps([&](Map* map)
    {
        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (creature && creature->GetEntry() == entry)
                result.push_back(creature);
        }
    });

    return result;
}

void Roleplay::RebuildCustomNpcSpawnsFromObjectMgr(std::string const& key)
{
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
        return;

    std::vector<ObjectGuid::LowType> spawns;
    spawns.reserve(itr->second.spawns.size());

    for (Creature* creature : GetLiveCustomNpcCreatures(key))
    {
        if (!creature)
            continue;

        ObjectGuid::LowType spawnId = creature->GetSpawnId();
        if (!spawnId)
            continue;

        if (std::find(spawns.begin(), spawns.end(), spawnId) == spawns.end())
            spawns.push_back(spawnId);
    }

    itr->second.spawns = std::move(spawns);
}

void Roleplay::ApplyCustomNpcStateToCreature(Creature* creature, uint32 templateId, uint8 variation)
{
    if (!creature)
        return;

    CreatureTemplate const& cTemplate = sObjectMgr->_creatureTemplateStore[templateId];
    if (cTemplate.Models.empty())
        return;

    if (variation < 1 || variation > cTemplate.Models.size())
        variation = 1;

    CreatureModel const& model = cTemplate.Models[variation - 1];
    CreatureAddon const& cAddon = sObjectMgr->_creatureTemplateAddonStore[templateId];

    creature->SetName(cTemplate.Name);

    if (CreatureOutfit::IsFake(model.CreatureDisplayID))
    {
        if (std::shared_ptr<CreatureOutfit> const& outfit = sObjectMgr->GetOutfit(model.CreatureDisplayID))
            creature->SetOutfit(outfit);
        else
            creature->SetDisplayId(model.CreatureDisplayID);
    }
    else
    {
        creature->SetDisplayId(model.CreatureDisplayID);
        creature->SetObjectScale(model.DisplayScale);
    }

    creature->LoadEquipment(variation);

    creature->RemoveAllAuras();
    for (uint32 auraId : cAddon.auras)
        creature->AddAura(auraId, creature);

    ApplyCreatureAddonState(creature, cAddon);
}

void Roleplay::ReloadSpawnedCustomNpcs(std::string const& key, Optional<uint8> variationId)
{
    TC_LOG_DEBUG("roleplay", "ROLEPLAY: Reloading custom npc '{}'", key);
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        TC_LOG_WARN("roleplay", "ROLEPLAY: Reload custom NPC '{}' failed: key not found.", key);
        return;
    }

    CustomNpcData const& data = itr->second;
    CreatureTemplate const& cTemplate = sObjectMgr->_creatureTemplateStore[data.templateId];
    if (cTemplate.Models.empty())
    {
        TC_LOG_ERROR("roleplay", "ROLEPLAY: Reload custom NPC '{}' failed: template {} has no models.", key, data.templateId);
        return;
    }

    std::vector<Creature*> creatures = GetLiveCustomNpcCreatures(key);
    TC_LOG_DEBUG("roleplay", "ROLEPLAY: Found {} live creature(s) for custom NPC '{}' (entry {}).", creatures.size(), key, data.templateId);

    for (Creature* creature : creatures)
    {
        if (!creature)
            continue;

        uint8 variation = 1;
        if (variationId)
            variation = *variationId;
        else if (Optional<uint8> resolvedVariation = ResolveCustomNpcSpawnVariation(&cTemplate, creature->GetCreatureData()))
            variation = *resolvedVariation;

        if (variation < 1 || variation > cTemplate.Models.size())
            variation = 1;

        ApplyCustomNpcStateToCreature(creature, data.templateId, variation);
        TC_LOG_DEBUG("roleplay", "ROLEPLAY: Reloaded creature spawn {} for custom NPC '{}'.", creature->GetSpawnId(), key);
    }

    RebuildCustomNpcSpawnsFromObjectMgr(key);
}

bool Roleplay::RefreshCustomNpcCreatures(std::string const& key, Optional<uint8> variationId)
{
    if (!CustomNpcNameExists(key))
        return false;

    ReloadSpawnedCustomNpcs(key, variationId);
    return true;
}

bool Roleplay::TryLoadCustomNpcBundleFromDb(uint32 templateId, CustomNpcReloadBundle& out, std::string& error) const
{
    out = {};
    out.creatureTemplate.Entry = templateId;

    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_CREATURE_TEMPLATE);
    stmt->setUInt32(0, templateId);
    stmt->setUInt32(1, 0);
    PreparedQueryResult templateResult = WorldDatabase.Query(stmt);
    if (!templateResult)
    {
        error = Trinity::StringFormat("creature_template {} not found in database", templateId);
        return false;
    }

    LoadCreatureTemplateFromFields(templateResult->Fetch(), out.creatureTemplate);

    if (QueryResult modelResult = WorldDatabase.PQuery(
        "SELECT CreatureDisplayID, DisplayScale, Probability FROM creature_template_model WHERE CreatureID = {} ORDER BY Idx ASC", templateId))
    {
        do
        {
            Field* fields = modelResult->Fetch();
            out.models.emplace_back(fields[0].GetUInt32(), fields[1].GetFloat(), fields[2].GetFloat());
        } while (modelResult->NextRow());
    }

    if (out.models.empty())
    {
        error = Trinity::StringFormat("creature_template_model for entry {} has no rows", templateId);
        return false;
    }

    for (CreatureModel const& model : out.models)
    {
        if (!CreatureOutfit::IsFake(model.CreatureDisplayID))
            continue;

        QueryResult outfitResult = WorldDatabase.PQuery(
            "SELECT entry, npcsoundsid, race, class, gender, spellvisualkitid, customizations, "
            "head, head_appearance, shoulders, shoulders_appearance, body, body_appearance, chest, chest_appearance, waist, waist_appearance, "
            "legs, legs_appearance, feet, feet_appearance, wrists, wrists_appearance, hands, hands_appearance, tabard, tabard_appearance, back, back_appearance, "
            "guildid FROM creature_template_outfits WHERE entry = {}", model.CreatureDisplayID);

        if (!outfitResult)
        {
            error = Trinity::StringFormat("creature_template_outfits entry {} missing for model variation", model.CreatureDisplayID);
            return false;
        }

        std::shared_ptr<CreatureOutfit> outfit;
        if (!LoadCreatureOutfitFromFields(outfitResult->Fetch(), outfit, error))
            return false;

        out.outfits[model.CreatureDisplayID] = std::move(outfit);
    }

    if (QueryResult equipResult = WorldDatabase.PQuery(
        "SELECT ID, ItemID1, AppearanceModID1, ItemVisual1, ItemID2, AppearanceModID2, ItemVisual2, ItemID3, AppearanceModID3, ItemVisual3 "
        "FROM creature_equip_template WHERE CreatureID = {}", templateId))
    {
        do
        {
            Field* fields = equipResult->Fetch();
            uint8 variationId = fields[0].GetUInt8();
            if (!variationId)
                continue;

            EquipmentInfo& equipmentInfo = out.equipment[variationId];
            for (uint8 i = 0; i < MAX_EQUIPMENT_ITEMS; ++i)
            {
                equipmentInfo.Items[i].ItemId = fields[1 + i * 3].GetUInt32();
                equipmentInfo.Items[i].AppearanceModId = fields[2 + i * 3].GetUInt16();
                equipmentInfo.Items[i].ItemVisual = fields[3 + i * 3].GetUInt16();
            }
        } while (equipResult->NextRow());
    }

    if (QueryResult addonResult = WorldDatabase.PQuery(
        "SELECT entry, PathId, mount, StandState, AnimTier, VisFlags, SheathState, PvPFlags, emote, aiAnimKit, movementAnimKit, meleeAnimKit, visibilityDistanceType, auras "
        "FROM creature_template_addon WHERE entry = {}", templateId))
    {
        Field* fields = addonResult->Fetch();
        CreatureAddon addon;
        addon.PathId = fields[1].GetUInt32();
        addon.mount = fields[2].GetUInt32();
        addon.standState = fields[3].GetUInt8();
        addon.animTier = fields[4].GetUInt8();
        addon.visFlags = fields[5].GetUInt8();
        addon.sheathState = fields[6].GetUInt8();
        addon.pvpFlags = fields[7].GetUInt8();
        addon.emote = fields[8].GetUInt32();
        addon.aiAnimKit = fields[9].GetUInt16();
        addon.movementAnimKit = fields[10].GetUInt16();
        addon.meleeAnimKit = fields[11].GetUInt16();
        addon.visibilityDistanceType = VisibilityDistanceType(fields[12].GetUInt8());

        for (std::string_view aura : Trinity::Tokenize(fields[13].GetStringView(), ' ', false))
        {
            if (Optional<uint32> spellId = Trinity::StringTo<uint32>(aura))
                addon.auras.push_back(*spellId);
        }

        out.addon = addon;
    }

    return true;
}

bool Roleplay::CommitCustomNpcBundle(uint32 templateId, CustomNpcReloadBundle&& bundle)
{
    CreatureTemplate& existing = sObjectMgr->_creatureTemplateStore[templateId];

    std::vector<uint32> gossipMenuIds = std::move(existing.GossipMenuIds);
    std::unordered_map<Difficulty, CreatureDifficulty> difficultyStore = std::move(existing.difficultyStore);
    uint32 resistance[MAX_SPELL_SCHOOL];
    memcpy(resistance, existing.resistance, sizeof(resistance));
    uint32 spells[MAX_CREATURE_SPELLS];
    memcpy(spells, existing.spells, sizeof(spells));

    existing = std::move(bundle.creatureTemplate);
    existing.Models = std::move(bundle.models);
    existing.GossipMenuIds = std::move(gossipMenuIds);
    existing.difficultyStore = std::move(difficultyStore);
    memcpy(existing.resistance, resistance, sizeof(existing.resistance));
    memcpy(existing.spells, spells, sizeof(existing.spells));
    existing.QueryData.reset();

    for (auto& pair : bundle.outfits)
        sObjectMgr->_creatureOutfitStore[pair.first] = std::move(pair.second);

    sObjectMgr->_equipmentInfoStore[templateId] = std::move(bundle.equipment);

    if (bundle.addon)
        sObjectMgr->_creatureTemplateAddonStore[templateId] = *bundle.addon;

    return true;
}

bool Roleplay::ReloadCustomNpcFromDb(std::string const& key)
{
    if (!CustomNpcNameExists(key))
    {
        TC_LOG_ERROR("roleplay", "ROLEPLAY: Reload failed: custom NPC '{}' not found.", key);
        return false;
    }

    CustomNpcData data = _customNpcStore[key];
    uint32 templateId = data.templateId;
    std::vector<ObjectGuid::LowType> spawns = data.spawns;

    CustomNpcReloadBundle bundle;
    std::string error;
    if (!TryLoadCustomNpcBundleFromDb(templateId, bundle, error))
    {
        TC_LOG_ERROR("roleplay", "ROLEPLAY: Reload failed for custom NPC '{}' (entry={}): {}. Runtime left untouched.",
            key, templateId, error);
        return false;
    }

    if (!CommitCustomNpcBundle(templateId, std::move(bundle)))
    {
        TC_LOG_ERROR("roleplay", "ROLEPLAY: Reload failed for custom NPC '{}' (entry={}): commit failed.", key, templateId);
        return false;
    }

    ReloadCreatureLocaleFromDb(templateId);
    RefreshCreatureTemplateClientCache(sObjectMgr->_creatureTemplateStore[templateId]);

    _customNpcStore[key].spawns = spawns;
    ReloadSpawnedCustomNpcs(key);

    TC_LOG_INFO("roleplay", "ROLEPLAY: Reloaded custom NPC '{}' (entry={}, live refresh by entry).", key, templateId);
    return true;
}

CustomNpcReloadSummary Roleplay::ReloadAllCustomNpcsFromDb()
{
    CustomNpcReloadSummary summary;
    std::vector<std::string> keys;
    keys.reserve(_customNpcStore.size());
    for (auto const& pair : _customNpcStore)
        keys.push_back(pair.first);

    for (std::string const& key : keys)
    {
        if (ReloadCustomNpcFromDb(key))
            ++summary.successCount;
        else
            ++summary.failedCount;
    }

    return summary;
}

bool Roleplay::DiagnoseCustomNpc(std::string const& key, std::ostringstream& report) const
{
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        report << "Custom NPC key '" << key << "' not found.";
        return false;
    }

    CustomNpcData const& data = itr->second;
    CreatureTemplate const* cTemplate = sObjectMgr->GetCreatureTemplate(data.templateId);
    report << "Custom NPC: " << key << "\n";
    report << "Entry: " << data.templateId << "\n";
    report << "Owner: " << GetCustomNpcOwnerDisplay(key) << "\n";
    report << "Tracked spawns: " << data.spawns.size() << "\n";
    report << "Live creatures by entry: " << GetLiveCustomNpcCreatures(key).size() << "\n";

    if (!cTemplate)
    {
        report << "Template: MISSING in runtime\n";
        return true;
    }

    report << "Name: " << cTemplate->Name << "\n";
    report << "Model variations: " << cTemplate->Models.size() << "\n";
    for (uint8 i = 0; i < cTemplate->Models.size(); ++i)
    {
        CreatureModel const& model = cTemplate->Models[i];
        report << "  #" << uint32(i + 1) << ": displayId=" << model.CreatureDisplayID << " scale=" << model.DisplayScale;
        if (CreatureOutfit::IsFake(model.CreatureDisplayID))
        {
            if (std::shared_ptr<CreatureOutfit> const& outfit = sObjectMgr->GetOutfit(model.CreatureDisplayID))
                report << " outfit race=" << uint32(outfit->GetRace()) << " gender=" << uint32(outfit->GetGender())
                    << " customizations=" << outfit->Customizations.size();
            else
                report << " outfit=MISSING";
        }
        report << "\n";
    }

    auto equipItr = sObjectMgr->_equipmentInfoStore.find(data.templateId);
    if (equipItr != sObjectMgr->_equipmentInfoStore.end())
        report << "Equipment variations: " << equipItr->second.size() << "\n";
    else
        report << "Equipment variations: 0\n";

    if (sObjectMgr->_creatureTemplateAddonStore.find(data.templateId) != sObjectMgr->_creatureTemplateAddonStore.end())
        report << "Addon: present\n";
    else
        report << "Addon: missing\n";

    CustomNpcReloadBundle bundle;
    std::string error;
    if (TryLoadCustomNpcBundleFromDb(data.templateId, bundle, error))
        report << "DB bundle: valid\n";
    else
        report << "DB bundle: INVALID - " << error << "\n";

    return true;
}

std::vector<CustomNpcRaceDescriptor> Roleplay::BuildCustomNpcRaceList(bool availableOnly) const
{
    std::vector<CustomNpcRaceDescriptor> result;
    uint32 availableIndex = 0;

    for (size_t i = 0, raceCount = EnumUtils::Count<Races>(); i < raceCount; ++i)
    {
        Races race = EnumUtils::FromIndex<Races>(i);
        CustomNpcRaceDescriptor descriptor;
        descriptor.race = race;
        descriptor.name = EnumUtils::ToString(race).Title;

        if (!sChrRacesStore.LookupEntry(race))
            descriptor.available = false;
        else
        {
            ChrModelEntry const* maleModel = sDB2Manager.GetChrModel(race, GENDER_MALE);
            ChrModelEntry const* femaleModel = sDB2Manager.GetChrModel(race, GENDER_FEMALE);
            descriptor.available = maleModel && femaleModel;
            if (maleModel)
            {
                descriptor.maleChrModelId = maleModel->ID;
                descriptor.maleDisplayId = maleModel->DisplayID;
            }
            if (femaleModel)
            {
                descriptor.femaleChrModelId = femaleModel->ID;
                descriptor.femaleDisplayId = femaleModel->DisplayID;
            }
        }

        if (availableOnly && !descriptor.available)
            continue;

        descriptor.listIndex = ++availableIndex;
        result.push_back(std::move(descriptor));
    }

    return result;
}

std::string Roleplay::GetCustomizationOptionLabel(ChrCustomizationOptionEntry const* option, LocaleConstant locale) const
{
    if (!option)
        return "Unknown";

    if (option->Name[locale] && *option->Name[locale])
        return option->Name[locale];

    if (option->Name[DEFAULT_LOCALE] && *option->Name[DEFAULT_LOCALE])
        return option->Name[DEFAULT_LOCALE];

    return Trinity::StringFormat("Option %u", option->ID);
}

std::string Roleplay::GetCustomizationChoiceLabel(ChrCustomizationChoiceEntry const* choice, LocaleConstant locale) const
{
    if (!choice)
        return "Unknown";

    if (choice->Name[locale] && *choice->Name[locale])
        return choice->Name[locale];

    if (choice->Name[DEFAULT_LOCALE] && *choice->Name[DEFAULT_LOCALE])
        return choice->Name[DEFAULT_LOCALE];

    return Trinity::StringFormat("Choice %u", choice->ID);
}

bool Roleplay::MeetsCustomizationReqForOutfit(ChrCustomizationReqEntry const* req, CreatureOutfit const& outfit, bool checkRequiredDependentChoices) const
{
    return ::MeetsCustomizationReqForOutfit(req, outfit, checkRequiredDependentChoices);
}

std::vector<ChrCustomizationOptionEntry const*> Roleplay::BuildCustomizationOptionList(Races race, Gender gender) const
{
    std::vector<ChrCustomizationOptionEntry const*> result;
    if (ChrModelEntry const* model = sDB2Manager.GetChrModel(race, gender))
        if (auto const* options = sDB2Manager.GetCustomiztionOptions(model->ID))
            result = *options;

    if (result.empty())
    {
        if (auto const* options = sDB2Manager.GetCustomiztionOptions(race, gender))
            result = *options;
    }

    std::sort(result.begin(), result.end(), [](auto* a, auto* b)
    {
        return std::tie(a->SortIndex, a->UiOrderIndex, a->ID) < std::tie(b->SortIndex, b->UiOrderIndex, b->ID);
    });

    result.erase(std::unique(result.begin(), result.end(), [](auto* a, auto* b)
    {
        return a->ID == b->ID;
    }), result.end());

    return result;
}

std::vector<ChrCustomizationChoiceEntry const*> Roleplay::BuildCustomizationChoiceList(uint32 optionId) const
{
    std::vector<ChrCustomizationChoiceEntry const*> result;
    if (auto const* choices = sDB2Manager.GetCustomiztionChoices(optionId))
        result = *choices;

    std::sort(result.begin(), result.end(), [](auto* a, auto* b)
    {
        return std::tie(a->UiOrderIndex, a->SortOrder, a->ID) < std::tie(b->UiOrderIndex, b->SortOrder, b->ID);
    });

    return result;
}

std::vector<ChrCustomizationChoiceEntry const*> Roleplay::BuildCustomizationChoiceListForOutfit(uint32 optionId, CreatureOutfit const& outfit) const
{
    std::vector<ChrCustomizationChoiceEntry const*> result;
    for (ChrCustomizationChoiceEntry const* choice : BuildCustomizationChoiceList(optionId))
    {
        if (!MeetsCustomizationReqForOutfit(sChrCustomizationReqStore.LookupEntry(choice->ChrCustomizationReqID), outfit, true))
            continue;

        result.push_back(choice);
    }

    return result;
}

uint32 Roleplay::FindCustomizationChoiceIndex(CreatureOutfit const& outfit, ChrCustomizationOptionEntry const* option) const
{
    if (!option)
        return 0;

    uint32 currentChoiceId = 0;
    for (UF::ChrCustomizationChoice const& customization : outfit.Customizations)
    {
        if (customization.ChrCustomizationOptionID == option->ID)
        {
            currentChoiceId = customization.ChrCustomizationChoiceID;
            break;
        }
    }

    if (!currentChoiceId)
        return 0;

    auto choices = BuildCustomizationChoiceListForOutfit(option->ID, outfit);
    for (uint32 i = 0; i < choices.size(); ++i)
    {
        if (choices[i]->ID == currentChoiceId)
            return i + 1;
    }

    return 0;
}

CreatureOutfit const* Roleplay::GetCustomNpcOutfit(std::string const& key, uint8 variation) const
{
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
        return nullptr;

    CreatureTemplate const& cTemplate = sObjectMgr->_creatureTemplateStore[itr->second.templateId];
    if (variation < 1 || variation > cTemplate.Models.size())
        return nullptr;

    uint32 outfitId = cTemplate.Models[variation - 1].CreatureDisplayID;
    if (!CreatureOutfit::IsFake(outfitId))
        return nullptr;

    if (std::shared_ptr<CreatureOutfit> const& outfit = sObjectMgr->GetOutfit(outfitId))
        return outfit.get();

    return nullptr;
}

CreatureOutfit* Roleplay::GetMutableCustomNpcOutfit(std::string const& key, uint8 variation, std::string& error)
{
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        error = "custom NPC key not found";
        return nullptr;
    }

    uint32 templateId = itr->second.templateId;
    EnsureNpcOutfitExists(templateId, variation);
    CreatureTemplate& cTemplate = sObjectMgr->_creatureTemplateStore[templateId];
    if (variation < 1 || variation > cTemplate.Models.size())
    {
        error = "variation is out of range";
        return nullptr;
    }

    uint32 outfitId = cTemplate.Models[variation - 1].CreatureDisplayID;
    std::shared_ptr<CreatureOutfit> co = sObjectMgr->GetOutfit(outfitId);
    if (!co)
    {
        error = "outfit not found in runtime";
        return nullptr;
    }

    return co.get();
}

bool Roleplay::TryGetCustomNpcOutfitCopy(std::string const& key, uint8 variation, CreatureOutfit& out, std::string& error)
{
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        error = "custom NPC key not found";
        return false;
    }

    uint32 templateId = itr->second.templateId;
    EnsureNpcOutfitExists(templateId, variation);

    CreatureOutfit const* existing = GetCustomNpcOutfit(key, variation);
    if (!existing)
    {
        error = "outfit not found";
        return false;
    }

    std::shared_ptr<CreatureOutfit> copy = DuplicateOutfit(*existing, existing->GetId());
    out = *copy;
    return true;
}

void Roleplay::ReplaceCustomization(std::vector<UF::ChrCustomizationChoice>& customizations, uint32 optionId, uint32 choiceId)
{
    for (UF::ChrCustomizationChoice& customization : customizations)
    {
        if (customization.ChrCustomizationOptionID == optionId)
        {
            customization.ChrCustomizationChoiceID = choiceId;
            return;
        }
    }

    UF::ChrCustomizationChoice customization;
    customization.ChrCustomizationOptionID = optionId;
    customization.ChrCustomizationChoiceID = choiceId;
    customizations.push_back(customization);
}

void Roleplay::SortCustomizationsByOptions(std::vector<UF::ChrCustomizationChoice>& customizations, std::vector<ChrCustomizationOptionEntry const*> const& options)
{
    std::vector<UF::ChrCustomizationChoice> sorted;
    sorted.reserve(customizations.size());

    for (ChrCustomizationOptionEntry const* option : options)
    {
        for (UF::ChrCustomizationChoice const& customization : customizations)
        {
            if (customization.ChrCustomizationOptionID == option->ID)
            {
                sorted.push_back(customization);
                break;
            }
        }
    }

    for (UF::ChrCustomizationChoice const& customization : customizations)
    {
        if (std::none_of(sorted.begin(), sorted.end(), [&](UF::ChrCustomizationChoice const& existing)
        {
            return existing.ChrCustomizationOptionID == customization.ChrCustomizationOptionID;
        }))
            sorted.push_back(customization);
    }

    customizations = std::move(sorted);
}

void Roleplay::NormalizeOutfitCustomizations(CreatureOutfit& outfit)
{
    auto options = BuildCustomizationOptionList(Races(outfit.race), Gender(outfit.gender));

    bool changed = true;
    for (uint8 pass = 0; pass < 8 && changed; ++pass)
    {
        changed = false;

        size_t oldSize = outfit.Customizations.size();
        outfit.Customizations.erase(std::remove_if(outfit.Customizations.begin(), outfit.Customizations.end(),
            [&](UF::ChrCustomizationChoice const& customization)
            {
                return std::none_of(options.begin(), options.end(), [&](ChrCustomizationOptionEntry const* option)
                {
                    return option->ID == customization.ChrCustomizationOptionID;
                });
            }), outfit.Customizations.end());
        if (outfit.Customizations.size() != oldSize)
            changed = true;

        for (auto itr = outfit.Customizations.begin(); itr != outfit.Customizations.end(); )
        {
            auto choices = BuildCustomizationChoiceListForOutfit(itr->ChrCustomizationOptionID, outfit);
            bool valid = std::any_of(choices.begin(), choices.end(), [&](ChrCustomizationChoiceEntry const* choice)
            {
                return choice->ID == itr->ChrCustomizationChoiceID;
            });

            if (!valid)
            {
                itr = outfit.Customizations.erase(itr);
                changed = true;
            }
            else
                ++itr;
        }
    }

    SortCustomizationsByOptions(outfit.Customizations, options);

    if (!outfit.Customizations.empty() && !ValidateCreatureOutfitAppearance(outfit))
        outfit.Customizations.clear();
}

bool Roleplay::ValidateAndSaveCustomNpcOutfit(std::string const& key, uint8 variation, CreatureOutfit& outfit, std::string& error)
{
    std::string validationError;
    if (!ValidateCreatureOutfit(outfit, validationError))
    {
        error = validationError;
        return false;
    }

    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        error = "custom NPC key not found";
        return false;
    }

    uint32 templateId = itr->second.templateId;
    CreatureTemplate& cTemplate = sObjectMgr->_creatureTemplateStore[templateId];
    if (variation < 1 || variation > cTemplate.Models.size())
    {
        error = "variation is out of range";
        return false;
    }

    uint32 outfitId = cTemplate.Models[variation - 1].CreatureDisplayID;
    std::shared_ptr<CreatureOutfit> stored = DuplicateOutfit(outfit, outfitId);
    sObjectMgr->_creatureOutfitStore[outfitId] = std::move(stored);

    SaveNpcOutfitToDb(templateId, variation);
    ReloadSpawnedCustomNpcs(key, variation);
    return true;
}

bool Roleplay::SetCustomNpcCustomizationByIndex(std::string const& key, uint8 variation, uint32 optionIndex, uint32 choiceIndex, std::string& error)
{
    CreatureOutfit workingCopy;
    if (!TryGetCustomNpcOutfitCopy(key, variation, workingCopy, error))
        return false;

    auto options = BuildCustomizationOptionList(Races(workingCopy.race), Gender(workingCopy.gender));
    if (optionIndex < 1 || optionIndex > options.size())
    {
        error = "option index is out of range";
        return false;
    }

    uint32 optionId = options[optionIndex - 1]->ID;
    auto choices = BuildCustomizationChoiceListForOutfit(optionId, workingCopy);
    if (choiceIndex < 1 || choiceIndex > choices.size())
    {
        error = "choice index is out of range";
        return false;
    }

    ReplaceCustomization(workingCopy.Customizations, optionId, choices[choiceIndex - 1]->ID);
    SortCustomizationsByOptions(workingCopy.Customizations, options);
    return ValidateAndSaveCustomNpcOutfit(key, variation, workingCopy, error);
}

bool Roleplay::SetCustomNpcFaceFromSelf(std::string const& key, uint8 variation, Player* player, std::string& error)
{
    if (!player)
    {
        error = "player is null";
        return false;
    }

    CreatureOutfit workingCopy;
    if (!TryGetCustomNpcOutfitCopy(key, variation, workingCopy, error))
        return false;

    workingCopy.Customizations.clear();
    for (UF::ChrCustomizationChoice const& customization : player->m_playerData->Customizations)
        workingCopy.Customizations.push_back(customization);

    auto options = BuildCustomizationOptionList(Races(workingCopy.race), Gender(workingCopy.gender));
    NormalizeOutfitCustomizations(workingCopy);
    SortCustomizationsByOptions(workingCopy.Customizations, options);
    return ValidateAndSaveCustomNpcOutfit(key, variation, workingCopy, error);
}

bool Roleplay::CopyCustomNpcCustomizationOptionByIndex(std::string const& key, uint8 variation, uint32 optionIndex, Player* player, std::string& error)
{
    if (!player)
    {
        error = "player is null";
        return false;
    }

    CreatureOutfit const* outfit = GetCustomNpcOutfit(key, variation);
    if (!outfit)
    {
        error = "outfit not found";
        return false;
    }

    auto options = BuildCustomizationOptionList(Races(outfit->race), Gender(outfit->gender));
    if (optionIndex < 1 || optionIndex > options.size())
    {
        error = "option index is out of range";
        return false;
    }

    uint32 optionId = options[optionIndex - 1]->ID;
    for (UF::ChrCustomizationChoice const& customization : player->m_playerData->Customizations)
    {
        if (customization.ChrCustomizationOptionID != optionId)
            continue;

        auto choices = BuildCustomizationChoiceListForOutfit(optionId, *outfit);
        for (uint32 i = 0; i < choices.size(); ++i)
        {
            if (choices[i]->ID == customization.ChrCustomizationChoiceID)
                return SetCustomNpcCustomizationByIndex(key, variation, optionIndex, i + 1, error);
        }
        break;
    }

    error = "player has no value for this option";
    return false;
}

bool Roleplay::AddCustomNpcModelVariationBlank(std::string const& key, Optional<uint8> sourceVariation, uint8& newVariation, std::string& error)
{
    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        error = "custom NPC key not found";
        return false;
    }

    uint32 templateId = itr->second.templateId;
    CreatureTemplate& cTemplate = sObjectMgr->_creatureTemplateStore[templateId];

    Races race = RACE_HUMAN;
    Gender gender = GENDER_MALE;
    if (sourceVariation && *sourceVariation >= 1 && *sourceVariation <= cTemplate.Models.size())
    {
        if (CreatureOutfit const* sourceOutfit = GetCustomNpcOutfit(key, *sourceVariation))
        {
            race = Races(sourceOutfit->race);
            gender = Gender(sourceOutfit->gender);
        }
    }
    else if (!cTemplate.Models.empty())
    {
        if (CreatureOutfit const* sourceOutfit = GetCustomNpcOutfit(key, 1))
        {
            race = Races(sourceOutfit->race);
            gender = Gender(sourceOutfit->gender);
        }
    }

    std::shared_ptr<CreatureOutfit> co(new CreatureOutfit());
    uint32 outfitId = AllocateNextOutfitId();
    co->id = outfitId;
    if (!PopulateDefaultOutfit(*co, race, gender))
    {
        error = "failed to populate default outfit";
        return false;
    }

    sObjectMgr->_creatureOutfitStore[outfitId] = std::move(co);
    cTemplate.Models.push_back(CreatureModel(outfitId, 1.0f, 1.0f));
    newVariation = uint8(cTemplate.Models.size());

    SaveNpcOutfitToDb(templateId, newVariation);
    SaveNpcModelInfo(cTemplate.Models.back(), templateId, newVariation);
    EnsureEquipmentInfoExists(templateId, newVariation);
    SaveNpcEquipmentInfoToDb(templateId, newVariation);
    ReloadSpawnedCustomNpcs(key, newVariation);
    return true;
}

bool Roleplay::AddCustomNpcModelVariationFromPlayer(std::string const& key, Player* source, uint8& newVariation, std::string& error)
{
    if (!source)
    {
        error = "player is null";
        return false;
    }

    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        error = "custom NPC key not found";
        return false;
    }

    std::shared_ptr<CreatureOutfit> co(new CreatureOutfit());
    uint32 outfitId = AllocateNextOutfitId();
    co->id = outfitId;
    if (!PopulateOutfitFromPlayer(source, *co))
    {
        error = "failed to populate outfit from player";
        return false;
    }

    sObjectMgr->_creatureOutfitStore[outfitId] = std::move(co);
    uint32 templateId = itr->second.templateId;
    CreatureTemplate& cTemplate = sObjectMgr->_creatureTemplateStore[templateId];
    cTemplate.Models.push_back(CreatureModel(outfitId, source->GetNativeDisplayScale(), 1.0f));
    newVariation = uint8(cTemplate.Models.size());

    EquipmentInfo equipmentInfo;
    PopulateEquipmentFromUnit(source, equipmentInfo);
    sObjectMgr->_equipmentInfoStore[templateId][newVariation] = equipmentInfo;

    SaveNpcOutfitToDb(templateId, newVariation);
    SaveNpcModelInfo(cTemplate.Models.back(), templateId, newVariation);
    SaveNpcEquipmentInfoToDb(templateId, newVariation);
    ReloadSpawnedCustomNpcs(key, newVariation);
    return true;
}

bool Roleplay::AddCustomNpcModelVariationFromCreature(std::string const& key, Creature* source, uint8& newVariation, std::string& error)
{
    if (!source)
    {
        error = "creature is null";
        return false;
    }

    auto itr = _customNpcStore.find(key);
    if (itr == _customNpcStore.end())
    {
        error = "custom NPC key not found";
        return false;
    }

    uint32 templateId = itr->second.templateId;
    CreatureTemplate& cTemplate = sObjectMgr->_creatureTemplateStore[templateId];

    if (std::shared_ptr<CreatureOutfit> const& sourceOutfit = source->GetOutfit())
    {
        uint32 outfitId = AllocateNextOutfitId();
        sObjectMgr->_creatureOutfitStore[outfitId] = DuplicateOutfit(*sourceOutfit, outfitId);
        cTemplate.Models.push_back(CreatureModel(outfitId, source->GetObjectScale(), 1.0f));
    }
    else
    {
        cTemplate.Models.push_back(CreatureModel(source->GetDisplayId(), source->GetObjectScale(), 1.0f));
    }

    newVariation = uint8(cTemplate.Models.size());
    EquipmentInfo equipmentInfo;
    PopulateEquipmentFromUnit(source, equipmentInfo);
    sObjectMgr->_equipmentInfoStore[templateId][newVariation] = equipmentInfo;

    if (CreatureOutfit::IsFake(cTemplate.Models.back().CreatureDisplayID))
        SaveNpcOutfitToDb(templateId, newVariation);
    SaveNpcModelInfo(cTemplate.Models.back(), templateId, newVariation);
    SaveNpcEquipmentInfoToDb(templateId, newVariation);
    ReloadSpawnedCustomNpcs(key, newVariation);
    return true;
}

bool Roleplay::AddCustomNpcModelVariationFromCustomNpc(std::string const& key, std::string const& sourceKey, Optional<uint8> sourceVariation, uint8& newVariation, std::string& error)
{
    if (!CustomNpcNameExists(sourceKey))
    {
        error = "source custom NPC not found";
        return false;
    }

    uint8 fromVariation = sourceVariation.value_or(1);
    return CopyCustomNpcModelVariation(key, fromVariation, {}, newVariation, error, sourceKey);
}

bool Roleplay::CopyCustomNpcModelVariation(std::string const& key, uint8 fromVariation, Optional<uint8> toVariation, uint8& actualVariation, std::string& error, std::string const& sourceKeyOverride)
{
    std::string const& sourceKey = sourceKeyOverride.empty() ? key : sourceKeyOverride;
    if (!CustomNpcNameExists(sourceKey) || !CustomNpcNameExists(key))
    {
        error = "custom NPC key not found";
        return false;
    }

    uint32 sourceTemplateId = _customNpcStore[sourceKey].templateId;
    uint32 targetTemplateId = _customNpcStore[key].templateId;
    CreatureTemplate const& sourceTemplate = sObjectMgr->_creatureTemplateStore[sourceTemplateId];
    CreatureTemplate& targetTemplate = sObjectMgr->_creatureTemplateStore[targetTemplateId];

    if (fromVariation < 1 || fromVariation > sourceTemplate.Models.size())
    {
        error = "source variation is out of range";
        return false;
    }

    CreatureModel const& sourceModel = sourceTemplate.Models[fromVariation - 1];
    uint8 targetVariation = toVariation.value_or(uint8(targetTemplate.Models.size() + 1));

    if (toVariation)
    {
        if (*toVariation < 1 || *toVariation > targetTemplate.Models.size())
        {
            error = "target variation is out of range";
            return false;
        }
        targetVariation = *toVariation;
    }
    else
    {
        targetVariation = uint8(targetTemplate.Models.size() + 1);
        targetTemplate.Models.push_back(sourceModel);
    }

    if (CreatureOutfit::IsFake(sourceModel.CreatureDisplayID))
    {
        if (std::shared_ptr<CreatureOutfit> const& sourceOutfit = sObjectMgr->GetOutfit(sourceModel.CreatureDisplayID))
        {
            uint32 outfitId = AllocateNextOutfitId();
            sObjectMgr->_creatureOutfitStore[outfitId] = DuplicateOutfit(*sourceOutfit, outfitId);
            targetTemplate.Models[targetVariation - 1] = CreatureModel(outfitId, sourceModel.DisplayScale, sourceModel.Probability);
        }
    }
    else
        targetTemplate.Models[targetVariation - 1] = sourceModel;

    auto sourceEquipItr = sObjectMgr->_equipmentInfoStore.find(sourceTemplateId);
    if (sourceEquipItr != sObjectMgr->_equipmentInfoStore.end())
    {
        auto equipItr = sourceEquipItr->second.find(fromVariation);
        if (equipItr != sourceEquipItr->second.end())
            sObjectMgr->_equipmentInfoStore[targetTemplateId][targetVariation] = equipItr->second;
    }

    actualVariation = targetVariation;
    SaveNpcOutfitToDb(targetTemplateId, actualVariation);
    SaveNpcModelInfo(targetTemplate.Models[actualVariation - 1], targetTemplateId, actualVariation);
    SaveNpcEquipmentInfoToDb(targetTemplateId, actualVariation);
    ReloadSpawnedCustomNpcs(key, actualVariation);
    return true;
}

bool Roleplay::ApplyCustomNpcModelVariationToCreature(std::string const& key, Creature* creature, uint8 variation, std::string& error)
{
    if (!creature)
    {
        error = "creature is null";
        return false;
    }

    if (!ApplyCustomNpcToCreature(key, creature, variation))
    {
        error = "failed to apply custom NPC";
        return false;
    }

    return true;
}

bool Roleplay::CloneCustomNpcFromKey(std::string const& sourceKey, std::string const& newKey, std::string const& displayName, Player* owner, Optional<uint8> sourceVariation, std::string& error)
{
    if (!CustomNpcNameExists(sourceKey))
    {
        error = "source custom NPC not found";
        return false;
    }

    if (CustomNpcNameExists(newKey))
    {
        error = "target key already exists";
        return false;
    }

    if (!owner)
    {
        error = "owner is null";
        return false;
    }

    uint32 sourceTemplateId = _customNpcStore[sourceKey].templateId;
    CreatureTemplate const& sourceTemplate = sObjectMgr->_creatureTemplateStore[sourceTemplateId];
    uint8 variation = sourceVariation.value_or(1);
    if (variation < 1 || variation > sourceTemplate.Models.size())
        variation = 1;

    uint32 newTemplateId = AllocateNextCustomNpcTemplateId();
    CreatureTemplate newTemplate;
    CopyCreatureTemplateFields(sourceTemplate, newTemplate, newTemplateId, displayName);

    std::unordered_map<uint32, uint32> outfitRemap;
    for (uint8 i = 0; i < sourceTemplate.Models.size(); ++i)
    {
        CreatureModel const& model = sourceTemplate.Models[i];
        CreatureModel newModel = model;
        if (CreatureOutfit::IsFake(model.CreatureDisplayID))
        {
            if (std::shared_ptr<CreatureOutfit> const& sourceOutfit = sObjectMgr->GetOutfit(model.CreatureDisplayID))
            {
                uint32 outfitId = AllocateNextOutfitId();
                sObjectMgr->_creatureOutfitStore[outfitId] = DuplicateOutfit(*sourceOutfit, outfitId);
                newModel.CreatureDisplayID = outfitId;
                outfitRemap[model.CreatureDisplayID] = outfitId;
            }
        }
        newTemplate.Models.push_back(newModel);
    }

    sObjectMgr->CheckCreatureTemplate(&newTemplate);
    sObjectMgr->_creatureTemplateStore[newTemplateId] = std::move(newTemplate);
    SaveNpcCreatureTemplateToDb(sObjectMgr->_creatureTemplateStore[newTemplateId]);

    for (uint8 i = 0; i < sObjectMgr->_creatureTemplateStore[newTemplateId].Models.size(); ++i)
    {
        uint8 var = i + 1;
        SaveNpcModelInfo(sObjectMgr->_creatureTemplateStore[newTemplateId].Models[i], newTemplateId, var);
        SaveNpcOutfitToDb(newTemplateId, var);
    }

    auto sourceEquipItr = sObjectMgr->_equipmentInfoStore.find(sourceTemplateId);
    if (sourceEquipItr != sObjectMgr->_equipmentInfoStore.end())
    {
        for (auto const& pair : sourceEquipItr->second)
        {
            sObjectMgr->_equipmentInfoStore[newTemplateId][pair.first] = pair.second;
            SaveNpcEquipmentInfoToDb(newTemplateId, pair.first);
        }
    }

    if (auto addonItr = sObjectMgr->_creatureTemplateAddonStore.find(sourceTemplateId); addonItr != sObjectMgr->_creatureTemplateAddonStore.end())
    {
        sObjectMgr->_creatureTemplateAddonStore[newTemplateId] = addonItr->second;
        SaveNpcCreatureTemplateAddonToDb(newTemplateId, addonItr->second);
    }
    else
        InitializeDefaultCustomNpcAddon(newTemplateId);

    CustomNpcData npcData;
    npcData.key = newKey;
    npcData.templateId = newTemplateId;
    PopulateCustomNpcOwner(npcData, owner);
    _customNpcStore[newKey] = npcData;
    SaveCustomNpcDataToDb(npcData);
    SaveCustomNpcOwnerToDb(npcData);

    TC_LOG_INFO("roleplay", "ROLEPLAY: Cloned custom NPC '{}' -> '{}' (entry {}).", sourceKey, newKey, newTemplateId);
    return true;
}

bool Roleplay::CloneCustomNpcFromEntry(uint32 entry, std::string const& key, std::string const& displayName, Player* owner, Optional<uint8> variation, std::string& error)
{
    CreatureTemplate const* cTemplate = sObjectMgr->GetCreatureTemplate(entry);
    if (!cTemplate)
    {
        error = "creature template not found";
        return false;
    }

    if (CustomNpcNameExists(key))
    {
        error = "target key already exists";
        return false;
    }

    if (!owner)
    {
        error = "owner is null";
        return false;
    }

    uint32 newTemplateId = AllocateNextCustomNpcTemplateId();
    CreatureTemplate newTemplate;
    CopyCreatureTemplateFields(*cTemplate, newTemplate, newTemplateId, displayName);

    uint8 sourceVariation = variation.value_or(1);
    if (sourceVariation < 1 || sourceVariation > cTemplate->Models.size())
        sourceVariation = 1;

    CreatureModel const& sourceModel = cTemplate->Models[sourceVariation - 1];
    CreatureModel newModel = sourceModel;
    if (CreatureOutfit::IsFake(sourceModel.CreatureDisplayID))
    {
        if (std::shared_ptr<CreatureOutfit> const& sourceOutfit = sObjectMgr->GetOutfit(sourceModel.CreatureDisplayID))
        {
            uint32 outfitId = AllocateNextOutfitId();
            sObjectMgr->_creatureOutfitStore[outfitId] = DuplicateOutfit(*sourceOutfit, outfitId);
            newModel.CreatureDisplayID = outfitId;
        }
    }
    newTemplate.Models.push_back(newModel);

    sObjectMgr->CheckCreatureTemplate(&newTemplate);
    sObjectMgr->_creatureTemplateStore[newTemplateId] = std::move(newTemplate);
    SaveNpcCreatureTemplateToDb(sObjectMgr->_creatureTemplateStore[newTemplateId]);
    SaveNpcModelInfo(sObjectMgr->_creatureTemplateStore[newTemplateId].Models[0], newTemplateId, 1);
    SaveNpcOutfitToDb(newTemplateId, 1);

    auto sourceEquipItr = sObjectMgr->_equipmentInfoStore.find(entry);
    if (sourceEquipItr != sObjectMgr->_equipmentInfoStore.end())
    {
        auto equipItr = sourceEquipItr->second.find(sourceVariation);
        if (equipItr != sourceEquipItr->second.end())
        {
            sObjectMgr->_equipmentInfoStore[newTemplateId][1] = equipItr->second;
            SaveNpcEquipmentInfoToDb(newTemplateId, 1);
        }
    }

    if (auto addonItr = sObjectMgr->_creatureTemplateAddonStore.find(entry); addonItr != sObjectMgr->_creatureTemplateAddonStore.end())
    {
        sObjectMgr->_creatureTemplateAddonStore[newTemplateId] = addonItr->second;
        SaveNpcCreatureTemplateAddonToDb(newTemplateId, addonItr->second);
    }
    else
        InitializeDefaultCustomNpcAddon(newTemplateId);

    CustomNpcData npcData;
    npcData.key = key;
    npcData.templateId = newTemplateId;
    PopulateCustomNpcOwner(npcData, owner);
    _customNpcStore[key] = npcData;
    SaveCustomNpcDataToDb(npcData);
    SaveCustomNpcOwnerToDb(npcData);
    return true;
}

bool Roleplay::CopyCustomNpcEquipmentFromCustomNpc(std::string const& key, std::string const& sourceKey, uint8 variation, Optional<uint8> sourceEquipmentVariation, std::string& error)
{
    if (!CustomNpcNameExists(key) || !CustomNpcNameExists(sourceKey))
    {
        error = "custom NPC key not found";
        return false;
    }

    uint32 sourceTemplateId = _customNpcStore[sourceKey].templateId;
    uint32 targetTemplateId = _customNpcStore[key].templateId;
    uint8 sourceVariation = sourceEquipmentVariation.value_or(variation);

    auto sourceEquipItr = sObjectMgr->_equipmentInfoStore.find(sourceTemplateId);
    if (sourceEquipItr == sObjectMgr->_equipmentInfoStore.end())
    {
        error = "source has no equipment";
        return false;
    }

    auto equipItr = sourceEquipItr->second.find(sourceVariation);
    if (equipItr == sourceEquipItr->second.end())
    {
        error = "source equipment variation not found";
        return false;
    }

    sObjectMgr->_equipmentInfoStore[targetTemplateId][variation] = equipItr->second;
    SaveNpcEquipmentInfoToDb(targetTemplateId, variation);
    ReloadSpawnedCustomNpcs(key, variation);
    return true;
}
