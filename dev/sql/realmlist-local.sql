-- Local realm row (make db-realmlist). Row 1 stays as is: the site reads it for realm status.
-- Copies icon/timezone/gamebuild/Region/Battlegroup from row 1 so the client build matches.
-- worldserver.conf RealmID must point to @REALM_ID@ (make conf does that).
INSERT INTO `@PREFIX@auth`.`realmlist`
  (`id`, `name`, `address`, `localAddress`, `localSubnetMask`, `port`, `icon`, `flag`, `timezone`,
   `allowedSecurityLevel`, `population`, `gamebuild`, `Region`, `Battlegroup`)
SELECT @REALM_ID@, '@REALM_NAME@', '127.0.0.1', '127.0.0.1', '255.255.255.0', 8085, `icon`, 2, `timezone`,
   0, 0, `gamebuild`, `Region`, `Battlegroup`
FROM `@PREFIX@auth`.`realmlist` WHERE `id` = 1
ON DUPLICATE KEY UPDATE `address` = '127.0.0.1', `localAddress` = '127.0.0.1', `port` = 8085,
   `gamebuild` = VALUES(`gamebuild`);
