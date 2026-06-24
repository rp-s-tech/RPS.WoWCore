-- NobleNext — NPC role stats (battle system)
SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `creature_role_stats` (
  `guid` int unsigned NOT NULL,
  `STR` int NOT NULL DEFAULT 0,
  `AGI` int NOT NULL DEFAULT 0,
  `INTEL` int NOT NULL DEFAULT 0,
  `VIT` int NOT NULL DEFAULT 0,
  `DEX` int NOT NULL DEFAULT 0,
  `WILL` int NOT NULL DEFAULT 0,
  `SPI` int NOT NULL DEFAULT 0,
  `HEALTH` int NOT NULL DEFAULT 0,
  `ARMOR` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='NobleNext EBS NPC stats';
