-- NobleNext POI — trinity_string для .help poi* (11022–11039)
SET NAMES utf8mb4;

DELETE FROM `trinity_string` WHERE `entry` BETWEEN 11022 AND 11039;

INSERT INTO `trinity_string`
(`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES
(11022, 'NobleNext POI: .poi create [name type desc color] | set apply/name/description|des/position/type/color/owner | update color|icon | delete <id> | sync | /nnpoi', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11023, 'Usage: .poi create [name] [legacyType0-4] [description] [color0-6]. Full: .poi create <owner> <legacyType0-4> <name> <desc> <color0-6>. Quotes and underscores supported.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11024, 'Usage: .poi delete <id>', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11025, 'Usage: .poi sync — отправить все POI на клиент', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11026, 'Usage: .poi set name <id> <текст>', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11027, 'Usage: .poi set description|des <id> <текст> — поддерживает \\n и <b></b>', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11028, 'Usage: .poi set position <id> — позиция = ваша', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11029, 'Usage: .poi set type <id> <1-4>; .poi set color <id> <0-6>; .poi update color|icon <id> <value>', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11030, 'Usage: .poi set owner player <id> [имя_персонажа]', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11031, 'Usage: .poi set owner org <id> <название_организации>', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11032, 'Usage: .poi set owner system <id>', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11033, 'Usage: .poi set owner npc <id> <имя_NPC>', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11034, 'POI создан. Используйте .poi sync или /nnpoi.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11035, 'POI %u удалён.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11036, 'POI синхронизированы с клиентом.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11037, 'POI %u обновлён.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11038, 'POI: тип должен быть 1–4 (1=Информация, 2=Сюжет, 3=Лагерь, 4=Башня).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
(11039, 'Usage: .poi set apply <id> <type1-4> <iconKey> <color0-6> <ownerType1-4> <position0|1> [owner] — пакет: тип, иконка, цвет, владелец, опционально GPS GM.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
