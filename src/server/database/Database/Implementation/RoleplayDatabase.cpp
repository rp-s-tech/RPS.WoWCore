/*
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "RoleplayDatabase.h"
#include "MySQLPreparedStatement.h"

void RoleplayDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_RoleplayDATABASE_STATEMENTS);

    // SELECTS
    PrepareStatement(Roleplay_SEL_CREATUREEXTRA, "SELECT guid, scale, id_creator_bnet, id_creator_player, id_modifier_bnet, id_modifier_player, UNIX_TIMESTAMP(created), UNIX_TIMESTAMP(modified), phaseMask, displayLock, displayId, nativeDisplayId, genderLock, gender, swim, gravity, fly FROM creature_extra", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_CREATUREEXTRA_TEMPLATE, "SELECT id_entry, disabled FROM creature_template_extra", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_GAMEOBJECTEXTRA_BY_CREATOR, "SELECT guid FROM gameobject_extra WHERE id_creator_player = ?", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_GAMEOBJECTEXTRA_BY_GUID, "SELECT id_creator_bnet, id_creator_player FROM gameobject_extra WHERE guid = ?", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_GAMEOBJECTEXTRA_BY_BNET, "SELECT guid FROM gameobject_extra WHERE id_creator_bnet = ?", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_SERVER_SETTINGS, "SELECT setting_name, setting_value FROM server_settings WHERE setting_name IN ('static_hour', 'static_minute', 'time_freezed')", CONNECTION_SYNCH);
    // PreparedStatementTask represents a successful zero-row SELECT as nullptr.
    // Add a NULL sentinel so snapshot loaders can distinguish an empty table
    // from an actual prepare/execute failure without changing global DB behavior.
    PrepareStatement(Roleplay_SEL_RP_PHASES, "SELECT phase_id, name, description, owner_account_id, map_id, visibility_mode, enabled, is_public, spawn_map, spawn_x, spawn_y, spawn_z, spawn_o, enter_spawn, created_at, archived_at FROM rp_phase UNION ALL SELECT NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_RP_PHASE_MEMBERS, "SELECT phase_id, character_guid, role FROM rp_phase_member UNION ALL SELECT NULL, NULL, NULL", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_CHARACTER_RP_PHASES, "SELECT character_guid, account_id, phase_id FROM character_rp_phase UNION ALL SELECT NULL, NULL, NULL", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_CHARACTER_RP_PHASE_BY_CHARACTER, "SELECT COUNT(*), COALESCE(MAX(phase_id), 0), COALESCE(MAX(account_id), 0) FROM character_rp_phase WHERE character_guid = ?", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_RP_PHASE_SPAWNS, "SELECT phase_id, spawn_type, spawn_id, map_id FROM rp_phase_spawn UNION ALL SELECT NULL, NULL, NULL, NULL", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_RP_PHASE_SPAWN_BY_KEY, "SELECT phase_id, map_id FROM rp_phase_spawn WHERE spawn_type = ? AND spawn_id = ?", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_RP_PHASE_SPAWN_COUNT_BY_KEY, "SELECT COUNT(*) FROM rp_phase_spawn WHERE spawn_type = ? AND spawn_id = ?", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_RP_PHASE_LATEST_BY_META, "SELECT phase_id FROM rp_phase WHERE name = ? AND description = ? AND owner_account_id = ? AND ((? = 0 AND map_id IS NULL) OR map_id = ?) AND archived_at IS NULL ORDER BY phase_id DESC LIMIT 1", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_RP_PHASE_ADDON_DATA, "SELECT phase_id, data_key, data_value, version, updated_by, updated_at FROM rp_phase_addon_data UNION ALL SELECT NULL, NULL, NULL, NULL, NULL, NULL", CONNECTION_SYNCH);
    PrepareStatement(Roleplay_SEL_RP_PHASE_ADDON_DATA_KEY, "SELECT data_value, version, updated_by, updated_at FROM rp_phase_addon_data WHERE phase_id = ? AND data_key = ?", CONNECTION_SYNCH);
    
    // DELETIONS
    PrepareStatement(Roleplay_DEL_CREATUREEXTRA, "DELETE FROM creature_extra WHERE guid = ?", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_DEL_GAMEOBJECTEXTRA, "DELETE FROM gameobject_extra WHERE guid = ?", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_DEL_CUSTOMNPC, "DELETE FROM custom_npcs WHERE `Key` = ?", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_DEL_CUSTOMNPCOWNER, "DELETE FROM custom_npc_owners WHERE `Key` = ?", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_DEL_SERVER_SETTINGS, "DELETE FROM server_settings WHERE setting_name IN ('static_hour', 'static_minute', 'time_freezed')", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_DEL_RP_PHASE_MEMBER, "DELETE FROM rp_phase_member WHERE phase_id = ? AND character_guid = ?", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_DEL_CHARACTER_RP_PHASE, "DELETE FROM character_rp_phase WHERE character_guid = ?", CONNECTION_BOTH);
    PrepareStatement(Roleplay_DEL_RP_PHASE_SPAWN, "DELETE FROM rp_phase_spawn WHERE spawn_type = ? AND spawn_id = ?", CONNECTION_BOTH);

    // UPDATES
    PrepareStatement(Roleplay_UPD_CREATUREEXTRA_TEMPLATE, "UPDATE creature_template_extra SET disabled = ? WHERE id_entry = ?", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_UPD_RP_PHASE, "UPDATE rp_phase SET name = ?, description = ?, map_id = ?, visibility_mode = ?, enabled = ? WHERE phase_id = ? AND archived_at IS NULL", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_UPD_RP_PHASE_NAME, "UPDATE rp_phase SET name = ? WHERE phase_id = ? AND archived_at IS NULL", CONNECTION_BOTH);
    PrepareStatement(Roleplay_UPD_RP_PHASE_ARCHIVE, "UPDATE rp_phase SET enabled = 0, archived_at = NOW() WHERE phase_id = ? AND archived_at IS NULL", CONNECTION_BOTH);
    PrepareStatement(Roleplay_UPD_RP_PHASE_UNARCHIVE, "UPDATE rp_phase SET enabled = 1, archived_at = NULL WHERE phase_id = ? AND archived_at IS NOT NULL", CONNECTION_BOTH);
    PrepareStatement(Roleplay_UPD_RP_PHASE_PUBLIC, "UPDATE rp_phase SET is_public = ? WHERE phase_id = ? AND archived_at IS NULL", CONNECTION_BOTH);
    PrepareStatement(Roleplay_UPD_RP_PHASE_OWNER, "UPDATE rp_phase SET owner_account_id = ? WHERE phase_id = ? AND archived_at IS NULL", CONNECTION_BOTH);
    PrepareStatement(Roleplay_UPD_RP_PHASE_ENTER_SPAWN, "UPDATE rp_phase SET spawn_map = ?, spawn_x = ?, spawn_y = ?, spawn_z = ?, spawn_o = ?, enter_spawn = ? WHERE phase_id = ? AND archived_at IS NULL", CONNECTION_BOTH);
    PrepareStatement(Roleplay_UPD_RP_PHASE_ADDON_DATA_CAS, "UPDATE rp_phase_addon_data SET data_value = ?, version = version + 1, updated_by = ?, updated_at = NOW() WHERE phase_id = ? AND data_key = ? AND version = ?", CONNECTION_BOTH);

    // REPLACES
    PrepareStatement(Roleplay_REP_CREATUREEXTRA, "REPLACE INTO creature_extra (guid, scale, id_creator_bnet, id_creator_player, id_modifier_bnet, id_modifier_player, created, modified, phaseMask, displayLock, displayId, nativeDisplayId, genderLock, gender, swim, gravity, fly) VALUES (?, ?, ?, ?, ?, ?, from_unixtime(?), from_unixtime(?), ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    // CONNECTION_BOTH: GobBlueprint CommitRoleplay uses DirectCommitTransaction (sync).
    PrepareStatement(Roleplay_REP_GAMEOBJECTEXTRA, "REPLACE INTO gameobject_extra (guid, id_creator_bnet, id_creator_player, created, modified) VALUES (?, ?, ?, NOW(), NOW())", CONNECTION_BOTH);
    PrepareStatement(Roleplay_REP_CUSTOMNPCDATA, "REPLACE INTO custom_npcs (`Key`, Entry) VALUES (?, ?)", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_REP_CUSTOMNPCOWNER, "INSERT INTO custom_npc_owners (`Key`, owner_bnet_account_id, owner_alias, created_at, updated_at) VALUES (?, ?, ?, NOW(), NOW()) ON DUPLICATE KEY UPDATE owner_bnet_account_id = VALUES(owner_bnet_account_id), owner_alias = VALUES(owner_alias), updated_at = NOW()", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_REP_SERVER_SETTINGS, "INSERT INTO server_settings (setting_name, setting_value) VALUES (?, ?)", CONNECTION_ASYNC);
    PrepareStatement(Roleplay_REP_CHARACTER_RP_PHASE, "REPLACE INTO character_rp_phase (character_guid, account_id, phase_id) VALUES (?, ?, ?)", CONNECTION_BOTH);

    // INSERTS
    PrepareStatement(Roleplay_INS_RP_PHASE, "INSERT INTO rp_phase (name, description, owner_account_id, map_id, visibility_mode, enabled) VALUES (?, ?, ?, ?, ?, ?)", CONNECTION_BOTH);
    PrepareStatement(Roleplay_INS_RP_PHASE_MEMBER, "INSERT INTO rp_phase_member (phase_id, character_guid, role) VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE role = VALUES(role)", CONNECTION_BOTH);
    PrepareStatement(Roleplay_INS_RP_PHASE_SPAWN, "INSERT INTO rp_phase_spawn (phase_id, spawn_type, spawn_id, map_id) VALUES (?, ?, ?, ?) ON DUPLICATE KEY UPDATE phase_id = VALUES(phase_id), map_id = VALUES(map_id)", CONNECTION_BOTH);
    PrepareStatement(Roleplay_INS_RP_PHASE_ADDON_DATA, "INSERT INTO rp_phase_addon_data (phase_id, data_key, data_value, version, updated_by) VALUES (?, ?, ?, 1, ?)", CONNECTION_BOTH);
    // WriteTransitionAudit uses DirectExecute so command/reconciliation audit
    // is persisted before the caller continues. The statement must therefore
    // exist on the synchronous connection as well as the async pool.
    PrepareStatement(Roleplay_INS_RP_PHASE_AUDIT, "INSERT INTO rp_phase_audit (actor_character_guid, actor_account_id, action, phase_id, detail) VALUES (?, ?, ?, ?, ?)", CONNECTION_BOTH);
}

RoleplayDatabaseConnection::RoleplayDatabaseConnection(MySQLConnectionInfo& connInfo, ConnectionFlags connectionFlags) : MySQLConnection(connInfo, connectionFlags)
{
}

RoleplayDatabaseConnection::~RoleplayDatabaseConnection()
{
}
