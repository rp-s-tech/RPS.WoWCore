-- CHARACTERS, one-time and OFFLINE: worldserver must be stopped, because the core caches
-- MAX(item_instance.guid) and MAX(mail.id) at startup and would reuse the IDs allocated here.
-- Starter kit v2 adds shirt (1800009), cloak (1800010) and tabard (1800011). New characters receive
-- them natively (world 2026_10_27_07 v2); existing living characters without any of the three get one
-- GM mail with just these items. Requires 2026_10_27_10 (kit v1 mail) to have been applied first.
-- Once the mark exists the script is a no-op, so later deletions of kit items are never refilled.
DROP PROCEDURE IF EXISTS rps_grant_starter_outfit_mail_v2;
DELIMITER //
CREATE PROCEDURE rps_grant_starter_outfit_mail_v2()
BEGIN
  DECLARE item_base BIGINT UNSIGNED;
  DECLARE mail_base BIGINT UNSIGNED;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; DROP TEMPORARY TABLE IF EXISTS rps_starter_targets; RESIGNAL; END;

  START TRANSACTION;
  IF NOT EXISTS (SELECT 1 FROM rps_migration_marks WHERE mark='starter-outfit-mail:1') THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Apply 2026_10_27_10 (starter kit v1 mail) first';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM rps_migration_marks WHERE mark='starter-outfit-mail:2') THEN
    DROP TEMPORARY TABLE IF EXISTS rps_starter_targets;
    CREATE TEMPORARY TABLE rps_starter_targets (
      n INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
      guid BIGINT UNSIGNED NOT NULL UNIQUE
    ) ENGINE=InnoDB;
    INSERT INTO rps_starter_targets (guid)
      SELECT c.guid FROM characters c
      WHERE c.deleteInfos_Name IS NULL
        AND NOT EXISTS (SELECT 1 FROM item_instance ii
          WHERE ii.owner_guid=c.guid AND ii.itemEntry BETWEEN 1800009 AND 1800011)
        AND NOT EXISTS (SELECT 1 FROM rps_action_receipts r
          WHERE r.issuer_guid=c.guid AND r.request_id='native-starter:2')
      ORDER BY c.guid;

    SELECT IFNULL(MAX(guid),0) INTO item_base FROM item_instance;
    SELECT IFNULL(MAX(id),0) INTO mail_base FROM mail;

    INSERT INTO item_instance (guid,itemEntry,owner_guid,count,charges,flags,enchantments,durability,createTime,bonusListIDs)
      SELECT item_base+(t.n-1)*3+k.idx, k.entry, t.guid, 1, '', 1, '', 0, UNIX_TIMESTAMP(), ''
      FROM rps_starter_targets t
      CROSS JOIN (SELECT 1 idx,1800009 entry UNION ALL SELECT 2,1800010 UNION ALL SELECT 3,1800011) k;

    -- stationery 61 = GM; checked 0xA10 = HAS_BODY | NOT_RETURNABLE | NOT_DELETABLE_WITH_ATTACHMENTS.
    INSERT INTO mail (id,messageType,stationery,mailTemplateId,sender,receiver,subject,body,has_items,expire_time,deliver_time,money,cod,checked)
      SELECT mail_base+t.n, 0, 61, 0, 0, t.guid,
        'Стартовая одежда: дополнение',
        'Рубашка, плащ и гербовая накидка к комплекту простой одежды. Предметы не дают обычных характеристик.',
        1, 4294967295, UNIX_TIMESTAMP(), 0, 0, 2576
      FROM rps_starter_targets t;

    INSERT INTO mail_items (mail_id,item_guid,receiver)
      SELECT mail_base+t.n, item_base+(t.n-1)*3+k.idx, t.guid
      FROM rps_starter_targets t
      CROSS JOIN (SELECT 1 idx UNION ALL SELECT 2 UNION ALL SELECT 3) k;

    INSERT INTO rps_action_receipts (issuer_guid,request_id,signature,state,result_kind,result_payload)
      SELECT t.guid, 'native-starter:2', 'starter-outfit:campaign:2', 'done', 'starter_outfit',
        JSON_OBJECT('schema',1,'kit_version',2,'mail',CAST(mail_base+t.n AS CHAR),'items',JSON_ARRAY(
          CAST(item_base+(t.n-1)*3+1 AS CHAR), CAST(item_base+(t.n-1)*3+2 AS CHAR),
          CAST(item_base+(t.n-1)*3+3 AS CHAR)))
      FROM rps_starter_targets t;

    INSERT INTO rps_migration_marks (mark) VALUES ('starter-outfit-mail:2');
    DROP TEMPORARY TABLE rps_starter_targets;
  END IF;
  COMMIT;
END//
DELIMITER ;
CALL rps_grant_starter_outfit_mail_v2();
DROP PROCEDURE rps_grant_starter_outfit_mail_v2;
