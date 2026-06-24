-- NobleNext POI — ng_character_poi (original author: ERINGAR)
SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `ng_character_poi` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `owner_guid` bigint unsigned NOT NULL DEFAULT 0,
  `owner_type` tinyint unsigned NOT NULL DEFAULT 1 COMMENT '1=player 2=org 3=system 4=npc',
  `owner_name` varchar(128) NOT NULL DEFAULT '',
  `type` tinyint unsigned NOT NULL DEFAULT 1 COMMENT '1=info 2=story 3=camp 4=tower',
  `x` float NOT NULL DEFAULT 0,
  `y` float NOT NULL DEFAULT 0,
  `map` int unsigned NOT NULL DEFAULT 0,
  `name` varchar(128) NOT NULL DEFAULT '',
  `description` text,
  `icon_key` varchar(32) NOT NULL DEFAULT 'Misc',
  `color_key` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=none 1=alliance 2=horde 3=neutral 4=public 5=other 6=story',
  PRIMARY KEY (`id`),
  KEY `idx_owner` (`owner_guid`),
  KEY `idx_owner_type_name` (`owner_type`, `owner_name`(32)),
  KEY `idx_map` (`map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='NobleNext POI';

ALTER TABLE `ng_character_poi`
  ADD COLUMN IF NOT EXISTS `owner_type` tinyint unsigned NOT NULL DEFAULT 1 COMMENT '1=player 2=org 3=system 4=npc' AFTER `owner_guid`,
  ADD COLUMN IF NOT EXISTS `owner_name` varchar(128) NOT NULL DEFAULT '' AFTER `owner_type`,
  ADD COLUMN IF NOT EXISTS `icon_key` varchar(32) NOT NULL DEFAULT 'Misc' AFTER `description`,
  ADD COLUMN IF NOT EXISTS `color_key` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=none 1=alliance 2=horde 3=neutral 4=public 5=other 6=story' AFTER `icon_key`;

UPDATE `ng_character_poi`
SET `owner_type` = 1
WHERE `owner_guid` > 0 AND `owner_type` = 0;

UPDATE `ng_character_poi` p
LEFT JOIN `characters` c ON c.guid = p.owner_guid
SET p.owner_name = COALESCE(NULLIF(p.owner_name, ''), c.name, '')
WHERE p.owner_guid > 0 AND p.owner_name = '';

UPDATE `ng_character_poi`
SET `icon_key` = CASE `type`
  WHEN 2 THEN 'Story'
  WHEN 3 THEN 'Camp'
  WHEN 4 THEN 'Tower'
  ELSE 'Misc'
END
WHERE `icon_key` = '';

CREATE INDEX IF NOT EXISTS `idx_owner` ON `ng_character_poi` (`owner_guid`);
CREATE INDEX IF NOT EXISTS `idx_owner_type_name` ON `ng_character_poi` (`owner_type`, `owner_name`(32));
CREATE INDEX IF NOT EXISTS `idx_map` ON `ng_character_poi` (`map`);
