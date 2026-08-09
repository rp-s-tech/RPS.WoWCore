/*
 * NobleNext — NN_RPPHASE protocol and phase-scoped addon KV transport.
 */

#include "noble_next_rpphase_protocol.h"

#include "AccountMgr.h"
#include "Base64.h"
#include "CharacterCache.h"
#include "ChatPackets.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "RBAC.h"
#include "RoleplayDatabase.h"
#include "RoleplayPhaseMgr.h"
#include "SharedDefines.h"
#include "StringConvert.h"
#include "Timer.h"
#include "Util.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RoleplayCore::NobleNext::RoleplayPhaseProtocol
{
namespace
{
    constexpr size_t MaxNamespaceLength = 24;
    constexpr size_t MaxKeyLength = 63;
    constexpr size_t MaxValueSize = 64 * 1024;
    constexpr size_t MaxPhaseQuota = 256 * 1024;
    constexpr uint32 UploadTimeoutMs = 30 * IN_MILLISECONDS;
    constexpr uint32 MaxUploadChunks = 512;
    constexpr size_t MaxPendingUploadsPerPlayer = 4;
    constexpr size_t MaxPendingUploadsGlobal = 128;

    enum class NamespaceAccess : uint8
    {
        Invalid,
        Editor,
        Manager
    };

    struct Upload
    {
        uint64 OwnerCharacterGuid = 0;
        uint64 PhaseId = 0;
        std::string NameSpace;
        std::string Key;
        uint64 ExpectedVersion = 0;
        uint32 ChunkCount = 0;
        uint32 NextChunk = 1;
        std::string Checksum;
        std::string EncodedValue;
        std::chrono::steady_clock::time_point ExpiresAt;
    };

    std::unordered_map<std::string, Upload> Uploads;
    std::mutex UploadMutex;
    std::mutex MutationMutex;

    bool IsAsciiIdentifier(std::string_view value, size_t maxLength)
    {
        if (value.empty() || value.size() > maxLength)
            return false;

        return std::all_of(value.begin(), value.end(), [](unsigned char ch)
        {
            return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
        });
    }

    bool IsRequestId(std::string_view value)
    {
        return IsAsciiIdentifier(value, 32);
    }

    bool ParseUInt64(std::string_view value, uint64& result)
    {
        if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch); }))
            return false;

        uint64 parsed = 0;
        auto const [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} || end != value.data() + value.size())
            return false;

        result = parsed;
        return true;
    }

    bool ParseUInt32(std::string_view value, uint32& result)
    {
        uint64 parsed = 0;
        if (!ParseUInt64(value, parsed) || parsed > std::numeric_limits<uint32>::max())
            return false;

        result = uint32(parsed);
        return true;
    }

    uint32 Fnv1a(std::string_view value)
    {
        uint32 hash = 2166136261u;
        for (unsigned char ch : value)
        {
            hash ^= ch;
            hash *= 16777619u;
        }
        return hash;
    }

    std::string Checksum(std::string_view value)
    {
        static constexpr char Hex[] = "0123456789abcdef";
        uint32 const valueHash = Fnv1a(value);
        std::string out(8, '0');
        for (uint32 i = 0; i < 8; ++i)
            out[7 - i] = Hex[(valueHash >> (i * 4)) & 0x0F];
        return out;
    }

    bool IsChecksum(std::string_view value)
    {
        return value.size() == 8 && std::all_of(value.begin(), value.end(), [](unsigned char ch)
        {
            return std::isxdigit(ch);
        });
    }

    std::string MakeUploadKey(Player const* player, std::string_view requestId)
    {
        return std::to_string(player ? player->GetGUID().GetCounter() : 0) + "|" + std::string(requestId);
    }

    Optional<std::string> UnescapeField(std::string_view value)
    {
        std::string out;
        out.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i)
        {
            char const ch = value[i];
            if (ch != '\\')
            {
                out.push_back(ch);
                continue;
            }

            if (++i == value.size())
                return {};

            switch (value[i])
            {
                case '\\': out.push_back('\\'); break;
                case '|': out.push_back('|'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'x':
                {
                    if (i + 2 >= value.size())
                        return {};

                    auto HexValue = [](char hex) -> int
                    {
                        if (hex >= '0' && hex <= '9')
                            return hex - '0';
                        if (hex >= 'a' && hex <= 'f')
                            return hex - 'a' + 10;
                        if (hex >= 'A' && hex <= 'F')
                            return hex - 'A' + 10;
                        return -1;
                    };

                    int const high = HexValue(value[++i]);
                    int const low = HexValue(value[++i]);
                    if (high < 0 || low < 0)
                        return {};
                    out.push_back(char((high << 4) | low));
                    break;
                }
                default:
                    return {};
            }
        }
        return out;
    }

    Optional<std::vector<std::string>> SplitRequest(std::string_view request)
    {
        std::vector<std::string> fields;
        std::string current;
        bool escaped = false;

        for (char ch : request)
        {
            if (ch == '|' && !escaped)
            {
                Optional<std::string> decoded = UnescapeField(current);
                if (!decoded)
                    return {};
                fields.push_back(std::move(*decoded));
                current.clear();
                continue;
            }

            current.push_back(ch);
            if (ch == '\\' && !escaped)
                escaped = true;
            else
                escaped = false;
        }

        Optional<std::string> decoded = UnescapeField(current);
        if (!decoded)
            return {};
        fields.push_back(std::move(*decoded));
        return fields;
    }

    NamespaceAccess GetNamespaceAccess(std::string_view nameSpace)
    {
        if (nameSpace == "public" || nameSpace == "editor" || nameSpace == "builder")
            return NamespaceAccess::Editor;
        if (nameSpace == "manager")
            return NamespaceAccess::Manager;
        return NamespaceAccess::Invalid;
    }

    bool HasStaffAccess(Player const* player)
    {
        return player && player->GetSession()
            && player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES);
    }

    bool HasServerStaffAccess(Player const* player)
    {
        return player && player->GetSession()
            && sRoleplayPhaseMgr.CanMutateCommonWorld(player->GetSession()->GetSecurity());
    }

    uint64 CharacterGuid(Player const* player)
    {
        return player ? player->GetGUID().GetCounter() : 0;
    }

    uint32 AccountId(Player const* player)
    {
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    bool HasRole(Player const* player, uint64 phaseId, RoleplayPhaseRole required)
    {
        return player && player->GetSession()
            && sRoleplayPhaseMgr.HasRole(phaseId, CharacterGuid(player), AccountId(player), required);
    }

    RoleplayPhaseRole EffectiveRole(Player const* player, uint64 phaseId)
    {
        return sRoleplayPhaseMgr.GetMemberRole(phaseId, CharacterGuid(player), AccountId(player));
    }

    bool CanViewPhase(Player const* player, uint64 phaseId)
    {
        return sRoleplayPhaseMgr.CanView(phaseId, CharacterGuid(player), AccountId(player), HasStaffAccess(player));
    }

    bool CanEnterPhase(Player const* player, uint64 phaseId)
    {
        if (!player)
            return false;
        return sRoleplayPhaseMgr.CanEnter(phaseId, CharacterGuid(player), AccountId(player),
            player->GetMapId(), HasStaffAccess(player));
    }

    bool CanManagePhase(Player const* player, uint64 phaseId)
    {
        return sRoleplayPhaseMgr.CanManage(phaseId, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player));
    }

    bool CanEditPhase(Player const* player, uint64 phaseId)
    {
        return sRoleplayPhaseMgr.CanEdit(phaseId, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player));
    }

    bool CanArchivePhase(Player const* player, uint64 phaseId)
    {
        return sRoleplayPhaseMgr.CanArchive(phaseId, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player));
    }

    bool CanUnarchivePhase(Player const* player, uint64 phaseId)
    {
        return sRoleplayPhaseMgr.CanUnarchive(phaseId, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player));
    }

    bool CanOwnPhase(Player const* player, uint64 phaseId)
    {
        return sRoleplayPhaseMgr.CanOwnPhase(phaseId, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player));
    }

    bool CanReadNamespace(Player const* player, uint64 phaseId, std::string_view nameSpace)
    {
        NamespaceAccess const access = GetNamespaceAccess(nameSpace);
        if (access == NamespaceAccess::Invalid)
            return false;

        RoleplayPhaseRole const required = access == NamespaceAccess::Manager
            ? RoleplayPhaseRole::Manager
            : RoleplayPhaseRole::Viewer;
        return HasRole(player, phaseId, required) || (HasStaffAccess(player) && required == RoleplayPhaseRole::Viewer);
    }

    bool CanWriteNamespace(Player const* player, uint64 phaseId, std::string_view nameSpace)
    {
        NamespaceAccess const access = GetNamespaceAccess(nameSpace);
        if (access == NamespaceAccess::Invalid)
            return false;

        RoleplayPhaseRole const required = access == NamespaceAccess::Manager
            ? RoleplayPhaseRole::Manager
            : RoleplayPhaseRole::Editor;
        return HasRole(player, phaseId, required);
    }

    std::string RoleName(RoleplayPhaseRole role)
    {
        switch (role)
        {
            case RoleplayPhaseRole::Viewer: return "viewer";
            case RoleplayPhaseRole::Editor: return "editor";
            case RoleplayPhaseRole::Manager: return "manager";
            case RoleplayPhaseRole::Owner: return "owner";
            default: return "none";
        }
    }

    std::string OwnerLabel(uint32 accountId)
    {
        if (!accountId)
            return "Server";

        std::string owner;
        if (!AccountMgr::GetName(accountId, owner) || owner.empty())
            return "Unknown";
        return owner;
    }

    // Access/capability flags only. Metadata (public/owner/spawn) is emitted once
    // by the caller so LIST rows do not duplicate keys and stay compact.
    // Addon ops are ACL-gated via RoleplayPhaseMgr; chat `.rps phase …` still uses RBAC.
    std::string CapabilityFields(Player const* player, uint64 phaseId)
    {
        RoleplayPhaseRole const role = EffectiveRole(player, phaseId);
        bool const staff = HasStaffAccess(player);
        RoleplayPhaseInfo phase;
        bool const hasPhase = sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase);
        bool const canManage = CanManagePhase(player, phaseId);
        bool const canOwn = CanOwnPhase(player, phaseId);
        bool const canEdit = CanEditPhase(player, phaseId);
        bool const canEnter = CanEnterPhase(player, phaseId);
        bool const canGoto = hasPhase && phase.HasSpawn() && canEnter;
        bool const canGrantManager = canOwn || staff;
        return std::string("role=") + RoleName(role)
            + "|staff=" + (staff ? "1" : "0")
            + "|can_enter=" + (canEnter ? "1" : "0")
            + "|can_goto=" + (canGoto ? "1" : "0")
            + "|can_edit=" + (canEdit ? "1" : "0")
            + "|can_manage=" + (canManage ? "1" : "0")
            + "|can_own=" + (canOwn ? "1" : "0")
            + "|can_rename=" + (canOwn ? "1" : "0")
            + "|can_set_public=" + (canOwn ? "1" : "0")
            + "|can_set_owner=" + (canOwn ? "1" : "0")
            + "|can_set_role=" + (canManage ? "1" : "0")
            + "|can_grant_manager=" + (canGrantManager ? "1" : "0")
            + "|can_set_spawn=" + (canManage ? "1" : "0")
            + "|can_set_enter_spawn=" + (canManage ? "1" : "0")
            + "|can_archive=" + (CanArchivePhase(player, phaseId) ? "1" : "0")
            + "|can_unarchive=" + (CanUnarchivePhase(player, phaseId) ? "1" : "0")
            + "|can_invite=" + (canManage ? "1" : "0")
            + "|can_revoke=" + (canManage ? "1" : "0");
    }

    std::string PhaseMetadataFields(RoleplayPhaseInfo const& phase)
    {
        // Owner/username may contain '|', '\\' or control chars — escape values only.
        return std::string("public=") + (phase.IsPublic ? "1" : "0")
            + "|owner=" + EscapeField(OwnerLabel(phase.OwnerAccountId))
            + "|owner_account=" + std::to_string(phase.OwnerAccountId)
            + "|spawn_set=" + (phase.HasSpawn() ? "1" : "0")
            + "|spawn_map=" + (phase.SpawnMap ? std::to_string(*phase.SpawnMap) : "")
            + "|enter_spawn=" + (phase.EnterSpawn ? "1" : "0");
    }

    // Escape-token-aware chunk end: never split \|, \\ or \xNN across packets.
    size_t EscapeAwareChunkEnd(std::string_view escaped, size_t start, size_t limit)
    {
        if (start >= escaped.size() || !limit)
            return start;

        size_t const hardEnd = std::min(start + limit, escaped.size());
        size_t i = start;
        size_t lastSafe = start;
        while (i < hardEnd)
        {
            if (escaped[i] != '\\')
            {
                ++i;
                lastSafe = i;
                continue;
            }

            if (i + 1 >= escaped.size())
            {
                lastSafe = escaped.size();
                break;
            }

            size_t tokenLen = 2;
            if (escaped[i + 1] == 'x')
            {
                if (i + 3 >= escaped.size())
                {
                    lastSafe = escaped.size();
                    break;
                }
                tokenLen = 4;
            }

            if (i + tokenLen > hardEnd)
                break;

            i += tokenLen;
            lastSafe = i;
        }

        if (lastSafe == start)
        {
            // Limit too small for one escape token: emit the full token anyway.
            if (escaped[start] == '\\' && start + 1 < escaped.size())
            {
                if (escaped[start + 1] == 'x' && start + 3 < escaped.size())
                    return start + 4;
                return start + 2;
            }
            return std::min(start + 1, escaped.size());
        }

        return lastSafe;
    }

    bool ResolveActiveDataPhase(Player const* player, std::string_view phaseIdText, uint64& phaseId)
    {
        if (!player || !ParseUInt64(phaseIdText, phaseId) || !phaseId)
            return false;

        return sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), player->GetMapId()) == phaseId;
    }

    bool TrySendRaw(Player* player, std::string const& body)
    {
        if (!player)
            return false;
        if (body.size() > MAX_PAYLOAD)
        {
            TC_LOG_ERROR("roleplay.phase", "NN_RPPHASE packet truncated/dropped: size {} exceeds MAX_PAYLOAD {}.",
                body.size(), MAX_PAYLOAD);
            return false;
        }

        WorldPackets::Chat::Chat packet;
        packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, body, 0, "", DEFAULT_LOCALE, ADDON_PREFIX);
        player->SendDirectMessage(packet.Write());
        return true;
    }

    // Returns false when any fragment could not be delivered; callers must not send RESULT ok.
    // Wire format: structural '|' / record separators stay raw; only field *values* are
    // EscapeField()'d by the caller. Whole-payload EscapeField would turn separators into
    // '\|' and break the client's escape-aware splitRaw / parseRecord.
    bool SendPayload(Player* player, std::string_view type, std::string_view requestId, std::string_view payload)
    {
        std::string const wire(payload);
        std::string const checksum = Checksum(wire);
        size_t const chunkLimit = MAX_PAYLOAD > type.size() + requestId.size() + 48
            ? MAX_PAYLOAD - type.size() - requestId.size() - 48
            : 1;

        std::vector<std::string_view> chunks;
        for (size_t offset = 0; offset < wire.size();)
        {
            size_t const end = EscapeAwareChunkEnd(wire, offset, chunkLimit);
            if (end <= offset)
                break;
            chunks.emplace_back(wire.data() + offset, end - offset);
            offset = end;
        }
        if (chunks.empty())
            chunks.emplace_back();

        uint32 const count = uint32(chunks.size());
        if (!TrySendRaw(player, std::string(type) + "|" + std::string(requestId) + "|begin|0|"
            + std::to_string(count) + "|" + checksum))
            return false;
        for (uint32 index = 0; index < count; ++index)
        {
            if (!TrySendRaw(player, std::string(type) + "|" + std::string(requestId) + "|item|"
                + std::to_string(index + 1) + "|" + std::to_string(count) + "|" + checksum + "|"
                + std::string(chunks[index])))
                return false;
        }
        return TrySendRaw(player, std::string(type) + "|" + std::string(requestId) + "|end|0|"
            + std::to_string(count) + "|" + checksum);
    }

    void SendResult(Player* player, std::string_view requestId, std::string_view status, std::string_view detail)
    {
        std::string escapedDetail = EscapeField(detail);
        size_t const maxDetail = MAX_PAYLOAD > requestId.size() + status.size() + 40
            ? MAX_PAYLOAD - requestId.size() - status.size() - 40
            : 0;
        if (escapedDetail.size() > maxDetail)
            escapedDetail.resize(maxDetail);

        std::string const payload = std::string(status) + "|" + escapedDetail;
        TrySendRaw(player, "RESULT|" + std::string(requestId) + "|end|0|0|" + Checksum(payload) + "|" + payload);
    }

    bool SendPhaseInfo(Player* player, std::string_view requestId, RoleplayPhaseInfo const& phase)
    {
        return SendPayload(player, "INFO", requestId, "phase=" + std::to_string(phase.Id)
            + "|name=" + EscapeField(phase.Name)
            + "|description=" + EscapeField(phase.Description)
            + "|map=" + (phase.MapId ? std::to_string(*phase.MapId) : "global")
            + "|enabled=" + std::to_string(phase.Enabled ? 1 : 0)
            + "|archived=" + std::to_string(phase.Archived ? 1 : 0)
            + "|valid=" + std::to_string(phase.Valid ? 1 : 0)
            + "|members=" + std::to_string(phase.Members.size())
            + "|" + PhaseMetadataFields(phase)
            + "|" + CapabilityFields(player, phase.Id));
    }

    bool SendData(Player* player, std::string_view requestId, uint64 phaseId, std::string_view nameSpace,
        RoleplayPhaseAddonData const& data)
    {
        std::vector<uint8> bytes(data.Value.begin(), data.Value.end());
        std::string const value = Trinity::Encoding::Base64::Encode(bytes);
        std::string const key = data.Key.substr(std::string(nameSpace).size() + 1);
        return SendPayload(player, "DATA", requestId, "phase=" + std::to_string(phaseId)
            + "|namespace=" + EscapeField(std::string(nameSpace))
            + "|key=" + EscapeField(key)
            + "|version=" + std::to_string(data.Version)
            + "|checksum=" + Checksum(value)
            + "|encoding=base64|value=" + EscapeField(value));
    }

    void SendPayloadOrError(Player* player, std::string_view type, std::string_view requestId,
        std::string_view payload, std::string_view okDetail)
    {
        if (!SendPayload(player, type, requestId, payload))
        {
            SendResult(player, requestId, "error", "payload delivery failed");
            return;
        }
        SendResult(player, requestId, "ok", okDetail);
    }

    void RemoveExpiredUploads()
    {
        std::lock_guard<std::mutex> lock(UploadMutex);
        auto const now = std::chrono::steady_clock::now();
        std::erase_if(Uploads, [now](auto const& entry)
        {
            return entry.second.ExpiresAt <= now;
        });
    }

    bool CommitData(Player* player, Upload const& upload, std::string& detail)
    {
        if (!player || sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), player->GetMapId()) != upload.PhaseId
            || !CanWriteNamespace(player, upload.PhaseId, upload.NameSpace))
        {
            detail = "data write denied";
            return false;
        }

        Optional<std::vector<uint8>> decoded = Trinity::Encoding::Base64::Decode(upload.EncodedValue);
        if (!decoded || decoded->size() > MaxValueSize)
        {
            detail = "invalid or oversized base64 value";
            return false;
        }

        std::string const value(decoded->begin(), decoded->end());
        std::lock_guard<std::mutex> lock(MutationMutex);

        RoleplayPhaseAddonData existing;
        bool const exists = sRoleplayPhaseMgr.GetAddonData(upload.PhaseId, upload.NameSpace + ":" + upload.Key, existing);
        if ((exists && existing.Version != upload.ExpectedVersion) || (!exists && upload.ExpectedVersion != 0))
        {
            detail = "version conflict";
            return false;
        }

        std::vector<RoleplayPhaseAddonData> rows;
        sRoleplayPhaseMgr.GetAddonDataByNamespace(upload.PhaseId, "", rows);
        size_t used = 0;
        for (RoleplayPhaseAddonData const& row : rows)
            used += row.Value.size();
        size_t const existingSize = exists ? existing.Value.size() : 0;
        size_t const retainedSize = used >= existingSize ? used - existingSize : 0;
        if (value.size() > MaxPhaseQuota || retainedSize > MaxPhaseQuota - value.size())
        {
            detail = "phase quota exceeded";
            return false;
        }

        std::string const storageKey = upload.NameSpace + ":" + upload.Key;
        RoleplayDatabasePreparedStatement* statement = nullptr;
        if (exists)
        {
            statement = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_ADDON_DATA_CAS);
            statement->setString(0, value);
            statement->setUInt64(1, player->GetGUID().GetCounter());
            statement->setUInt64(2, upload.PhaseId);
            statement->setString(3, storageKey);
            statement->setUInt64(4, upload.ExpectedVersion);
        }
        else
        {
            statement = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE_ADDON_DATA);
            statement->setUInt64(0, upload.PhaseId);
            statement->setString(1, storageKey);
            statement->setString(2, value);
            statement->setUInt64(3, player->GetGUID().GetCounter());
        }
        RoleplayDatabase.DirectExecute(statement);

        // Reload publishes a single immutable snapshot. All response assembly
        // below reads only that snapshot; it does not issue follow-up SQL.
        if (!sRoleplayPhaseMgr.Reload())
        {
            detail = "snapshot reload failed";
            return false;
        }

        RoleplayPhaseAddonData committed;
        if (!sRoleplayPhaseMgr.GetAddonData(upload.PhaseId, storageKey, committed)
            || committed.Version != upload.ExpectedVersion + 1
            || committed.Value != value)
        {
            detail = "version conflict";
            return false;
        }

        sRoleplayPhaseMgr.WriteAudit(player->GetGUID().GetCounter(), player->GetSession()->GetAccountId(),
            "addon_data_put", upload.PhaseId, R"({"source":"NN_RPPHASE"})");
        detail = "saved version " + std::to_string(committed.Version);
        return true;
    }

    void HandleContext(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        if (fields.size() == 2 || (fields.size() == 3 && fields[2] == "get"))
        {
            if (!SendContext(player, requestId))
            {
                SendResult(player, requestId, "error", "payload delivery failed");
                return;
            }
            SendResult(player, requestId, "ok", "context snapshot");
            return;
        }

        if (fields.size() == 4 && fields[2] == "enter")
        {
            uint64 phaseId = 0;
            RoleplayPhaseInfo phase;
            if (!ParseUInt64(fields[3], phaseId) || !phaseId || !sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase)
                || !(phase.EnterSpawn && phase.HasSpawn()
                    ? sRoleplayPhaseMgr.GotoPhase(player, phaseId)
                    : sRoleplayPhaseMgr.TransitionPlayer(player, phaseId)))
            {
                SendResult(player, requestId, "error", "phase entry denied");
                return;
            }

            if (!SendContext(player, requestId))
            {
                SendResult(player, requestId, "error", "payload delivery failed");
                return;
            }
            SendResult(player, requestId, "ok", "entered");
            return;
        }

        if (fields.size() == 4 && fields[2] == "goto")
        {
            uint64 phaseId = 0;
            if (!ParseUInt64(fields[3], phaseId) || !phaseId || !sRoleplayPhaseMgr.GotoPhase(player, phaseId))
            {
                SendResult(player, requestId, "error", "phase goto denied");
                return;
            }

            if (!SendContext(player, requestId))
            {
                SendResult(player, requestId, "error", "payload delivery failed");
                return;
            }
            SendResult(player, requestId, "ok", "goto");
            return;
        }

        if (fields.size() == 3 && fields[2] == "leave")
        {
            if (!sRoleplayPhaseMgr.TransitionPlayer(player, 0))
            {
                SendResult(player, requestId, "error", "phase leave failed");
                return;
            }

            if (!SendContext(player, requestId))
            {
                SendResult(player, requestId, "error", "payload delivery failed");
                return;
            }
            SendResult(player, requestId, "ok", "left");
            return;
        }

        SendResult(player, requestId, "error", "invalid CONTEXT request");
    }

    void HandleInfo(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        uint64 phaseId = 0;
        RoleplayPhaseInfo phase;
        if (fields.size() != 3 || !ParseUInt64(fields[2], phaseId)
            || !CanViewPhase(player, phaseId)
            || !sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase))
        {
            SendResult(player, requestId, "error", "phase unavailable");
            return;
        }

        if (!SendPhaseInfo(player, requestId, phase))
        {
            SendResult(player, requestId, "error", "payload delivery failed");
            return;
        }
        SendResult(player, requestId, "ok", "info snapshot");
    }

    void HandleList(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // LIST|rid  or  LIST|rid|my  or  LIST|rid|owned
        bool const mineOnly = fields.size() == 3 && fields[2] == "my";
        bool const ownedOnly = fields.size() == 3 && fields[2] == "owned";
        if (fields.size() != 2 && !mineOnly && !ownedOnly)
        {
            SendResult(player, requestId, "error", "invalid LIST request");
            return;
        }

        std::vector<RoleplayPhaseInfo> phases;
        sRoleplayPhaseMgr.GetPhaseList(phases);
        std::sort(phases.begin(), phases.end(), [](RoleplayPhaseInfo const& left, RoleplayPhaseInfo const& right)
        {
            return left.Id < right.Id;
        });

        std::string payload;
        for (RoleplayPhaseInfo const& phase : phases)
        {
            if (ownedOnly)
            {
                if (!sRoleplayPhaseMgr.IsOwnerAccount(phase.Id, AccountId(player)))
                    continue;
            }
            else if (mineOnly)
            {
                if (!sRoleplayPhaseMgr.IsOwnedOrMember(phase.Id, CharacterGuid(player), AccountId(player)))
                    continue;
            }
            else if (!CanViewPhase(player, phase.Id))
                continue;
            // Client splitEscapedLines splits on the two-char sequence "\n", not a raw LF.
            if (!payload.empty())
                payload += "\\n";
            payload += "phase=" + std::to_string(phase.Id)
                + "|name=" + EscapeField(phase.Name)
                + "|map=" + (phase.MapId ? std::to_string(*phase.MapId) : "global")
                + "|members=" + std::to_string(phase.Members.size())
                + "|archived=" + std::to_string(phase.Archived ? 1 : 0)
                + "|" + PhaseMetadataFields(phase)
                + "|" + CapabilityFields(player, phase.Id);
        }

        char const* okDetail = ownedOnly ? "list owned snapshot" : (mineOnly ? "list my snapshot" : "list snapshot");
        SendPayloadOrError(player, "LIST", requestId, payload, okDetail);
    }

    void HandleMembers(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        uint64 phaseId = 0;
        RoleplayPhaseInfo phase;
        if (fields.size() != 3 || !ParseUInt64(fields[2], phaseId)
            || !CanViewPhase(player, phaseId)
            || !sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase))
        {
            SendResult(player, requestId, "error", "phase unavailable");
            return;
        }

        std::sort(phase.Members.begin(), phase.Members.end(), [](RoleplayPhaseMemberInfo const& left, RoleplayPhaseMemberInfo const& right)
        {
            return left.CharacterGuid < right.CharacterGuid;
        });
        std::string payload;
        for (RoleplayPhaseMemberInfo const& member : phase.Members)
        {
            if (!payload.empty())
                payload += "\\n";

            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(member.CharacterGuid);
            std::string charName;
            uint32 accountId = 0;
            if (CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(guid))
            {
                charName = cache->Name;
                accountId = cache->AccountId;
            }
            else
            {
                sCharacterCache->GetCharacterNameByGuid(guid, charName);
                accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
            }
            if (charName.empty())
                charName = "#" + std::to_string(member.CharacterGuid);

            // role = effective (account owner → owner); acl_role = stored membership ENUM.
            RoleplayPhaseRole const effective = sRoleplayPhaseMgr.GetMemberRole(phaseId, member.CharacterGuid, accountId);
            payload += "phase=" + std::to_string(phaseId)
                + "|character=" + std::to_string(member.CharacterGuid)
                + "|name=" + EscapeField(charName)
                + "|account=" + EscapeField(OwnerLabel(accountId))
                + "|account_id=" + std::to_string(accountId)
                + "|role=" + RoleName(effective)
                + "|acl_role=" + RoleName(member.Role);
        }

        SendPayloadOrError(player, "MEMBERS", requestId, payload, "members snapshot");
    }

    void HandleRules(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        uint64 phaseId = 0;
        if (fields.size() != 3 || !ParseUInt64(fields[2], phaseId) || !CanViewPhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "phase unavailable");
            return;
        }

        SendPayloadOrError(player, "RULES", requestId, "phase=" + std::to_string(phaseId)
            + "|public.read=" + std::to_string(CanReadNamespace(player, phaseId, "public") ? 1 : 0)
            + "|public.write=" + std::to_string(CanWriteNamespace(player, phaseId, "public") ? 1 : 0)
            + "|editor.read=" + std::to_string(CanReadNamespace(player, phaseId, "editor") ? 1 : 0)
            + "|editor.write=" + std::to_string(CanWriteNamespace(player, phaseId, "editor") ? 1 : 0)
            // Keep builder fields as a one-release compatibility alias.
            + "|builder.read=" + std::to_string(CanReadNamespace(player, phaseId, "builder") ? 1 : 0)
            + "|builder.write=" + std::to_string(CanWriteNamespace(player, phaseId, "builder") ? 1 : 0)
            + "|manager.read=" + std::to_string(CanReadNamespace(player, phaseId, "manager") ? 1 : 0)
            + "|manager.write=" + std::to_string(CanWriteNamespace(player, phaseId, "manager") ? 1 : 0)
            + "|max_value=" + std::to_string(MaxValueSize)
            + "|phase_quota=" + std::to_string(MaxPhaseQuota)
            + "|" + CapabilityFields(player, phaseId), "rules snapshot");
    }

    void HandleCaps(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        if (fields.size() != 2)
        {
            SendResult(player, requestId, "error", "invalid CAPS request");
            return;
        }

        bool const staff = HasStaffAccess(player);
        bool const canCreate = player && player->GetSession()
            && player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_CREATE);
        bool const canArchivePerm = player && player->GetSession()
            && player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ARCHIVE);
        SendPayloadOrError(player, "CAPS", requestId, std::string("staff=") + (staff ? "1" : "0")
            + "|can_create=" + (canCreate ? "1" : "0")
            + "|can_archive=" + (canArchivePerm ? "1" : "0")
            + "|default_scope=global"
            + "|soft_archive=1"
            + "|purge=0", "caps snapshot");
    }

    void HandleCreate(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // CREATE|rid|name|scope|mapIdOrEmpty|description
        if (!player || !player->GetSession()
            || !player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_CREATE)
            || fields.size() != 6)
        {
            SendResult(player, requestId, "error", "create denied");
            return;
        }

        std::string const& name = fields[2];
        std::string const& scope = fields[3];
        std::string const& mapToken = fields[4];
        std::string const& description = fields[5];
        if (name.empty() || name.size() > 96 || description.size() > 255)
        {
            SendResult(player, requestId, "error", "invalid create fields");
            return;
        }

        Optional<uint32> mapId;
        if (scope == "map")
        {
            Optional<uint32> parsed = Trinity::StringTo<uint32>(mapToken);
            if (!parsed || !sMapStore.LookupEntry(*parsed))
            {
                SendResult(player, requestId, "error", "invalid map scope");
                return;
            }
            mapId = *parsed;
        }
        else if (scope != "global" || !mapToken.empty())
        {
            SendResult(player, requestId, "error", "invalid create scope");
            return;
        }

        uint64 phaseId = 0;
        if (!sRoleplayPhaseMgr.Create(name, description, AccountId(player), CharacterGuid(player), phaseId, mapId))
        {
            SendResult(player, requestId, "error", "create failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), "addon_create", phaseId,
            R"({"source":"NN_RPPHASE"})");
        RoleplayPhaseInfo phase;
        if (sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase) && !SendPhaseInfo(player, requestId, phase))
        {
            SendResult(player, requestId, "error", "payload delivery failed");
            return;
        }
        SendResult(player, requestId, "ok", "created " + std::to_string(phaseId));
    }

    void HandleArchive(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // ARCHIVE|rid|phaseId|confirm  (ACL: Owner/staff via CanArchivePhase)
        uint64 phaseId = 0;
        if (!player || fields.size() != 4 || !ParseUInt64(fields[2], phaseId) || fields[3] != "confirm")
        {
            SendResult(player, requestId, "error", "archive denied");
            return;
        }

        if (!CanArchivePhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "archive denied");
            return;
        }

        RoleplayPhaseInfo phase;
        if (!sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase))
        {
            SendResult(player, requestId, "error", "archive denied");
            return;
        }
        bool const staffBypass = HasStaffAccess(player) && phase.OwnerAccountId != AccountId(player);

        if (!sRoleplayPhaseMgr.Archive(phaseId, CharacterGuid(player), AccountId(player), staffBypass,
            HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "archive failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player),
            staffBypass ? "addon_archive_staff" : "addon_archive", phaseId,
            staffBypass ? R"({"source":"NN_RPPHASE","bypass":"all_phases"})" : R"({"source":"NN_RPPHASE"})");
        SendResult(player, requestId, "ok", "archived " + std::to_string(phaseId));
    }

    void HandleUnarchive(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // UNARCHIVE|rid|phaseId|confirm  (ACL: Owner/staff via CanUnarchivePhase)
        uint64 phaseId = 0;
        if (!player || fields.size() != 4 || !ParseUInt64(fields[2], phaseId) || fields[3] != "confirm")
        {
            SendResult(player, requestId, "error", "unarchive denied");
            return;
        }

        bool const staffBypass = HasStaffAccess(player)
            && !sRoleplayPhaseMgr.CanUnarchive(phaseId, CharacterGuid(player), AccountId(player), false,
                HasServerStaffAccess(player));
        if (!CanUnarchivePhase(player, phaseId) && !staffBypass)
        {
            SendResult(player, requestId, "error", "unarchive denied");
            return;
        }

        if (!sRoleplayPhaseMgr.Unarchive(phaseId, CharacterGuid(player), AccountId(player), staffBypass,
            HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "unarchive failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player),
            staffBypass ? "addon_unarchive_staff" : "addon_unarchive", phaseId,
            staffBypass ? R"({"source":"NN_RPPHASE","bypass":"all_phases"})" : R"({"source":"NN_RPPHASE"})");
        SendResult(player, requestId, "ok", "unarchived " + std::to_string(phaseId));
    }

    Optional<RoleplayPhaseRole> ParseMemberRoleToken(std::string_view role)
    {
        if (role == "viewer")
            return RoleplayPhaseRole::Viewer;
        if (role == "editor" || role == "builder")
            return RoleplayPhaseRole::Editor;
        if (role == "manager")
            return RoleplayPhaseRole::Manager;
        return {};
    }

    bool ResolveCharacterGuidByName(std::string const& name, uint64& characterGuid)
    {
        characterGuid = 0;
        if (name.empty())
            return false;
        if (ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(name); !guid.IsEmpty())
        {
            characterGuid = guid.GetCounter();
            return characterGuid != 0;
        }
        return false;
    }

    void HandleInvite(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // INVITE|rid|phaseId|playerName|role  (ACL: Manager+; grant manager = Owner/staff)
        uint64 phaseId = 0;
        Optional<RoleplayPhaseRole> role;
        uint64 targetGuid = 0;
        if (!player || fields.size() != 5 || !ParseUInt64(fields[2], phaseId)
            || !(role = ParseMemberRoleToken(fields[4]))
            || !ResolveCharacterGuidByName(fields[3], targetGuid)
            || !CanManagePhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "invite denied");
            return;
        }

        if (!sRoleplayPhaseMgr.SetMemberRole(phaseId, targetGuid, *role, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "invite failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), "addon_invite", phaseId,
            R"({"source":"NN_RPPHASE"})");
        SendResult(player, requestId, "ok", "invited " + std::to_string(targetGuid));
    }

    void HandleRevoke(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // REVOKE|rid|phaseId|playerName|confirm  (ACL: Manager+)
        uint64 phaseId = 0;
        uint64 targetGuid = 0;
        if (!player || fields.size() != 5 || !ParseUInt64(fields[2], phaseId) || fields[4] != "confirm"
            || !ResolveCharacterGuidByName(fields[3], targetGuid)
            || !CanManagePhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "revoke denied");
            return;
        }

        if (!sRoleplayPhaseMgr.RemoveMember(phaseId, targetGuid, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "revoke failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), "addon_revoke", phaseId,
            R"({"source":"NN_RPPHASE"})");
        SendResult(player, requestId, "ok", "revoked " + std::to_string(targetGuid));
    }

    void HandleSetRole(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // SET_ROLE|rid|phaseId|playerName|role  (ACL: Manager+; grant manager = Owner/staff)
        uint64 phaseId = 0;
        Optional<RoleplayPhaseRole> role;
        uint64 targetGuid = 0;
        if (!player || fields.size() != 5 || !ParseUInt64(fields[2], phaseId)
            || !(role = ParseMemberRoleToken(fields[4]))
            || !ResolveCharacterGuidByName(fields[3], targetGuid)
            || !CanManagePhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "set_role denied");
            return;
        }

        if (!sRoleplayPhaseMgr.SetMemberRole(phaseId, targetGuid, *role, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "set_role failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), "addon_set_role", phaseId,
            R"({"source":"NN_RPPHASE"})");
        SendResult(player, requestId, "ok", "role set");
    }

    void HandleSetPublic(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // SET_PUBLIC|rid|phaseId|true|false
        uint64 phaseId = 0;
        Optional<bool> isPublic;
        if (fields.size() == 4)
        {
            if (fields[3] == "true" || fields[3] == "1")
                isPublic = true;
            else if (fields[3] == "false" || fields[3] == "0")
                isPublic = false;
        }
        if (!player || !isPublic || !ParseUInt64(fields[2], phaseId) || !CanOwnPhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "set_public denied");
            return;
        }

        if (!sRoleplayPhaseMgr.SetPublic(phaseId, *isPublic, CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "set_public failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), "addon_set_public", phaseId,
            R"({"source":"NN_RPPHASE"})");
        SendResult(player, requestId, "ok", *isPublic ? "public" : "private");
    }

    void HandleRename(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // RENAME|rid|phaseId|name  (ACL: Owner/staff)
        uint64 phaseId = 0;
        if (!player || fields.size() != 4 || !ParseUInt64(fields[2], phaseId) || fields[3].empty()
            || !CanOwnPhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "rename denied");
            return;
        }

        if (!sRoleplayPhaseMgr.Rename(phaseId, fields[3], CharacterGuid(player), AccountId(player),
            HasStaffAccess(player), HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "rename failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), "addon_rename", phaseId,
            R"({"source":"NN_RPPHASE"})");
        RoleplayPhaseInfo phase;
        if (sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase) && !SendPhaseInfo(player, requestId, phase))
        {
            SendResult(player, requestId, "error", "payload delivery failed");
            return;
        }
        SendResult(player, requestId, "ok", "renamed");
    }

    void HandleSetOwner(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        // SET_OWNER|rid|phaseId|characterName|force   (characterName may be "0" for Server; ACL: Owner/staff)
        uint64 phaseId = 0;
        if (!player || fields.size() != 5 || !ParseUInt64(fields[2], phaseId) || fields[4] != "force"
            || !CanOwnPhase(player, phaseId))
        {
            SendResult(player, requestId, "error", "set_owner denied");
            return;
        }

        uint32 ownerAccountId = 0;
        uint64 managerGuid = 0;
        if (fields[3] != "0")
        {
            if (!ResolveCharacterGuidByName(fields[3], managerGuid) || !managerGuid)
            {
                SendResult(player, requestId, "error", "owner character not found");
                return;
            }
            CharacterCacheEntry const* character = sCharacterCache->GetCharacterCacheByGuid(
                ObjectGuid::Create<HighGuid::Player>(managerGuid));
            if (!character || !character->AccountId || character->IsDeleted)
            {
                SendResult(player, requestId, "error", "owner character not found");
                return;
            }
            ownerAccountId = character->AccountId;
        }

        if (!sRoleplayPhaseMgr.SetOwner(phaseId, ownerAccountId, managerGuid, CharacterGuid(player), AccountId(player),
            true, HasStaffAccess(player), HasServerStaffAccess(player)))
        {
            SendResult(player, requestId, "error", "set_owner failed");
            return;
        }

        sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), "addon_set_owner", phaseId,
            R"({"source":"NN_RPPHASE"})");
        SendResult(player, requestId, "ok", ownerAccountId ? "owner transferred" : "owner server");
    }

    void HandleDataGet(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        uint64 phaseId = 0;
        if (fields.size() != 6 || !ResolveActiveDataPhase(player, fields[3], phaseId)
            || !IsAsciiIdentifier(fields[4], MaxNamespaceLength) || !IsAsciiIdentifier(fields[5], MaxKeyLength)
            || !CanReadNamespace(player, phaseId, fields[4]))
        {
            SendResult(player, requestId, "error", "data read denied");
            return;
        }

        RoleplayPhaseAddonData data;
        if (!sRoleplayPhaseMgr.GetAddonData(phaseId, fields[4] + ":" + fields[5], data))
        {
            SendResult(player, requestId, "error", "data key not found");
            return;
        }

        if (!SendData(player, requestId, phaseId, fields[4], data))
        {
            SendResult(player, requestId, "error", "payload delivery failed");
            return;
        }
        SendResult(player, requestId, "ok", "data snapshot");
    }

    void HandleDataList(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        uint64 phaseId = 0;
        if (fields.size() != 5 || !ResolveActiveDataPhase(player, fields[3], phaseId)
            || !IsAsciiIdentifier(fields[4], MaxNamespaceLength) || !CanReadNamespace(player, phaseId, fields[4]))
        {
            SendResult(player, requestId, "error", "data list denied");
            return;
        }

        std::vector<RoleplayPhaseAddonData> rows;
        sRoleplayPhaseMgr.GetAddonDataByNamespace(phaseId, fields[4], rows);
        std::sort(rows.begin(), rows.end(), [](RoleplayPhaseAddonData const& left, RoleplayPhaseAddonData const& right)
        {
            return left.Key < right.Key;
        });

        std::string payload;
        size_t const prefixLength = fields[4].size() + 1;
        for (RoleplayPhaseAddonData const& row : rows)
        {
            if (!payload.empty())
                payload += "\\n";
            payload += "phase=" + std::to_string(phaseId)
                + "|namespace=" + EscapeField(fields[4])
                + "|key=" + EscapeField(row.Key.substr(prefixLength))
                + "|version=" + std::to_string(row.Version)
                + "|size=" + std::to_string(row.Value.size());
        }
        SendPayloadOrError(player, "DATA", requestId, payload, "data list snapshot");
    }

    void HandleDataPutBegin(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        uint64 phaseId = 0;
        uint64 expectedVersion = 0;
        uint32 chunkCount = 0;
        if (fields.size() != 9 || !ResolveActiveDataPhase(player, fields[3], phaseId)
            || !IsAsciiIdentifier(fields[4], MaxNamespaceLength) || !IsAsciiIdentifier(fields[5], MaxKeyLength)
            || !ParseUInt64(fields[6], expectedVersion) || !ParseUInt32(fields[7], chunkCount)
            || !chunkCount || chunkCount > MaxUploadChunks || !IsChecksum(fields[8])
            || !CanWriteNamespace(player, phaseId, fields[4]))
        {
            SendResult(player, requestId, "error", "data write denied");
            return;
        }

        Upload upload;
        upload.OwnerCharacterGuid = CharacterGuid(player);
        upload.PhaseId = phaseId;
        upload.NameSpace = fields[4];
        upload.Key = fields[5];
        upload.ExpectedVersion = expectedVersion;
        upload.ChunkCount = chunkCount;
        upload.Checksum = fields[8];
        upload.ExpiresAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(UploadTimeoutMs);

        std::lock_guard<std::mutex> lock(UploadMutex);
        std::string const uploadKey = MakeUploadKey(player, requestId);
        bool const replacesExisting = Uploads.contains(uploadKey);
        size_t const playerUploads = std::count_if(Uploads.begin(), Uploads.end(), [&upload](auto const& entry)
        {
            return entry.second.OwnerCharacterGuid == upload.OwnerCharacterGuid;
        });
        if (!replacesExisting
            && (Uploads.size() >= MaxPendingUploadsGlobal || playerUploads >= MaxPendingUploadsPerPlayer))
        {
            SendResult(player, requestId, "error", "too many pending uploads");
            return;
        }
        Uploads[uploadKey] = std::move(upload);
        SendResult(player, requestId, "accepted", "upload started");
    }

    void HandleDataPutItem(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        uint32 index = 0;
        uint32 count = 0;
        if (fields.size() != 6 || !ParseUInt32(fields[3], index) || !ParseUInt32(fields[4], count))
        {
            SendResult(player, requestId, "error", "invalid upload item");
            return;
        }

        if (!std::all_of(fields[5].begin(), fields[5].end(), [](unsigned char ch)
        {
            return std::isalnum(ch) || ch == '+' || ch == '/' || ch == '=';
        }))
        {
            SendResult(player, requestId, "error", "invalid base64 item");
            return;
        }

        std::lock_guard<std::mutex> lock(UploadMutex);
        auto upload = Uploads.find(MakeUploadKey(player, requestId));
        if (upload == Uploads.end() || upload->second.ExpiresAt <= std::chrono::steady_clock::now()
            || upload->second.ChunkCount != count || upload->second.NextChunk != index
            || upload->second.EncodedValue.size() + fields[5].size() > (MaxValueSize * 4 / 3) + 8)
        {
            Uploads.erase(MakeUploadKey(player, requestId));
            SendResult(player, requestId, "error", "upload expired or out of sequence");
            return;
        }

        upload->second.EncodedValue += fields[5];
        ++upload->second.NextChunk;
        upload->second.ExpiresAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(UploadTimeoutMs);
        SendResult(player, requestId, "accepted", "upload item received");
    }

    void HandleDataPutEnd(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        if (fields.size() != 3)
        {
            SendResult(player, requestId, "error", "invalid upload end");
            return;
        }

        Upload upload;
        {
            std::lock_guard<std::mutex> lock(UploadMutex);
            auto entry = Uploads.find(MakeUploadKey(player, requestId));
            if (entry == Uploads.end() || entry->second.ExpiresAt <= std::chrono::steady_clock::now()
                || entry->second.NextChunk != entry->second.ChunkCount + 1
                || Checksum(entry->second.EncodedValue) != entry->second.Checksum)
            {
                if (entry != Uploads.end())
                    Uploads.erase(entry);
                SendResult(player, requestId, "error", "incomplete or corrupt upload");
                return;
            }
            upload = std::move(entry->second);
            Uploads.erase(entry);
        }

        std::string detail;
        if (!CommitData(player, upload, detail))
        {
            SendResult(player, requestId, "conflict", detail);
            return;
        }

        RoleplayPhaseAddonData data;
        if (sRoleplayPhaseMgr.GetAddonData(upload.PhaseId, upload.NameSpace + ":" + upload.Key, data)
            && !SendData(player, requestId, upload.PhaseId, upload.NameSpace, data))
        {
            SendResult(player, requestId, "error", "payload delivery failed");
            return;
        }
        SendResult(player, requestId, "ok", detail);
    }

    void HandleData(Player* player, std::string_view requestId, std::vector<std::string> const& fields)
    {
        if (fields.size() < 3)
        {
            SendResult(player, requestId, "error", "invalid DATA request");
            return;
        }

        if (fields[2] == "get")
            return HandleDataGet(player, requestId, fields);
        if (fields[2] == "list")
            return HandleDataList(player, requestId, fields);
        if (fields[2] == "put_begin")
            return HandleDataPutBegin(player, requestId, fields);
        if (fields[2] == "put_item")
            return HandleDataPutItem(player, requestId, fields);
        if (fields[2] == "put_end")
            return HandleDataPutEnd(player, requestId, fields);

        SendResult(player, requestId, "error", "unknown DATA operation");
    }

    bool HandleAddonMessage(Player* player, uint32 chatType, std::string_view prefix,
        std::string_view message, bool selfWhisper)
    {
        if (prefix != ADDON_PREFIX)
            return false;

        // The matching prefix is always consumed, including malformed packets,
        // so protocol traffic cannot be reflected to another addon listener.
        Optional<std::vector<std::string>> fields = SplitRequest(message);
        std::string requestId = fields && fields->size() > 1 && IsRequestId((*fields)[1]) ? (*fields)[1] : "0";
        if (!player || !player->GetSession() || chatType != CHAT_MSG_WHISPER || !selfWhisper)
        {
            SendResult(player, requestId, "error", "self-whisper addon channel required");
            return true;
        }

        RemoveExpiredUploads();
        if (!fields || fields->size() < 2 || !IsRequestId((*fields)[1]))
        {
            SendResult(player, requestId, "error", fields ? "malformed request" : "malformed escape sequence");
            return true;
        }

        std::string const& operation = (*fields)[0];
        if (operation == "CONTEXT")
            HandleContext(player, requestId, *fields);
        else if (operation == "INFO")
            HandleInfo(player, requestId, *fields);
        else if (operation == "LIST")
            HandleList(player, requestId, *fields);
        else if (operation == "MEMBERS")
            HandleMembers(player, requestId, *fields);
        else if (operation == "RULES")
            HandleRules(player, requestId, *fields);
        else if (operation == "CAPS")
            HandleCaps(player, requestId, *fields);
        else if (operation == "CREATE")
            HandleCreate(player, requestId, *fields);
        else if (operation == "ARCHIVE")
            HandleArchive(player, requestId, *fields);
        else if (operation == "UNARCHIVE")
            HandleUnarchive(player, requestId, *fields);
        else if (operation == "INVITE")
            HandleInvite(player, requestId, *fields);
        else if (operation == "REVOKE")
            HandleRevoke(player, requestId, *fields);
        else if (operation == "SET_ROLE")
            HandleSetRole(player, requestId, *fields);
        else if (operation == "SET_PUBLIC")
            HandleSetPublic(player, requestId, *fields);
        else if (operation == "RENAME")
            HandleRename(player, requestId, *fields);
        else if (operation == "SET_OWNER")
            HandleSetOwner(player, requestId, *fields);
        else if (operation == "DATA")
            HandleData(player, requestId, *fields);
        else
            SendResult(player, requestId, "error", "unknown request");
        return true;
    }
}

std::string EscapeField(std::string_view value)
{
    static constexpr char Hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value)
    {
        switch (ch)
        {
            case '\\': out += "\\\\"; break;
            case '|': out += "\\|"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20 || ch == 0x7F || ch >= 0x80)
                {
                    out += "\\x";
                    out.push_back(Hex[ch >> 4]);
                    out.push_back(Hex[ch & 0x0F]);
                }
                else
                    out.push_back(char(ch));
                break;
        }
    }
    return out;
}

void SendRaw(Player* player, std::string const& body)
{
    if (!player)
        return;
    if (body.size() > MAX_PAYLOAD)
    {
        TC_LOG_ERROR("roleplay.phase", "NN_RPPHASE SendRaw dropped: size {} exceeds MAX_PAYLOAD {}.",
            body.size(), MAX_PAYLOAD);
        return;
    }

    WorldPackets::Chat::Chat packet;
    packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, body, 0, "", DEFAULT_LOCALE, ADDON_PREFIX);
    player->SendDirectMessage(packet.Write());
}

bool SendContext(Player* player, std::string_view requestId)
{
    if (!player || !player->GetSession())
        return false;

    uint64 const phaseId = sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), player->GetMapId());
    std::string payload = "phase=" + std::to_string(phaseId)
        + "|character=" + std::to_string(player->GetGUID().GetCounter());
    if (phaseId)
    {
        RoleplayPhaseInfo phase;
        if (sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase))
        {
            payload += "|name=" + EscapeField(phase.Name)
                + "|map=" + (phase.MapId ? std::to_string(*phase.MapId) : "global")
                + "|" + PhaseMetadataFields(phase)
                + "|" + CapabilityFields(player, phaseId);
        }
    }
    if (!SendPayload(player, "CONTEXT", requestId, payload))
    {
        TC_LOG_ERROR("roleplay.phase", "NN_RPPHASE CONTEXT payload delivery failed for request {}.",
            std::string(requestId));
        return false;
    }
    return true;
}

void SendTransitionEvent(Player* player, uint64 previousPhaseId, uint64 currentPhaseId)
{
    if (!SendPayload(player, "EVENT", "0", "event=transition|previous_phase=" + std::to_string(previousPhaseId)
        + "|current_phase=" + std::to_string(currentPhaseId)))
        TC_LOG_ERROR("roleplay.phase", "NN_RPPHASE EVENT payload delivery failed.");
    SendContext(player, "0");
}

void RegisterHandlers()
{
    sRoleplayPhaseMgr.RegisterAddonMessageHandler(HandleAddonMessage);
    sRoleplayPhaseMgr.RegisterTransitionHandler(SendTransitionEvent);
}
}

void AddSC_NobleNextRoleplayPhaseProtocol()
{
    RoleplayCore::NobleNext::RoleplayPhaseProtocol::RegisterHandlers();
}
