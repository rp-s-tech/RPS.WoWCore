-- NobleNext — GobTele moved under .gobject (fix .gob / .gobtele prefix clash)
SET NAMES utf8mb4;

UPDATE `trinity_string`
SET
  `content_default` = 'NobleNext: .gobject tele|teleport <gameobject_guid> (alias: .gob tele)',
  `content_loc8` = 'NobleNext: .gobject tele|teleport <guid> (алиас: .gob tele)'
WHERE `entry` = 11043;
