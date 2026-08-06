-- NobleNext — help strings for GM commands (11040+)
SET NAMES utf8mb4;

DELETE FROM `trinity_string` WHERE `entry` BETWEEN 11040 AND 11055;

INSERT INTO `trinity_string`
(`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES
(11040, 'NobleNext: .daytime <1-6> — local time-of-day preset for client.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .daytime <1-6> — пресет времени суток для клиента.'),
(11041, 'NobleNext: .setgrouptime <1-6> — time preset for group.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .setgrouptime <1-6> — время для группы.'),
(11042, 'NobleNext: .weather <1-4> [0-10] [player] | .weather cancel', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .weather <1-4> [0-10] [имя] | .weather cancel'),
(11043, 'NobleNext: .gobject tele|teleport <gameobject_guid> (alias: .gob tele)', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject tele|teleport <guid> (алиас: .gob tele)'),
(11044, 'NobleNext: .wpmove / .wpwalk / .wpwait / .wpemote / .wpclear / .wpgo / .wpstop', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: команды маршрута NPC'),
(11045, 'NobleNext: .pet say|emote|stay|follow|play|pos|tele', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: управление спутником'),
(11046, 'NobleNext: .sethp / .setarmor / .setbuff / .setdebuff / .wakeup (EBS)', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: battle-команды EBS'),
(11047, 'NobleNext: .npcsetstat / .npcsetstatradius / .checknpcstat', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: статы NPC'),
(11048, 'NobleNext: .nnstatus / .nn help', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: диагностика и справка');
