/*
 * NobleNext — .gobject blueprint * (AccountID keys, public/private, member edit).
 * ChatHandler::PSendSysMessage uses printf (%s/%u). Trailing `silent` skips chat dumps.
 */

#include "noble_next_gobblueprint_mgr.h"
#include "../GobGroup/noble_next_gobgroup_protocol.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    namespace
    {
        std::string TailToString(Tail const& value)
        {
            return std::string(value.data(), value.size());
        }

        bool ExtractSilent(std::string& text)
        {
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.pop_back();

            constexpr char const* kSilent = "silent";
            size_t const n = std::char_traits<char>::length(kSilent);
            if (text.size() == n && text == kSilent)
            {
                text.clear();
                return true;
            }
            if (text.size() > n)
            {
                size_t const pos = text.size() - n;
                if (text.compare(pos, n, kSilent) == 0 && (pos == 0 || text[pos - 1] == ' ' || text[pos - 1] == '\t'))
                {
                    if (pos == 0)
                        text.clear();
                    else
                    {
                        text.resize(pos);
                        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                            text.pop_back();
                    }
                    return true;
                }
            }
            return false;
        }

        GobBlueprintListScope ParseListScope(std::string& text)
        {
            auto consume = [&](char const* token, GobBlueprintListScope scope) -> bool
            {
                size_t const n = std::char_traits<char>::length(token);
                if (text.size() >= n && text.compare(0, n, token) == 0
                    && (text.size() == n || text[n] == ' ' || text[n] == '\t'))
                {
                    text.erase(0, n);
                    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                        text.erase(text.begin());
                    return true;
                }
                return false;
            };

            if (consume("mine", GobBlueprintListScope::Mine))
                return GobBlueprintListScope::Mine;
            if (consume("public", GobBlueprintListScope::Public))
                return GobBlueprintListScope::Public;
            if (consume("all", GobBlueprintListScope::All))
                return GobBlueprintListScope::All;
            return GobBlueprintListScope::Mine;
        }

        std::vector<std::string> SplitArgs(std::string const& text)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char ch : text)
            {
                if (ch == ' ' || ch == '\t')
                {
                    if (!cur.empty())
                    {
                        out.push_back(cur);
                        cur.clear();
                    }
                }
                else
                    cur.push_back(ch);
            }
            if (!cur.empty())
                out.push_back(cur);
            return out;
        }
    }

    class GobBlueprintCommands : public CommandScript
    {
    public:
        GobBlueprintCommands() : CommandScript("noble_next_gobblueprint_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable memberTable =
            {
                { "add",      HandleMemberAdd,      LANG_COMMAND_GOBBLUEPRINT_MEMBER_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_MEMBER, Console::No },
                { "remove",   HandleMemberRemove,   LANG_COMMAND_GOBBLUEPRINT_MEMBER_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_MEMBER, Console::No },
                { "replace",  HandleMemberReplace,  LANG_COMMAND_GOBBLUEPRINT_MEMBER_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_MEMBER, Console::No },
                { "setroot",  HandleMemberSetRoot,  LANG_COMMAND_GOBBLUEPRINT_MEMBER_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_MEMBER, Console::No },
                { "setcenter",HandleMemberSetCenter,LANG_COMMAND_GOBBLUEPRINT_MEMBER_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_MEMBER, Console::No },
            };

            static ChatCommandTable blueprintTable =
            {
                { "help",       HandleHelp,      LANG_COMMAND_GOBBLUEPRINT_HELP,            rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_HELP,       Console::No },
                { "list",       HandleList,      LANG_COMMAND_GOBBLUEPRINT_LIST_HELP,       rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_LIST,       Console::No },
                { "info",       HandleInfo,      LANG_COMMAND_GOBBLUEPRINT_INFO_HELP,       rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_INFO,       Console::No },
                { "new",        HandleNew,       LANG_COMMAND_GOBBLUEPRINT_NEW_HELP,        rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_NEW,        Console::No },
                { "update",     HandleUpdate,    LANG_COMMAND_GOBBLUEPRINT_UPDATE_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_UPDATE,     Console::No },
                { "spawn",      HandleSpawn,     LANG_COMMAND_GOBBLUEPRINT_SPAWN_HELP,      rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_SPAWN,      Console::No },
                { "delete",     HandleDelete,    LANG_COMMAND_GOBBLUEPRINT_DELETE_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_DELETE,     Console::No },
                { "rename",     HandleRename,    LANG_COMMAND_GOBBLUEPRINT_RENAME_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_RENAME,     Console::No },
                { "set-public", HandleSetPublic, LANG_COMMAND_GOBBLUEPRINT_SET_PUBLIC_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_SET_PUBLIC, Console::No },
                { "member",     memberTable },
                { "status",     HandleStatus,    LANG_COMMAND_GOBBLUEPRINT_HELP,            rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT,            Console::No },
            };

            static ChatCommandTable gobjectTable =
            {
                { "blueprint", blueprintTable },
            };

            static ChatCommandTable commandTable =
            {
                { "gobject", gobjectTable },
            };
            return commandTable;
        }

    private:
        static bool RequirePlayer(ChatHandler* handler, Player*& player, std::string_view verb)
        {
            player = handler ? handler->GetPlayer() : nullptr;
            if (!player)
            {
                GobGroupProtocol::SendBlueprintResult(nullptr, verb, "error", "player required");
                handler->SendSysMessage("Команда доступна только в игре.");
                handler->SetSentErrorMessage(true);
                return false;
            }
            return true;
        }

        static bool HandleHelp(ChatHandler* handler)
        {
            handler->SendSysMessage("NobleNext Шаблоны (ключ $Account-$Id):");
            handler->SendSysMessage("  .gobject blueprint list [mine|public|all] [$filter] [silent]");
            handler->SendSysMessage("  .gobject blueprint info|spawn|delete|update <$key|$name> [silent]");
            handler->SendSysMessage("  .gobject blueprint new $name | rename <$key|$name> $new | set-public <$key|$name> <0|1>");
            handler->SendSysMessage("  .gobject blueprint member add object|group <key> <anchor> <guid>");
            handler->SendSysMessage("  .gobject blueprint member remove object|group <key> <id>");
            handler->SendSysMessage("  .gobject blueprint member replace|setroot <key> <memberId> [entry]");
            handler->SendSysMessage("  .gobject blueprint member setcenter <key> [player]");
            Trinity::ChatCommands::SendCommandHelpFor(*handler, "gobject blueprint");
            return true;
        }

        static bool HandleList(ChatHandler* handler, Optional<Tail> args)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "list"))
                return false;

            std::string text = args ? TailToString(*args) : std::string();
            bool const silent = ExtractSilent(text);
            GobBlueprintListScope const scope = ParseListScope(text);
            std::string filter = text;

            std::vector<GobBlueprintListItem> items;
            std::string error;
            if (!sGobBlueprintMgr.List(player, scope, filter, items, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "list", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobGroupProtocol::SendBlueprintList(player, items, filter);
            if (!silent)
            {
                handler->PSendSysMessage("Шаблоны: %u", uint32(items.size()));
                for (GobBlueprintListItem const& item : items)
                {
                    handler->PSendSysMessage("  [%s] «%s» — %u GO%s%s",
                        item.Key().c_str(), item.Name.c_str(), item.MemberCount,
                        item.IsPublic ? " [public]" : " [private]",
                        item.CanMutate ? "" : " (ro)");
                }
            }
            GobGroupProtocol::SendBlueprintResult(player, "list", "ok",
                Trinity::StringFormat("count={}", items.size()));
            return true;
        }

        static bool HandleInfo(ChatHandler* handler, Tail name)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "info"))
                return false;

            std::string key = TailToString(name);
            bool const silent = ExtractSilent(key);
            if (key.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint info <$key|$name>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string error;
            if (!sGobBlueprintMgr.Info(player, key, record, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "info", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobGroupProtocol::SendBlueprintInfo(player, record);
            if (!silent)
                handler->PSendSysMessage("Шаблон %s «%s» — %u GO%s",
                    record.Key().c_str(), record.Name.c_str(), record.PhysicalMemberCount(),
                    record.IsPublic ? " [public]" : " [private]");
            GobGroupProtocol::SendBlueprintResult(player, "info", "ok", record.Key());
            return true;
        }

        static bool HandleNew(ChatHandler* handler, Tail name)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "new"))
                return false;

            std::string nameStr = TailToString(name);
            bool const silent = ExtractSilent(nameStr);
            if (nameStr.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint new $name");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string error;
            if (!sGobBlueprintMgr.NewFromActiveGroup(player, nameStr, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "new", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string infoError;
            uint32 count = 0;
            if (sGobBlueprintMgr.Info(player, nameStr, record, infoError))
                count = record.PhysicalMemberCount();

            if (!silent)
                handler->PSendSysMessage("Шаблон «%s» сохранён как %s (%u GO)",
                    nameStr.c_str(), record.Key().c_str(), count);
            GobGroupProtocol::SendBlueprintResult(player, "new", "ok",
                Trinity::StringFormat("{}:{}", record.Key(), count));
            if (count > 0 || record.HasVirtualRoot())
                GobGroupProtocol::SendBlueprintInfo(player, record);
            return true;
        }

        static bool HandleUpdate(ChatHandler* handler, Tail name)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "update"))
                return false;

            std::string key = TailToString(name);
            bool const silent = ExtractSilent(key);
            if (key.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint update <$key|$name>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string error;
            if (!sGobBlueprintMgr.UpdateFromActiveGroup(player, key, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "update", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string infoError;
            uint32 count = 0;
            if (sGobBlueprintMgr.Info(player, key, record, infoError))
                count = record.PhysicalMemberCount();

            if (!silent)
                handler->PSendSysMessage("Шаблон %s перезаписан (%u GO). Мир не изменён.",
                    record.Key().c_str(), count);
            GobGroupProtocol::SendBlueprintResult(player, "update", "ok",
                Trinity::StringFormat("{}:{}", record.Key(), count));
            GobGroupProtocol::SendBlueprintInfo(player, record);
            return true;
        }

        static bool HandleSpawn(ChatHandler* handler, Tail name)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "spawn"))
                return false;

            std::string key = TailToString(name);
            bool const silent = ExtractSilent(key);
            if (key.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint spawn <$key|$name>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string error;
            if (!sGobBlueprintMgr.Spawn(player, key, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "spawn", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (!silent)
                handler->PSendSysMessage("Ставим шаблон «%s» у ваших ног…", key.c_str());
            GobGroupProtocol::SendBlueprintResult(player, "spawn", "queued", key);
            GobGroupProtocol::SendBlueprintStatus(player, sGobBlueprintMgr.BuildStatus(player));
            return true;
        }

        static bool HandleDelete(ChatHandler* handler, Tail name)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "delete"))
                return false;

            std::string key = TailToString(name);
            bool const silent = ExtractSilent(key);
            if (key.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint delete <$key|$name>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string error;
            if (!sGobBlueprintMgr.Delete(player, key, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "delete", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (!silent)
                handler->PSendSysMessage("Шаблон «%s» удалён.", key.c_str());
            GobGroupProtocol::SendBlueprintResult(player, "delete", "ok", key);
            return true;
        }

        static bool HandleRename(ChatHandler* handler, std::string_view oldName, Tail newName)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "rename"))
                return false;
            if (oldName.empty() || newName.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint rename <$key|$name> $new");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string const oldStr(oldName);
            std::string newStr = TailToString(newName);
            bool const silent = ExtractSilent(newStr);
            if (newStr.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint rename <$key|$name> $new");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string error;
            if (!sGobBlueprintMgr.Rename(player, oldStr, newStr, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "rename", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (!silent)
                handler->PSendSysMessage("Шаблон «%s» переименован в «%s».", oldStr.c_str(), newStr.c_str());
            GobGroupProtocol::SendBlueprintResult(player, "rename", "ok",
                Trinity::StringFormat("{}->{}", oldStr, newStr));
            return true;
        }

        static bool HandleSetPublic(ChatHandler* handler, Tail args)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "set-public"))
                return false;

            std::string text = TailToString(args);
            bool const silent = ExtractSilent(text);
            auto parts = SplitArgs(text);
            if (parts.size() < 2)
            {
                handler->SendSysMessage("Использование: .gobject blueprint set-public <$key|$name> <0|1>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            bool const isPublic = parts.back() == "1" || parts.back() == "public" || parts.back() == "on";
            std::string key = parts[0];
            for (size_t i = 1; i + 1 < parts.size(); ++i)
                key += " " + parts[i];

            std::string error;
            if (!sGobBlueprintMgr.SetPublic(player, key, isPublic, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "set-public", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (!silent)
                handler->PSendSysMessage("Шаблон «%s» теперь %s.", key.c_str(), isPublic ? "публичный" : "приватный");
            GobGroupProtocol::SendBlueprintResult(player, "set-public", "ok",
                Trinity::StringFormat("{}:{}", key, isPublic ? 1 : 0));
            return true;
        }

        static bool HandleMemberAdd(ChatHandler* handler, Tail args)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "member-add"))
                return false;

            std::string text = TailToString(args);
            bool const silent = ExtractSilent(text);
            auto parts = SplitArgs(text);
            // add object|group <key> <anchor> <guid>
            if (parts.size() < 4)
            {
                handler->SendSysMessage("Использование: .gobject blueprint member add object|group <key> <anchorGroup> <guid>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string const kind = parts[0];
            std::string const key = parts[1];
            uint64 const anchor = strtoull(parts[2].c_str(), nullptr, 10);
            uint64 const guid = strtoull(parts[3].c_str(), nullptr, 10);
            std::string error;
            bool ok = false;
            if (kind == "object" || kind == "go")
                ok = sGobBlueprintMgr.MemberAddObject(player, key, anchor, guid, error);
            else if (kind == "group")
                ok = sGobBlueprintMgr.MemberAddGroup(player, key, anchor, guid, error);
            else
                error = "Ожидается object|group";

            if (!ok)
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "member-add", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string infoError;
            if (sGobBlueprintMgr.Info(player, key, record, infoError))
                GobGroupProtocol::SendBlueprintInfo(player, record);
            if (!silent)
                handler->PSendSysMessage("В шаблон «%s» добавлен %s.", key.c_str(), kind.c_str());
            GobGroupProtocol::SendBlueprintResult(player, "member-add", "ok", key);
            return true;
        }

        static bool HandleMemberRemove(ChatHandler* handler, Tail args)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "member-remove"))
                return false;

            std::string text = TailToString(args);
            bool const silent = ExtractSilent(text);
            auto parts = SplitArgs(text);
            // remove object|group <key> <id>
            if (parts.size() < 3)
            {
                handler->SendSysMessage("Использование: .gobject blueprint member remove object|group <key> <id>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string const kind = parts[0];
            std::string const key = parts[1];
            uint32 const id = uint32(strtoul(parts[2].c_str(), nullptr, 10));
            std::string error;
            bool ok = false;
            if (kind == "object" || kind == "go")
                ok = sGobBlueprintMgr.MemberRemoveObject(player, key, id, error);
            else if (kind == "group")
                ok = sGobBlueprintMgr.MemberRemoveGroup(player, key, id, error);
            else
                error = "Ожидается object|group";

            if (!ok)
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "member-remove", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string infoError;
            if (sGobBlueprintMgr.Info(player, key, record, infoError))
                GobGroupProtocol::SendBlueprintInfo(player, record);
            if (!silent)
                handler->PSendSysMessage("Из шаблона «%s» удалён %s #%u.", key.c_str(), kind.c_str(), id);
            GobGroupProtocol::SendBlueprintResult(player, "member-remove", "ok", key);
            return true;
        }

        static bool HandleMemberReplace(ChatHandler* handler, Tail args)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "member-replace"))
                return false;

            std::string text = TailToString(args);
            bool const silent = ExtractSilent(text);
            auto parts = SplitArgs(text);
            // replace <key> <memberId> <entry>
            if (parts.size() < 3)
            {
                handler->SendSysMessage("Использование: .gobject blueprint member replace <key> <memberId> <entry>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string const key = parts[0];
            uint32 const memberId = uint32(strtoul(parts[1].c_str(), nullptr, 10));
            uint32 const entry = uint32(strtoul(parts[2].c_str(), nullptr, 10));
            std::string error;
            if (!sGobBlueprintMgr.MemberReplace(player, key, memberId, entry, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "member-replace", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string infoError;
            if (sGobBlueprintMgr.Info(player, key, record, infoError))
                GobGroupProtocol::SendBlueprintInfo(player, record);
            if (!silent)
                handler->PSendSysMessage("В шаблоне «%s» member #%u заменён на entry %u.",
                    key.c_str(), memberId, entry);
            GobGroupProtocol::SendBlueprintResult(player, "member-replace", "ok", key);
            return true;
        }

        static bool HandleMemberSetRoot(ChatHandler* handler, Tail args)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "member-setroot"))
                return false;

            std::string text = TailToString(args);
            bool const silent = ExtractSilent(text);
            auto parts = SplitArgs(text);
            if (parts.size() < 2)
            {
                handler->SendSysMessage("Использование: .gobject blueprint member setroot <key> <memberId>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string const key = parts[0];
            uint32 const memberId = uint32(strtoul(parts[1].c_str(), nullptr, 10));
            std::string error;
            if (!sGobBlueprintMgr.MemberSetRoot(player, key, memberId, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "member-setroot", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string infoError;
            if (sGobBlueprintMgr.Info(player, key, record, infoError))
                GobGroupProtocol::SendBlueprintInfo(player, record);
            if (!silent)
                handler->PSendSysMessage("Root шаблона «%s» → member #%u.", key.c_str(), memberId);
            GobGroupProtocol::SendBlueprintResult(player, "member-setroot", "ok", key);
            return true;
        }

        static bool HandleMemberSetCenter(ChatHandler* handler, Tail args)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "member-setcenter"))
                return false;

            std::string text = TailToString(args);
            bool const silent = ExtractSilent(text);
            auto parts = SplitArgs(text);
            if (parts.empty())
            {
                handler->SendSysMessage("Использование: .gobject blueprint member setcenter <key> [player]");
                handler->SetSentErrorMessage(true);
                return false;
            }

            std::string const key = parts[0];
            bool const fromPlayer = parts.size() < 2 || parts[1] == "player" || parts[1] == "here";
            std::string error;
            if (!sGobBlueprintMgr.MemberSetCenter(player, key, fromPlayer, error))
            {
                if (!silent)
                    handler->SendSysMessage(error.c_str());
                GobGroupProtocol::SendBlueprintResult(player, "member-setcenter", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobBlueprintRecord record;
            std::string infoError;
            if (sGobBlueprintMgr.Info(player, key, record, infoError))
                GobGroupProtocol::SendBlueprintInfo(player, record);
            if (!silent)
                handler->PSendSysMessage("Центр шаблона «%s» обновлён%s.",
                    key.c_str(), fromPlayer ? " (позиция игрока)" : "");
            GobGroupProtocol::SendBlueprintResult(player, "member-setcenter", "ok", key);
            return true;
        }

        static bool HandleStatus(ChatHandler* handler, Optional<EXACT_SEQUENCE("silent")> silentFlag)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "status"))
                return false;

            bool const silent = !!silentFlag;
            std::string const status = sGobBlueprintMgr.BuildStatus(player);
            if (!silent)
                handler->SendSysMessage(status.c_str());
            GobGroupProtocol::SendBlueprintStatus(player, status);
            GobGroupProtocol::SendBlueprintResult(player, "status", "ok", status);
            return true;
        }
    };
}

void AddSC_NobleNextGobBlueprintCommands()
{
    new RoleplayCore::NobleNext::GobBlueprintCommands();
}
