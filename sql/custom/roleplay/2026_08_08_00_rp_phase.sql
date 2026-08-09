SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `rp_phase` (
  `phase_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `name` VARCHAR(96) NOT NULL,
  `description` VARCHAR(255) NOT NULL DEFAULT '',
  `owner_account_id` INT UNSIGNED NOT NULL,
  `map_id` INT UNSIGNED NULL,
  `visibility_mode` ENUM('exclusive','overlay') NOT NULL DEFAULT 'exclusive',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `is_public` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `spawn_map` INT UNSIGNED NULL DEFAULT NULL,
  `spawn_x` FLOAT NULL DEFAULT NULL,
  `spawn_y` FLOAT NULL DEFAULT NULL,
  `spawn_z` FLOAT NULL DEFAULT NULL,
  `spawn_o` FLOAT NULL DEFAULT NULL,
  `enter_spawn` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `archived_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`phase_id`),
  KEY `idx_rp_phase_owner_name` (`owner_account_id`, `name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `rp_phase_member` (
  `phase_id` BIGINT UNSIGNED NOT NULL,
  `character_guid` BIGINT UNSIGNED NOT NULL,
  `role` ENUM('viewer','editor','manager') NOT NULL DEFAULT 'viewer',
  PRIMARY KEY (`phase_id`, `character_guid`),
  KEY `idx_rp_phase_member_character` (`character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `character_rp_phase` (
  `character_guid` BIGINT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `phase_id` BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (`character_guid`),
  KEY `idx_character_rp_phase_phase` (`phase_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `rp_phase_spawn` (
  `phase_id` BIGINT UNSIGNED NOT NULL,
  `spawn_type` ENUM('creature','gameobject') NOT NULL,
  `spawn_id` BIGINT UNSIGNED NOT NULL,
  `map_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`spawn_type`, `spawn_id`),
  KEY `idx_rp_phase_spawn_phase` (`phase_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `rp_phase_addon_data` (
  `phase_id` BIGINT UNSIGNED NOT NULL,
  `data_key` VARCHAR(96) NOT NULL,
  `data_value` MEDIUMBLOB NOT NULL,
  `version` BIGINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_by` BIGINT UNSIGNED NOT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`phase_id`, `data_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `rp_phase_audit` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `actor_character_guid` BIGINT UNSIGNED NULL,
  `actor_account_id` INT UNSIGNED NULL,
  `action` VARCHAR(64) NOT NULL,
  `phase_id` BIGINT UNSIGNED NULL,
  `detail` JSON NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_rp_phase_audit_phase_created` (`phase_id`, `created_at`),
  KEY `idx_rp_phase_audit_actor_created` (`actor_character_guid`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `gameobject_extra` (
  `guid` BIGINT UNSIGNED NOT NULL,
  `id_creator_bnet` INT UNSIGNED NOT NULL DEFAULT 0,
  `id_creator_player` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `created` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `modified` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`) USING BTREE,
  KEY `idx_gameobject_extra_creator_player` (`id_creator_player`),
  KEY `idx_gameobject_extra_creator_bnet` (`id_creator_bnet`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci ROW_FORMAT=DYNAMIC;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'rp_phase' AND COLUMN_NAME = 'is_public'), 'SELECT 1', 'ALTER TABLE `rp_phase` ADD COLUMN `is_public` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `enabled`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'rp_phase' AND COLUMN_NAME = 'spawn_map'), 'SELECT 1', 'ALTER TABLE `rp_phase` ADD COLUMN `spawn_map` INT UNSIGNED NULL DEFAULT NULL AFTER `is_public`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'rp_phase' AND COLUMN_NAME = 'spawn_x'), 'SELECT 1', 'ALTER TABLE `rp_phase` ADD COLUMN `spawn_x` FLOAT NULL DEFAULT NULL AFTER `spawn_map`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'rp_phase' AND COLUMN_NAME = 'spawn_y'), 'SELECT 1', 'ALTER TABLE `rp_phase` ADD COLUMN `spawn_y` FLOAT NULL DEFAULT NULL AFTER `spawn_x`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'rp_phase' AND COLUMN_NAME = 'spawn_z'), 'SELECT 1', 'ALTER TABLE `rp_phase` ADD COLUMN `spawn_z` FLOAT NULL DEFAULT NULL AFTER `spawn_y`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'rp_phase' AND COLUMN_NAME = 'spawn_o'), 'SELECT 1', 'ALTER TABLE `rp_phase` ADD COLUMN `spawn_o` FLOAT NULL DEFAULT NULL AFTER `spawn_z`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'rp_phase' AND COLUMN_NAME = 'enter_spawn'), 'SELECT 1', 'ALTER TABLE `rp_phase` ADD COLUMN `enter_spawn` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `spawn_o`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

UPDATE `rp_phase_member`
  SET `role` = 'viewer'
  WHERE `role` NOT IN ('viewer', 'builder', 'editor', 'manager');

ALTER TABLE `rp_phase_member`
  MODIFY COLUMN `role` ENUM('viewer','builder','editor','manager') NOT NULL DEFAULT 'viewer';

UPDATE `rp_phase_member`
  SET `role` = 'editor'
  WHERE `role` = 'builder';

ALTER TABLE `rp_phase_member`
  MODIFY COLUMN `role` ENUM('viewer','editor','manager') NOT NULL DEFAULT 'viewer';

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'character_rp_phase' AND COLUMN_NAME = 'account_id'), 'SELECT 1', 'ALTER TABLE `character_rp_phase` ADD COLUMN `account_id` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `character_guid`');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

ALTER TABLE `character_rp_phase`
  MODIFY COLUMN `account_id` INT UNSIGNED NOT NULL;

SET @sql = IF(EXISTS(SELECT 1 FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gameobject_extra' AND INDEX_NAME = 'idx_gameobject_extra_creator_bnet'), 'SELECT 1', 'ALTER TABLE `gameobject_extra` ADD INDEX `idx_gameobject_extra_creator_bnet` (`id_creator_bnet`)');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
