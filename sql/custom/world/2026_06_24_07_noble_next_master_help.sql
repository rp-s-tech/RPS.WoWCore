-- NobleNext — additional help strings (11056+)
SET NAMES utf8mb4;

DELETE FROM `trinity_string` WHERE `entry` BETWEEN 11056 AND 11058;
INSERT INTO `trinity_string` (`entry`, `content_default`, `locale_koKR`, `locale_frFR`, `locale_deDE`, `locale_zhCN`, `locale_zhTW`, `locale_esES`, `locale_esMX`, `locale_ruRU`) VALUES
(11056, 'NobleNext: .npcroll [player] [1-3] — NPC d20 roll (target NPC required).', '', '', '', '', '', '', '', 'NobleNext: .npcroll [игрок] [1-3] — бросок d20 NPC (нужен NPC в цели).'),
(11057, 'NobleNext: .weapon [0-2] — toggle/sheath NPC weapon state.', '', '', '', '', '', '', '', 'NobleNext: .weapon [0-2] — переключить состояние оружия NPC.'),
(11058, 'NobleNext: .npcsave — save NPC pose to saved_npc.', '', '', '', '', '', '', '', 'NobleNext: .npcsave — сохранить позу NPC в saved_npc.');
