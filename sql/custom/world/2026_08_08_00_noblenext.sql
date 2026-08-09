SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `gameobject_group_root` (
  `root_guid` BIGINT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL DEFAULT '',
  `created_by` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`root_guid`),
  KEY `idx_gameobject_group_root_name` (`name`(32))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `gameobject_group` (
  `member_guid` BIGINT UNSIGNED NOT NULL,
  `root_guid` BIGINT UNSIGNED NOT NULL,
  `offset_x` FLOAT NOT NULL DEFAULT 0,
  `offset_y` FLOAT NOT NULL DEFAULT 0,
  `offset_z` FLOAT NOT NULL DEFAULT 0,
  `offset_o` FLOAT NOT NULL DEFAULT 0,
  `rotation0` FLOAT NOT NULL DEFAULT 0,
  `rotation1` FLOAT NOT NULL DEFAULT 0,
  `rotation2` FLOAT NOT NULL DEFAULT 0,
  `rotation3` FLOAT NOT NULL DEFAULT 1,
  PRIMARY KEY (`member_guid`),
  KEY `idx_gameobject_group_root_guid` (`root_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

SET @rps_add_server_phase_id = (
  SELECT IF(
    COUNT(*) = 0,
    'ALTER TABLE `gameobject_teleport` ADD COLUMN `server_phase_id` BIGINT UNSIGNED NULL DEFAULT NULL AFTER `phase`',
    'SELECT 1'
  )
  FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'gameobject_teleport'
    AND `COLUMN_NAME` = 'server_phase_id'
);
PREPARE rps_server_phase_stmt FROM @rps_add_server_phase_id;
EXECUTE rps_server_phase_stmt;
DEALLOCATE PREPARE rps_server_phase_stmt;

INSERT INTO `trinity_string`
(`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES
(11110, 'NobleNext: .gobject group help|capabilities|create|use|add|scan|addnear|remove|dissolve|delete|info|list|near|target|check|status|capture|recalc|sync|move|turn|nudge|rotate|scale|relocate|reload|cleanup', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group help|capabilities|create|use|add|scan|addnear|remove|dissolve|delete|info|list|near|target|check|status|capture|recalc|sync|move|turn|nudge|rotate|scale|relocate|reload|cleanup'),
(11111, 'NobleNext: .gobject group create <root guid> [name] — explicit GO becomes root/active group.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group create <root guid> [name] — указанный GO становится root/active group.'),
(11112, 'NobleNext: .gobject group use|target <group-guid> — select active group (root or member → canonical root).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group use|target <group-guid> — выбрать active group (root или member → canonical root).'),
(11113, 'NobleNext: .gobject group add <group-guid> <member-guid> | scan <group-guid> <radius> | addnear <group-guid> <radius> confirm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group add <group-guid> <member-guid> | scan <group-guid> <radius> | addnear <group-guid> <radius> confirm'),
(11114, 'NobleNext: .gobject group remove <group-guid> <member-guid> | dissolve <group-guid> — metadata only | delete <group-guid> full-force — delete every GO.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group remove <group-guid> <member-guid> | dissolve <group-guid> — только metadata | delete <group-guid> full-force — удалить все GO.'),
(11115, 'NobleNext: .gobject group info <object-guid> | list [map] | near [radius] | check/status <group-guid> | reload', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group info <object-guid> | list [map] | near [radius] | check/status <group-guid> | reload'),
(11116, 'NobleNext: .gobject group capture <root|member guid> [silent] — refresh relative pose(s). silent = GobMover.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group capture <root|member guid> [silent] — обновить relative pose. silent = GobMover.'),
(11117, 'NobleNext: .gobject group recalc <group-guid> — accept current world layout as offsets; unlock dirty.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group recalc <group-guid> — принять текущую расстановку как offsets; снять dirty.'),
(11118, 'NobleNext: .gobject group sync <group-guid> — keep root, realign members from saved offsets.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group sync <group-guid> — оставить root и выровнять members по сохранённым offsets.'),
(11119, 'NobleNext: .gobject group move|turn|nudge|rotate|scale <group-guid> [...] | relocate <group-guid> <map> <x> <y> <z> <o> confirm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group move|turn|nudge|rotate|scale <group-guid> [...] | relocate <group-guid> <map> <x> <y> <z> <o> confirm'),
(11120, 'NobleNext: .gobject group cleanup confirm — delete orphan group metadata only (never deletes gameobject).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group cleanup confirm — удалить только orphan metadata, не удаляя gameobject.'),
(11130, 'Syntax: .rps phase help|create|list|info|enter|leave|invite|revoke|archive|unarchive|reload|spawn|set|goto', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase help|create|list|info|enter|leave|invite|revoke|archive|unarchive|reload|spawn|set|goto'),
(11131, 'Syntax: .rps phase create "<name>" [--map <mapId>] [description...] — create logical RP phase.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase create "<name>" [--map <mapId>] [описание...] — создать logical RP phase.'),
(11132, 'Syntax: .rps phase list [my|--all-phases] — list discoverable RP phases; my = owned/member, including archived.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase list [my|--all-phases] — список доступных фаз; my = свои owner/member, включая архивные.'),
(11133, 'Syntax: .rps phase info <phaseId> — show phase metadata, ACL role, public/private and spawn.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase info <phaseId> — показать параметры фазы, ACL, публичность и spawn.'),
(11134, 'Syntax: .rps phase enter <phaseId> — transition into a discoverable usable phase.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase enter <phaseId> — перейти в доступную активную фазу.'),
(11135, 'Syntax: .rps phase leave — leave current logical RP phase.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase leave — выйти из текущей logical RP phase.'),
(11136, 'Syntax: .rps phase invite <phaseId> <character> [viewer|editor|manager] — grant membership.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase invite <phaseId> <персонаж> [viewer|editor|manager] — выдать доступ.'),
(11137, 'Syntax: .rps phase revoke <phaseId> <character> — remove membership, not ownership.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase revoke <phaseId> <персонаж> — удалить membership, но не ownership.'),
(11138, 'Syntax: .rps phase archive <phaseId> confirm — soft-archive phase (Owner or staff 3045).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase archive <phaseId> confirm — архивировать фазу (Owner или staff 3045).'),
(11139, 'Syntax: .rps phase reload — reload RP phase snapshot from roleplay DB.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase reload — перезагрузить snapshot RP phases из roleplay DB.'),
(11140, 'Syntax: .rps phase spawn info|assign|clear — manage object-to-phase spawn mapping.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase spawn info|assign|clear — управлять привязкой spawn к фазе.'),
(11141, 'Syntax: .rps phase set name|public|owner|role|spawn|enter-spawn — change phase metadata.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase set name|public|owner|role|spawn|enter-spawn — изменить параметры фазы.'),
(11142, 'Syntax: .rps phase set public <phaseId> true|false — toggle public access (Owner or staff).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase set public <phaseId> true|false — изменить публичность (Owner или staff).'),
(11143, 'Syntax: .rps phase set owner <phaseId> <character|0> force — transfer owner account (Owner or staff; 0 = Server).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase set owner <phaseId> <персонаж|0> force — сменить owner account (Owner или staff; 0 = Server).'),
(11144, 'Syntax: .rps phase set role <phaseId> <character> viewer|editor|manager — set membership role.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase set role <phaseId> <персонаж> viewer|editor|manager — изменить роль участника.'),
(11145, 'Syntax: .rps phase set spawn <phaseId> — store current position as phase enter spawn.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase set spawn <phaseId> — сохранить текущую позицию как enter spawn.'),
(11146, 'Syntax: .rps phase set enter-spawn <phaseId> true|false — toggle teleport to spawn on enter.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase set enter-spawn <phaseId> true|false — включить телепорт на spawn при входе.'),
(11147, 'Syntax: .rps phase goto <phaseId> — transition and teleport to phase spawn.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase goto <phaseId> — перейти в фазу и телепортироваться на её spawn.'),
(11148, 'Syntax: .lookup rp phase [query] — search discoverable RP phases by id, name, description or owner.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .lookup rp phase [запрос] — найти доступную RP phase по id, имени, описанию или owner.'),
(11149, 'Syntax: .rps phase unarchive <phaseId> confirm — restore soft-archived RP phase (Owner or staff 3045).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase unarchive <phaseId> confirm — разархивировать RP phase (Owner или staff 3045).'),
(11150, 'Syntax: .rps phase set name <phaseId> "<name>" — rename RP phase (Owner or staff).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .rps phase set name <phaseId> "<имя>" — переименовать RP phase (Owner или staff).'),
(11151, 'Syntax: .gobject list <my|playerName> [phaseId|all] — list persistent GOs by creator Battle.net account; owner labels use game-account usernames. Other accounts require staff RBAC 3045.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .gobject list <my|имя> [phaseId|all] — список постоянных GO по Battle.net ID создателя; owner выводится как username игрового аккаунта. Чужие аккаунты требуют RBAC 3045.'),
(11152, 'Syntax: .gobject check <spawnGuid> — show GO details without changing the last targeted GO.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'Синтаксис: .gobject check <spawnGuid> — показать детали GO без смены last targeted GO.')
ON DUPLICATE KEY UPDATE
  `content_default` = VALUES(`content_default`),
  `content_loc8` = VALUES(`content_loc8`);

UPDATE `trinity_string`
SET
  `content_default` = '%s (Entry: %u) - |cffffffff|Hgameobject:%s|h[%s X:%f Y:%f Z:%f MapId:%u]|h|r %s %s',
  `content_loc8` = '%s (Entry: %u) - |cffffffff|Hgameobject:%s|h[%s X:%f Y:%f Z:%f MapId:%u]|h|r %s %s'
WHERE `entry` = 517;
