SET NAMES utf8mb4;

-- GobBlueprint RBAC 3055–3066. Prefer the bundled prod file:
--   sql/custom/2026_08_09_00_gobblueprint.sql

INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(3055, 'Command: gobject blueprint'),
(3056, 'Command: gobject blueprint help'),
(3057, 'Command: gobject blueprint list'),
(3058, 'Command: gobject blueprint info'),
(3059, 'Command: gobject blueprint new'),
(3060, 'Command: gobject blueprint update'),
(3061, 'Command: gobject blueprint spawn'),
(3062, 'Command: gobject blueprint delete'),
(3063, 'Command: gobject blueprint rename'),
(3064, 'Command: gobject blueprint set-public'),
(3065, 'Command: gobject blueprint member'),
(3066, 'Command: gobject blueprint staff')
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

INSERT IGNORE INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(197, 3055),
(197, 3056),
(197, 3057),
(197, 3058),
(197, 3059),
(197, 3060),
(197, 3061),
(197, 3062),
(197, 3063),
(197, 3064),
(197, 3065),
(197, 3066),
(193, 3055),
(193, 3056),
(193, 3057),
(193, 3058),
(193, 3059),
(193, 3060),
(193, 3061),
(193, 3062),
(193, 3063),
(193, 3064),
(193, 3065);
-- Staff override (3066) is intentionally NOT linked to 193 by default; grant via 197 (GM Commands).
