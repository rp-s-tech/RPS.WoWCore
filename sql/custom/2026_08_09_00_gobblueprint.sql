-- =============================================================================
-- GobBlueprint — единая production migration (roleplay + auth + world)
-- =============================================================================
-- Применить одним прогоном (имена БД как в worldserver.conf по умолчанию):
--
--   mysql -h HOST -P PORT -u USER -p < sql/custom/2026_08_09_00_gobblueprint.sql
--
-- Если базы называются иначе (например wow_roleplay / wow_auth / wow_world),
-- замените USE ниже или используйте tools/apply_gobblueprint_sql.sh.
--
-- Идемпотентно: DDL через IF NOT EXISTS; RBAC/help — ON DUPLICATE KEY / INSERT IGNORE.
-- =============================================================================

SET NAMES utf8mb4;

-- -----------------------------------------------------------------------------
-- 1) roleplay — шаблоны GO-групп
-- -----------------------------------------------------------------------------
USE `roleplay`;

CREATE TABLE IF NOT EXISTS `gameobject_blueprint` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `owner_account_id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL,
  `description` VARCHAR(255) NOT NULL DEFAULT '',
  `is_public` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `deleted` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_gbp_owner_name` (`owner_account_id`, `name`),
  KEY `idx_gbp_owner_deleted` (`owner_account_id`, `deleted`),
  KEY `idx_gbp_public_deleted` (`is_public`, `deleted`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `gameobject_blueprint_part` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `blueprint_id` INT UNSIGNED NOT NULL,
  `part_type` ENUM('base','object','group') NOT NULL DEFAULT 'base',
  `label` VARCHAR(100) NOT NULL DEFAULT '',
  `source_root_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_gbp_part_blueprint` (`blueprint_id`),
  CONSTRAINT `fk_gbp_part_blueprint`
    FOREIGN KEY (`blueprint_id`) REFERENCES `gameobject_blueprint` (`id`)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- entry=0 + is_root=1 => виртуальный центр (без GO). Ровно один is_root на шаблон.
CREATE TABLE IF NOT EXISTS `gameobject_blueprint_member` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `blueprint_id` INT UNSIGNED NOT NULL,
  `part_id` INT UNSIGNED NOT NULL,
  `sort_order` INT UNSIGNED NOT NULL,
  `is_root` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = virtual center when is_root=1',
  `offset_x` FLOAT NOT NULL DEFAULT 0,
  `offset_y` FLOAT NOT NULL DEFAULT 0,
  `offset_z` FLOAT NOT NULL DEFAULT 0,
  `offset_o` FLOAT NOT NULL DEFAULT 0,
  `rotation0` FLOAT NOT NULL DEFAULT 0,
  `rotation1` FLOAT NOT NULL DEFAULT 0,
  `rotation2` FLOAT NOT NULL DEFAULT 0,
  `rotation3` FLOAT NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_gbp_member_sort` (`blueprint_id`, `sort_order`),
  KEY `idx_gbp_member_part` (`part_id`),
  KEY `idx_gbp_member_root` (`blueprint_id`, `is_root`),
  CONSTRAINT `fk_gbp_member_blueprint`
    FOREIGN KEY (`blueprint_id`) REFERENCES `gameobject_blueprint` (`id`)
    ON DELETE CASCADE,
  CONSTRAINT `fk_gbp_member_part`
    FOREIGN KEY (`part_id`) REFERENCES `gameobject_blueprint_part` (`id`)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- -----------------------------------------------------------------------------
-- 2) auth — RBAC 3055–3066
-- -----------------------------------------------------------------------------
USE `auth`;

INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(3055, 'Command: gobject blueprint'),
(3056, 'Command: gobject blueprint help'),
(3057, 'Command: gobject blueprint list'),
(3058, 'Command: gobject blueprint info'),
(3059, 'Command: gobject blueprint new'),
(3060, 'Command: gobject blueprint update'),
(3061, 'Command: gobject blueprint spawn'),
(3062, 'Command: gobject blueprint delete'),
(3063, 'Command: gobject blueprint rename'),
(3064, 'Command: gobject blueprint set-public'),
(3065, 'Command: gobject blueprint member'),
(3066, 'Command: gobject blueprint staff')
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(197, 3055),
(197, 3056),
(197, 3057),
(197, 3058),
(197, 3059),
(197, 3060),
(197, 3061),
(197, 3062),
(197, 3063),
(197, 3064),
(197, 3065),
(197, 3066),
(193, 3055),
(193, 3056),
(193, 3057),
(193, 3058),
(193, 3059),
(193, 3060),
(193, 3061),
(193, 3062),
(193, 3063),
(193, 3064),
(193, 3065);
-- Staff override (3066) по умолчанию только у роли 197 (GM Commands), не у 193.

-- -----------------------------------------------------------------------------
-- 3) world — trinity_string help 11121–11128, 11153–11155
-- -----------------------------------------------------------------------------
USE `world`;

INSERT INTO `trinity_string`
(`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES
(11121, 'NobleNext: .gobject blueprint — AccountID templates ($Account-$Id), public/private, virtual center, member edit.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint — шаблоны AccountID ($Account-$Id), public/private, виртуальный центр, правка состава.'),
(11122, 'NobleNext: .gobject blueprint list [mine|public|all] [$filter] — discover templates by ACL.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint list [mine|public|all] [$filter] — список шаблонов по ACL.'),
(11123, 'NobleNext: .gobject blueprint info <$key|$name> — header, public flag, parts/members.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint info <$key|$name> — header, public, parts/members.'),
(11124, 'NobleNext: .gobject blueprint new $name — snapshot active GobGroup as new private template.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint new $name — снимок active GobGroup как новый private шаблон.'),
(11125, 'NobleNext: .gobject blueprint update <$key|$name> — overwrite template from active group (template-only).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint update <$key|$name> — перезаписать шаблон из active group (только template).'),
(11126, 'NobleNext: .gobject blueprint spawn <$key|$name> — spawn at player facing as new GobGroup.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint spawn <$key|$name> — поставить у ног/взгляда новой GobGroup.'),
(11127, 'NobleNext: .gobject blueprint delete <$key|$name> — soft-delete template.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint delete <$key|$name> — soft-delete шаблона.'),
(11128, 'NobleNext: .gobject blueprint rename <$key|$name> $new — rename display name.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint rename <$key|$name> $new — переименовать display name.'),
(11153, 'NobleNext: .gobject blueprint set-public <$key|$name> <0|1> — private/public visibility.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint set-public <$key|$name> <0|1> — приватный/публичный доступ.'),
(11154, 'NobleNext: .gobject blueprint member add|remove|replace|setroot|setcenter … — edit template composition / virtual center.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint member add|remove|replace|setroot|setcenter … — состав шаблона / виртуальный центр.'),
(11155, 'NobleNext: staff override for mutating foreign blueprints (RBAC 3066).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: staff override для правки чужих шаблонов (RBAC 3066).')
ON DUPLICATE KEY UPDATE
  `content_default` = VALUES(`content_default`),
  `content_loc8` = VALUES(`content_loc8`);
