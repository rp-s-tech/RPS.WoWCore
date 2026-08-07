SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `gameobject_group_root` (
  `root_guid` bigint unsigned NOT NULL COMMENT 'gameobject.guid root/anchor',
  `name` varchar(100) NOT NULL DEFAULT '',
  `created_by` int unsigned NOT NULL DEFAULT 0 COMMENT 'GM account id',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`root_guid`),
  KEY `idx_gameobject_group_root_name` (`name`(32))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `gameobject_group` (
  `member_guid` bigint unsigned NOT NULL COMMENT 'gameobject.guid member',
  `root_guid` bigint unsigned NOT NULL COMMENT 'gameobject_group_root.root_guid',
  `offset_x` float NOT NULL DEFAULT 0,
  `offset_y` float NOT NULL DEFAULT 0,
  `offset_z` float NOT NULL DEFAULT 0,
  `offset_o` float NOT NULL DEFAULT 0 COMMENT 'orientation relative to root',
  `rotation0` float NOT NULL DEFAULT 0,
  `rotation1` float NOT NULL DEFAULT 0,
  `rotation2` float NOT NULL DEFAULT 0,
  `rotation3` float NOT NULL DEFAULT 1 COMMENT 'model quaternion relative to root yaw',
  PRIMARY KEY (`member_guid`),
  KEY `idx_gameobject_group_root_guid` (`root_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Help overview / subcommands (идемпотентно)
INSERT INTO `trinity_string`
(`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES
(11110, 'NobleNext: .gobject group help|capabilities|create|use|add|scan|addnear|remove|dissolve|delete|info|list|near|target|check|status|capture|recalc|sync|move|turn|relocate|reload|cleanup', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group help|capabilities|create|use|add|scan|addnear|remove|dissolve|delete|info|list|near|target|check|status|capture|recalc|sync|move|turn|relocate|reload|cleanup'),
(11111, 'NobleNext: .gobject group create <root guid> [name] — explicit GO becomes root/active group.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group create <root guid> [name] — указанный GO становится root/active group.'),
(11112, 'NobleNext: .gobject group use|target <group-guid> — select active group (root or member → canonical root).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group use|target <group-guid> — active group (root или member → canonical root).'),
(11113, 'NobleNext: .gobject group add <group-guid> <member-guid> | scan <group-guid> <radius> | addnear <group-guid> <radius> confirm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group add <group-guid> <member-guid> | scan <group-guid> <radius> | addnear <group-guid> <radius> confirm'),
(11114, 'NobleNext: .gobject group remove <group-guid> <member-guid> | dissolve <group-guid> — metadata only | delete <group-guid> full-force — delete every GO.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group remove <group-guid> <member-guid> | dissolve <group-guid> — только metadata | delete <group-guid> full-force — удалить все GO.'),
(11115, 'NobleNext: .gobject group info <object-guid> | list [map] | near [radius] (default 50, DB root position) | check/status <group-guid> | reload', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group info <object-guid> | list [map] | near [radius] (по DB-позиции root, по умолчанию 50) | check/status <group-guid> | reload'),
(11116, 'NobleNext: .gobject group capture <root|member guid> [silent] — refresh relative pose(s). silent = GobMover.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group capture <root|member guid> [silent] — обновить relative pose. silent = GobMover.'),
(11117, 'NobleNext: .gobject group recalc <group-guid> — accept current world layout as offsets; unlock dirty.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group recalc <group-guid> — принять world-расстановку как offsets; снять dirty.'),
(11118, 'NobleNext: .gobject group sync <group-guid> — keep root, realign members from saved offsets.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group sync <group-guid> — root на месте, members по offsets из gameobject_group.'),
(11119, 'NobleNext: .gobject group move <group-guid> [x y z] | turn <group-guid> [o] | relocate <group-guid> <map> <x> <y> <z> <o> confirm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group move <group-guid> [x y z] | turn <group-guid> [o] | relocate <group-guid> <map> <x> <y> <z> <o> confirm'),
(11120, 'NobleNext: .gobject group cleanup confirm — delete orphan group metadata only (never deletes gameobject).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject group cleanup confirm — удалить только orphan metadata (никогда не удаляет gameobject).')
ON DUPLICATE KEY UPDATE
  `content_default` = VALUES(`content_default`),
  `content_loc8` = VALUES(`content_loc8`);

UPDATE `trinity_string`
SET
  `content_default` = '%s (Entry: %u) - |cffffffff|Hgameobject:%s|h[%s X:%f Y:%f Z:%f MapId:%u]|h|r %s %s',
  `content_loc8` = '%s (Entry: %u) - |cffffffff|Hgameobject:%s|h[%s X:%f Y:%f Z:%f MapId:%u]|h|r %s %s'
WHERE `entry` = 517;
