-- NobleNext — телепорты gameobject (gobtele.lua)
SET NAMES utf8mb4;

CREATE TABLE IF NOT EXISTS `gameobject_teleport` (
  `guid` int unsigned NOT NULL,
  `entry` int unsigned NOT NULL,
  `position_x` float NOT NULL,
  `position_y` float NOT NULL,
  `position_z` float NOT NULL,
  `orientation` float NOT NULL,
  `map` int unsigned NOT NULL,
  `user` int unsigned NOT NULL,
  `phase` int unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='NobleNext gob teleports';
