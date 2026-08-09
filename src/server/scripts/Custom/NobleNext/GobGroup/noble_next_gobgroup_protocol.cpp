/*
 * NobleNext — NN_GOBGROUP addon protocol (self-whisper, LANG_ADDON).
 */

#include "noble_next_gobgroup_protocol.h"
#include "noble_next_gobgroup_mgr.h"
#include "../GobBlueprint/noble_next_gobblueprint_mgr.h"

#include "ChatPackets.h"
#include "Player.h"
#include "RBAC.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "Util.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace RoleplayCore::NobleNext
{
namespace GobGroupProtocol
{
namespace
{
    struct CapEntry
    {
        char const* Path;
        uint32 RbacId;
    };

    // GobGroup leaf IDs 3008–3030 — effective gate is RBAC only (no staff gate).
    CapEntry const kGobGroupCaps[] =
    {
        { "gobject.group",              rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP },
        { "gobject.group.capabilities", rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP },
        { "gobject.group.help",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_HELP },
        { "gobject.group.create",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CREATE },
        { "gobject.group.use",          rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_USE },
        { "gobject.group.add",          rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_ADD },
        { "gobject.group.scan",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_SCAN },
        { "gobject.group.addnear",      rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_ADDNEAR },
        { "gobject.group.remove",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_REMOVE },
        { "gobject.group.dissolve",     rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_DISSOLVE },
        { "gobject.group.delete",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_DELETE },
        { "gobject.group.info",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_INFO },
        { "gobject.group.list",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_LIST },
        { "gobject.group.near",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_LIST },
        { "gobject.group.target",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_TARGET },
        { "gobject.group.check",        rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CHECK },
        { "gobject.group.status",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_STATUS },
        { "gobject.group.capture",      rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CAPTURE },
        { "gobject.group.recalc",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_RECALC },
        { "gobject.group.sync",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_SYNC },
        { "gobject.group.move",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_MOVE },
        { "gobject.group.turn",         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_TURN },
        { "gobject.group.nudge",        rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_MOVE },
        { "gobject.group.rotate",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_TURN },
        { "gobject.group.scale",        rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_MOVE },
        { "gobject.group.relocate",     rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_RELOCATE },
        { "gobject.group.reload",       rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_RELOAD },
        { "gobject.group.cleanup",      rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CLEANUP },
        { "gobject.blueprint",          rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT },
        { "gobject.blueprint.help",     rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_HELP },
        { "gobject.blueprint.list",     rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_LIST },
        { "gobject.blueprint.info",     rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_INFO },
        { "gobject.blueprint.new",      rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_NEW },
        { "gobject.blueprint.update",   rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_UPDATE },
        { "gobject.blueprint.spawn",    rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_SPAWN },
        { "gobject.blueprint.delete",   rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_DELETE },
        { "gobject.blueprint.rename",   rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_RENAME },
        { "gobject.blueprint.set-public", rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_SET_PUBLIC },
        { "gobject.blueprint.member",   rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_MEMBER },
        { "gobject.blueprint.staff",    rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_STAFF },
    };

    std::string FormatCoord(float value)
    {
        if (!std::isfinite(value))
            return "0";
        return Trinity::StringFormat("{:.4f}", value);
    }

    std::string FormatCompactCoord(float value)
    {
        if (!std::isfinite(value))
            return "0";
        return Trinity::StringFormat("{:.6g}", value);
    }

    std::vector<std::string> SplitNearName(std::string name)
    {
        // A DB name can be 100 UTF-8 characters. Keep each physical addon
        // message safely below MAX_PAYLOAD while retaining the complete name.
        constexpr size_t NAME_CHUNK_CHARACTERS = 16;
        std::vector<std::string> chunks;
        while (!name.empty())
        {
            std::string chunk = name;
            utf8truncate(chunk, NAME_CHUNK_CHARACTERS);
            if (chunk.empty())
            {
                // Names loaded from utf8mb4 should never reach this fallback,
                // but guarantee progress if malformed legacy bytes exist.
                chunk.assign(name, 0, std::min(name.size(), NAME_CHUNK_CHARACTERS));
            }
            name.erase(0, chunk.size());
            chunks.push_back(std::move(chunk));
        }
        if (chunks.empty())
            chunks.emplace_back();
        return chunks;
    }

    bool IsAllowed(Player* player, CapEntry const& entry)
    {
        if (!player || !player->GetSession())
            return false;
        return player->GetSession()->HasPermission(entry.RbacId);
    }

    void AppendCapToken(std::vector<std::string>& tokens, CapEntry const& entry, Player* player)
    {
        tokens.emplace_back(Trinity::StringFormat("{}:{}",
            entry.Path, IsAllowed(player, entry) ? 1 : 0));
    }

    void SendChunkedCaps(Player* player, std::vector<std::string> const& tokens)
    {
        if (tokens.empty())
        {
            SendRaw(player, "CAPS|1|1|");
            return;
        }

        std::vector<std::string> chunks;
        std::string current;
        for (std::string const& token : tokens)
        {
            size_t const needed = (current.empty() ? 0 : 1) + token.size();
            // CAPS|<i>|<n>| + payload; reserve ~20 for header
            if (!current.empty() && current.size() + needed > MAX_PAYLOAD - 20)
            {
                chunks.push_back(std::move(current));
                current.clear();
            }
            if (!current.empty())
                current.push_back('|');
            current += token;
        }
        if (!current.empty())
            chunks.push_back(std::move(current));

        uint32 const count = uint32(chunks.size());
        for (uint32 i = 0; i < count; ++i)
            SendRaw(player, Trinity::StringFormat("CAPS|{}|{}|{}", i + 1, count, chunks[i]));
    }
}

std::string EscapeField(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
            case '\\': out += "\\\\"; break;
            case '|':  out += "\\|"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:   out.push_back(ch); break;
        }
    }
    return out;
}

void SendRaw(Player* player, std::string const& body)
{
    if (!player)
        return;

    WorldPackets::Chat::Chat packet;
    packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, body, 0, "", DEFAULT_LOCALE, ADDON_PREFIX);
    player->SendDirectMessage(packet.Write());
}

void SendResult(Player* player, std::string_view verb, std::string_view status, std::string_view detail)
{
    SendRaw(player, Trinity::StringFormat("RESULT|{}|{}|{}",
        EscapeField(verb), EscapeField(status), EscapeField(detail)));
}

void SendCapabilities(Player* player)
{
    if (!player)
        return;

    std::vector<std::string> tokens;
    tokens.reserve(std::size(kGobGroupCaps));
    for (CapEntry const& entry : kGobGroupCaps)
        AppendCapToken(tokens, entry, player);

    SendChunkedCaps(player, tokens);
}

void SendInfo(Player* player, GobGroupInfoSnapshot const& snap)
{
    if (!player)
        return;

    SendRaw(player, Trinity::StringFormat("INFO_BEGIN|{}|{}|{}|{}|{}|{}",
        snap.ObjectGuid,
        snap.GroupRoot,
        EscapeField(snap.Name),
        snap.MemberCount,
        snap.Dirty ? 1 : 0,
        snap.Busy ? 1 : 0));

    SendRaw(player, Trinity::StringFormat("INFO_OBJECT|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
        snap.Object.Guid,
        snap.Object.Entry,
        snap.Object.MapId,
        FormatCoord(snap.Object.X),
        FormatCoord(snap.Object.Y),
        FormatCoord(snap.Object.Z),
        FormatCoord(snap.Object.O),
        snap.Object.InGroup ? 1 : 0,
        snap.Object.IsRoot ? 1 : 0,
        snap.Object.LogicalPhaseId));

    for (GobGroupObjectSnapshot const& member : snap.Members)
    {
        SendRaw(player, Trinity::StringFormat("INFO_MEMBER|{}|{}|{}|{}|{}|{}|{}|{}",
            member.Guid,
            member.Entry,
            member.MapId,
            FormatCoord(member.X),
            FormatCoord(member.Y),
            FormatCoord(member.Z),
            FormatCoord(member.O),
            member.LogicalPhaseId));
    }

    SendRaw(player, Trinity::StringFormat("INFO_END|{}", snap.ObjectGuid));
}

void SendList(Player* player, GobGroupListSnapshot const& snap)
{
    if (!player)
        return;

    std::string const filter = snap.MapFilter
        ? std::to_string(*snap.MapFilter)
        : std::string("all");

    SendRaw(player, Trinity::StringFormat("LIST_BEGIN|{}|{}", filter, snap.Items.size()));
    for (GobGroupListItemSnapshot const& item : snap.Items)
    {
        SendRaw(player, Trinity::StringFormat("LIST_ITEM|{}|{}|{}|{}|{}",
            item.RootGuid,
            EscapeField(item.Name),
            item.MemberCount,
            item.MapId,
            item.LogicalPhaseId));
    }
    SendRaw(player, "LIST_END");
}

void SendNear(Player* player, GobGroupNearSnapshot const& snap)
{
    if (!player)
        return;

    SendRaw(player, Trinity::StringFormat("NEAR_BEGIN|{}|{}|{}",
        snap.MapId, FormatCompactCoord(snap.Radius), snap.Items.size()));

    for (GobGroupNearItemSnapshot const& item : snap.Items)
    {
        std::vector<std::string> const nameChunks = SplitNearName(item.Name);
        uint32 const chunkCount = uint32(nameChunks.size());
        for (uint32 i = 0; i < chunkCount; ++i)
        {
            SendRaw(player, Trinity::StringFormat("NEAR_ITEM|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                item.RootGuid,
                EscapeField(nameChunks[i]),
                item.MemberCount,
                item.MapId,
                FormatCompactCoord(item.Distance),
                FormatCompactCoord(item.X),
                FormatCompactCoord(item.Y),
                FormatCompactCoord(item.Z),
                i + 1,
                chunkCount,
                item.LogicalPhaseId));
        }
    }

    SendRaw(player, Trinity::StringFormat("NEAR_END|{}", snap.Items.size()));
}

void SendStatus(Player* player, GobGroupStatusSnapshot const& snap)
{
    if (!player)
        return;

    SendRaw(player, Trinity::StringFormat("STATUS|{}|{}|{}|{}",
        snap.GroupGuid,
        snap.Dirty ? 1 : 0,
        snap.Busy ? 1 : 0,
        EscapeField(snap.JobState)));
}

void SendBlueprintList(Player* player, std::vector<GobBlueprintListItem> const& items, std::string_view filter)
{
    if (!player)
        return;

    // BEGIN|filter|count
    // ROW|key|id|owner|name|members|public|canMutate|description
    SendRaw(player, Trinity::StringFormat("BLUEPRINT_LIST_BEGIN|{}|{}",
        EscapeField(filter), items.size()));
    for (GobBlueprintListItem const& item : items)
    {
        SendRaw(player, Trinity::StringFormat("BLUEPRINT_LIST_ROW|{}|{}|{}|{}|{}|{}|{}|{}",
            EscapeField(item.Key()),
            item.Id,
            item.OwnerAccountId,
            EscapeField(item.Name),
            item.MemberCount,
            item.IsPublic ? 1 : 0,
            item.CanMutate ? 1 : 0,
            EscapeField(item.Description)));
    }
    SendRaw(player, "BLUEPRINT_LIST_END");
}

void SendBlueprintInfo(Player* player, GobBlueprintRecord const& record)
{
    if (!player)
        return;

    // INFO|key|id|owner|name|physMembers|public|canMutate|canSetPublic|virtualRoot|description
    SendRaw(player, Trinity::StringFormat("BLUEPRINT_INFO|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
        EscapeField(record.Key()),
        record.Id,
        record.OwnerAccountId,
        EscapeField(record.Name),
        record.PhysicalMemberCount(),
        record.IsPublic ? 1 : 0,
        record.CanMutate ? 1 : 0,
        record.CanSetPublic ? 1 : 0,
        record.HasVirtualRoot() ? 1 : 0,
        EscapeField(record.Description)));

    for (GobBlueprintPart const& part : record.Parts)
    {
        char const* typeName = "base";
        if (part.Type == GobBlueprintPartType::Object)
            typeName = "object";
        else if (part.Type == GobBlueprintPartType::Group)
            typeName = "group";
        SendRaw(player, Trinity::StringFormat("BLUEPRINT_INFO_PART|{}|{}|{}|{}",
            part.Id, typeName, EscapeField(part.Label), part.SourceRootGuid));
    }

    // MEMBER|id|partId|sort|isRoot|entry
    for (GobBlueprintMember const& member : record.Members)
    {
        SendRaw(player, Trinity::StringFormat("BLUEPRINT_INFO_MEMBER|{}|{}|{}|{}|{}",
            member.Id, member.PartId, member.SortOrder, member.IsRoot ? 1 : 0, member.Entry));
    }
    SendRaw(player, Trinity::StringFormat("BLUEPRINT_INFO_END|{}", EscapeField(record.Key())));
}

void SendBlueprintResult(Player* player, std::string_view verb, std::string_view status, std::string_view detail)
{
    SendRaw(player, Trinity::StringFormat("BLUEPRINT_RESULT|{}|{}|{}",
        EscapeField(verb), EscapeField(status), EscapeField(detail)));
}

void SendBlueprintStatus(Player* player, std::string_view statusLine)
{
    SendRaw(player, Trinity::StringFormat("BLUEPRINT_STATUS|{}", EscapeField(statusLine)));
}

} // namespace GobGroupProtocol
} // namespace RoleplayCore::NobleNext
