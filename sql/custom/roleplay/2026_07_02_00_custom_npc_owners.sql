-- Custom NPC owners for site/core ownership checks.
SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `custom_npc_owners` (
  `Key` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci NOT NULL,
  `owner_bnet_account_id` int unsigned NOT NULL,
  `owner_alias` varchar(320) NOT NULL DEFAULT '',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`Key`),
  KEY `idx_custom_npc_owners_owner_bnet` (`owner_bnet_account_id`),
  CONSTRAINT `fk_custom_npc_owners_custom_npcs`
    FOREIGN KEY (`Key`) REFERENCES `custom_npcs` (`Key`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
