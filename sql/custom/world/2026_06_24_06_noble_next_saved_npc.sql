-- NobleNext — saved NPC pose table (legacy saved_npc)
SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `saved_npc` (
  `entry` int unsigned NOT NULL DEFAULT 0,
  `guid` int unsigned NOT NULL,
  `animation` smallint unsigned NOT NULL DEFAULT 0,
  `size` float NOT NULL DEFAULT 1,
  `mount` int unsigned NOT NULL DEFAULT 0,
  `byte1` tinyint unsigned NOT NULL DEFAULT 0,
  `byte2` tinyint unsigned NOT NULL DEFAULT 0,
  `auras` varchar(64) NOT NULL DEFAULT '0',
  `gm_account_id` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='NobleNext saved NPC pose';
