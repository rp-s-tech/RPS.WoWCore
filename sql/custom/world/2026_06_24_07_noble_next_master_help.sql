-- NobleNext — additional help strings (11056+)
SET NAMES utf8mb4;

DELETE FROM `trinity_string` WHERE `entry` BETWEEN 11056 AND 11058;

INSERT INTO `trinity_string`
(`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES
(11056, 'NobleNext: .npcroll [player] [1-3] — NPC d20 roll (target NPC required).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .npcroll [игрок] [1-3] — бросок d20 NPC (нужен NPC в цели).'),
(11057, 'NobleNext: .weapon [0-2] — toggle/sheath NPC weapon state.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .weapon [0-2] — переключить состояние оружия NPC.'),
(11058, 'NobleNext: .npcsave — save NPC pose to saved_npc.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .npcsave — сохранить позу NPC в saved_npc.');
