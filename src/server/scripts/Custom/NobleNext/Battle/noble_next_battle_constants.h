/*
 * NobleNext Battle — aura IDs and indexed effect tables (legacy EBS).
 */

#pragma once

#include "Define.h"
#include <cstddef>
#include <optional>
#include <string_view>

namespace RoleplayCore::NobleNext::Battle
{
    constexpr uint32 AURA_TURN        = 88037;
    constexpr uint32 AURA_HP          = 900100;
    constexpr uint32 AURA_WOUND       = 900102;
    constexpr uint32 AURA_WEAK        = 88012;
    constexpr uint32 AURA_DEATH       = 88053;
    constexpr uint32 AURA_ARMOR       = 900103;
    constexpr uint32 AURA_ENERGY      = 900101;
    constexpr uint32 AURA_FOCUS       = 900104;
    constexpr uint32 AURA_PHYS_DEF    = 102050;
    constexpr uint32 AURA_MAG_DEF     = 102051;

    struct AuraEntry
    {
        uint32 SpellId;
        std::string_view Name;
    };

    inline constexpr AuraEntry Auras[] =
    {
        { 88061, "Усталость" },
        { 88062, "Контроль" },
        { 88063, "Усилиение" },
        { 88064, "Периодический урон" },
    };

    inline constexpr AuraEntry AuraStats[] =
    {
        { 95001, "Физическая сила" },
        { 95002, "Мастерство" },
        { 95003, "Учёность" },
        { 95004, "Мудрость" },
        { 95005, "Атака" },
        { 95006, "Защита" },
        { 95007, "Дальний бой" },
        { 95008, "Магия" },
        { 95010, "Живучесть" },
        { 95011, "Мана" },
        { 95018, "Мощь" },
        { 95023, "Незаметность" },
        { 95048, "Удача" },
    };

    inline constexpr AuraEntry AuraDebuffs[] =
    {
        { 95019, "Холод" },
        { 95020, "Жар" },
        { 95024, "Сон" },
        { 95025, "Оглушение" },
        { 95026, "Обездвиживание" },
        { 95027, "Замешательство" },
        { 95031, "Безумие" },
        { 95033, "Разрушение брони" },
        { 95034, "Уязвимость к магии" },
        { 95035, "Уязвимость к природе" },
        { 95036, "Уязвимость ко Тьме" },
        { 95037, "Уязвимость к Скверне" },
        { 95038, "Уязвимость к огню" },
        { 95039, "Уязвимость к ветру" },
        { 95040, "Уязвимость к земле" },
        { 95041, "Уязвимость к воде" },
        { 95042, "Уязвимость ко льду" },
        { 95043, "Уязвимость к крови" },
        { 95044, "Уязвимость к молнии" },
        { 95047, "Буян" },
        { 95054, "Антимагия" },
    };

    inline constexpr AuraEntry AuraBuffs[] =
    {
        { 95012, "Усиление (скорость)" },
        { 95013, "Усиление (атака)" },
        { 95014, "Усиление (защита)" },
        { 95015, "Усиление (дальний бой)" },
        { 95016, "Усиление (точность)" },
        { 95028, "Абсолютная неуязвимость" },
        { 95029, "Неуязвимость (магия)" },
        { 95030, "Неуязвимость (физич.)" },
        { 95032, "Исступление" },
    };

    inline constexpr AuraEntry AuraHarm[] =
    {
        { 95021, "Лёгкая рана" },
        { 95022, "Тяжёлая рана" },
        { 95049, "Перелом руки" },
        { 95050, "Перелом ноги" },
        { 95051, "Перелом челюсти" },
        { 95052, "Кровопотеря" },
        { 95053, "Немота" },
    };

    inline constexpr AuraEntry AuraActions[] =
    {
        { 95017, "Действие" },
        { 95045, "Нейтрализация" },
        { 95046, "Провокация" },
        { 95055, "Подкрепление" },
        { 95056, "Метка охотника" },
    };

    inline std::optional<AuraEntry const*> ResolveIndexed(AuraEntry const* table, size_t count, uint32 luaIndex)
    {
        if (!table || luaIndex == 0 || luaIndex > count)
            return std::nullopt;
        return &table[luaIndex - 1];
    }
}
