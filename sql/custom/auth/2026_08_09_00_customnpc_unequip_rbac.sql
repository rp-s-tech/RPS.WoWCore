-- Custom NPC unequip RBAC (2113–2116).
-- Equip (2108–2111) was granted to GM role 193, unequip leaf perms were missing →
-- `.cnpc unequip *` invisible / silent-fail while equip worked.
-- Also mirrored in sql/RoleplayCore/1. auth db.sql for fresh installs.

INSERT IGNORE INTO `rbac_permissions` (`id`, `name`) VALUES
(2113, 'Command: .customnpc unequip armor'),
(2114, 'Command: .customnpc unequip left'),
(2115, 'Command: .customnpc unequip ranged'),
(2116, 'Command: .customnpc unequip right');

-- 193 = Role: Sec Level Gamemaster
INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(193, 2113),
(193, 2114),
(193, 2115),
(193, 2116);
