SET NAMES utf8mb4;

-- GobBlueprint help 11121–11128, 11153–11155. Prefer the bundled prod file:
--   sql/custom/2026_08_09_00_gobblueprint.sql

INSERT INTO `trinity_string`
(`entry`, `content_default`, `content_loc1`, `content_loc2`, `content_loc3`, `content_loc4`, `content_loc5`, `content_loc6`, `content_loc7`, `content_loc8`)
VALUES
(11121, 'NobleNext: .gobject blueprint — AccountID templates ($Account-$Id), public/private, virtual center, member edit.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint — шаблоны AccountID ($Account-$Id), public/private, виртуальный центр, правка состава.'),
(11122, 'NobleNext: .gobject blueprint list [mine|public|all] [$filter] — discover templates by ACL.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint list [mine|public|all] [$filter] — список шаблонов по ACL.'),
(11123, 'NobleNext: .gobject blueprint info <$key|$name> — header, public flag, parts/members.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint info <$key|$name> — header, public, parts/members.'),
(11124, 'NobleNext: .gobject blueprint new $name — snapshot active GobGroup as new private template.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint new $name — снимок active GobGroup как новый private шаблон.'),
(11125, 'NobleNext: .gobject blueprint update <$key|$name> — overwrite template from active group (template-only).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint update <$key|$name> — перезаписать шаблон из active group (только template).'),
(11126, 'NobleNext: .gobject blueprint spawn <$key|$name> — spawn at player facing as new GobGroup.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint spawn <$key|$name> — поставить у ног/взгляда новой GobGroup.'),
(11127, 'NobleNext: .gobject blueprint delete <$key|$name> — soft-delete template.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint delete <$key|$name> — soft-delete шаблона.'),
(11128, 'NobleNext: .gobject blueprint rename <$key|$name> $new — rename display name.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint rename <$key|$name> $new — переименовать display name.'),
(11153, 'NobleNext: .gobject blueprint set-public <$key|$name> <0|1> — private/public visibility.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint set-public <$key|$name> <0|1> — приватный/публичный доступ.'),
(11154, 'NobleNext: .gobject blueprint member add|remove|replace|setroot|setcenter … — edit template composition / virtual center.', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: .gobject blueprint member add|remove|replace|setroot|setcenter … — состав шаблона / виртуальный центр.'),
(11155, 'NobleNext: staff override for mutating foreign blueprints (RBAC 3066).', NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'NobleNext: staff override для правки чужих шаблонов (RBAC 3066).')
ON DUPLICATE KEY UPDATE
  `content_default` = VALUES(`content_default`),
  `content_loc8` = VALUES(`content_loc8`);
