-- WORLD database. Enable after combat_world has installed the stat definitions and
-- the matching hotfix spells are available. Run without --force, with writers stopped.
DROP PROCEDURE IF EXISTS rps_enable_dice_20261026;
DELIMITER $$
CREATE PROCEDURE rps_enable_dice_20261026()
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        DROP TEMPORARY TABLE IF EXISTS rps_expected_dice_spells;
        RESIGNAL;
    END;

    CREATE TEMPORARY TABLE rps_expected_dice_spells (
        stat_key varchar(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci PRIMARY KEY,
        spell_id int unsigned NOT NULL UNIQUE
    );
    INSERT INTO rps_expected_dice_spells (stat_key, spell_id) VALUES
        ('accuracy', 1500001), ('healing', 1500002), ('evasion', 1500003),
        ('resistance', 1500004), ('skill', 1500005), ('knowledge', 1500006),
        ('endurance', 1500007), ('craft', 1500008), ('weapon_damage', 1500009),
        ('shield_block', 1500010);

    START TRANSACTION;
    IF EXISTS (SELECT 1 FROM rps_expected_dice_spells e
               LEFT JOIN rps_stat_definitions s ON s.stat_key = e.stat_key
               WHERE s.stat_key IS NULL OR s.spell_id NOT IN (0, e.spell_id)) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Missing dice stat or conflicting operator mapping';
    END IF;
    IF EXISTS (SELECT 1 FROM rps_stat_definitions s
               JOIN rps_expected_dice_spells e ON s.spell_id = e.spell_id
               WHERE s.stat_key <> e.stat_key) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Dice spell ID belongs to another stat';
    END IF;
    UPDATE rps_stat_definitions s JOIN rps_expected_dice_spells e ON e.stat_key = s.stat_key
       SET s.spell_id = e.spell_id WHERE s.spell_id = 0;
    COMMIT;
    DROP TEMPORARY TABLE rps_expected_dice_spells;
END$$
DELIMITER ;
CALL rps_enable_dice_20261026();
DROP PROCEDURE rps_enable_dice_20261026;
