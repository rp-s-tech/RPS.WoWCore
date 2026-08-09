SET NAMES utf8mb4;

-- GobBlueprint: AccountID ownership, public/private, virtual center (entry=0 root), parts.
-- Spawn has no world back-link. Prefer the bundled prod file:
--   sql/custom/2026_08_09_00_gobblueprint.sql

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

-- entry=0 + is_root=1 => virtual center (no GO). Exactly one is_root per blueprint.
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
