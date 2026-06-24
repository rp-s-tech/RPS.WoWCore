-- NobleNext — help strings for GM commands (11040+)
SET NAMES utf8mb4;

DELETE FROM `trinity_string` WHERE `entry` BETWEEN 11040 AND 11055;
INSERT INTO `trinity_string` (`entry`, `content_default`, `locale_koKR`, `locale_frFR`, `locale_deDE`, `locale_zhCN`, `locale_zhTW`, `locale_esES`, `locale_esMX`, `locale_ruRU`) VALUES
(11040, 'NobleNext: .daytime <1-6> — local time-of-day preset for client.', '', '', '', '', '', '', '', 'NobleNext: .daytime <1-6> — пресет времени суток для клиента.'),
(11041, 'NobleNext: .setgrouptime <1-6> — time preset for group.', '', '', '', '', '', '', '', 'NobleNext: .setgrouptime <1-6> — время для группы.'),
(11042, 'NobleNext: .weather <1-4> [0-10] [player] | .weather cancel', '', '', '', '', '', '', '', 'NobleNext: .weather <1-4> [0-10] [имя] | .weather cancel'),
(11043, 'NobleNext: .gobtele <gameobject_guid>', '', '', '', '', '', '', '', 'NobleNext: .gobtele <guid объекта>'),
(11044, 'NobleNext: .wpmove / .wpwalk / .wpwait / .wpemote / .wpclear / .wpgo / .wpstop', '', '', '', '', '', '', '', 'NobleNext: команды маршрута NPC'),
(11045, 'NobleNext: .pet say|emote|stay|follow|play|pos|tele', '', '', '', '', '', '', '', 'NobleNext: управление спутником'),
(11046, 'NobleNext: .sethp / .setarmor / .setbuff / .setdebuff / .wakeup (EBS)', '', '', '', '', '', '', '', 'NobleNext: battle-команды EBS'),
(11047, 'NobleNext: .npcsetstat / .npcsetstatradius / .checknpcstat', '', '', '', '', '', '', '', 'NobleNext: статы NPC'),
(11048, 'NobleNext: .nnstatus / .nn help', '', '', '', '', '', '', '', 'NobleNext: диагностика и справка');
