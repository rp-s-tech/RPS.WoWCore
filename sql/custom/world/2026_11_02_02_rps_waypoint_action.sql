-- Fresh install: only rps_waypoint_action is needed.
-- Upgrade: back up the affected tables and stop worldserver. If legacy routes
-- remain, first run contrib/db/legacy/2026_11_02_01_noblenext_native_waypoints.sql.
-- Deploy the matching binary before starting worldserver again.
CREATE TABLE IF NOT EXISTS `rps_waypoint_action` (
  `PathId` int unsigned NOT NULL,
  `NodeId` int unsigned NOT NULL,
  `kind` tinyint unsigned NOT NULL,
  `emote` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`PathId`, `NodeId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELIMITER //
DROP PROCEDURE IF EXISTS rps_retire_legacy_waypoint_tables//
CREATE PROCEDURE rps_retire_legacy_waypoint_tables()
BEGIN
  DECLARE v_legacy bool DEFAULT FALSE;
  DECLARE v_actions bool DEFAULT FALSE;
  DECLARE v_receipts bool DEFAULT FALSE;
  DECLARE v_concat_limit bigint unsigned DEFAULT @@session.group_concat_max_len;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    SET SESSION group_concat_max_len = v_concat_limit;
    RESIGNAL;
  END;

  SELECT EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = 'noblenext_waypoint') INTO v_legacy;
  SELECT EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = 'noblenext_waypoint_action') INTO v_actions;
  SELECT EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = 'noblenext_waypoint_migration') INTO v_receipts;
  IF v_concat_limit < 1048576 THEN
    SET SESSION group_concat_max_len = 1048576;
  END IF;
  START TRANSACTION;
  IF (SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()
    AND table_name IN ('rps_waypoint_action', 'noblenext_waypoint_action') AND engine = 'InnoDB') <> 1 + v_actions THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'RPS: waypoint action tables must use InnoDB';
  END IF;
  IF v_legacy THEN
    IF EXISTS(SELECT 1 FROM noblenext_waypoint) THEN
      IF NOT v_receipts THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'RPS: convert legacy routes before removing their source table';
      END IF;
      IF EXISTS (
        SELECT 1 FROM (
          SELECT creature_guid, COUNT(*) AS actions,
            SHA2(GROUP_CONCAT(CONCAT_WS(':', creature_entry, map_id, point, kind, x, y, z, wait_ms, emote)
              ORDER BY point SEPARATOR '|'), 256) AS source_hash
          FROM noblenext_waypoint GROUP BY creature_guid
        ) s LEFT JOIN noblenext_waypoint_migration m ON m.creature_guid = s.creature_guid
        WHERE m.creature_guid IS NULL OR BINARY m.source_hash <> BINARY s.source_hash OR m.action_count <> s.actions
      ) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'RPS: legacy routes are not verified; run the legacy conversion first';
      END IF;
    END IF;
  END IF;
  IF v_actions THEN
    IF EXISTS (
      SELECT 1 FROM noblenext_waypoint_action src JOIN rps_waypoint_action dst USING (PathId, NodeId)
      WHERE src.kind <> dst.kind OR src.emote <> dst.emote
    ) THEN
      SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'RPS: conflicting waypoint actions; old tables retained';
    END IF;
    INSERT INTO rps_waypoint_action (PathId, NodeId, kind, emote)
      SELECT PathId, NodeId, kind, emote FROM noblenext_waypoint_action
      ON DUPLICATE KEY UPDATE kind = rps_waypoint_action.kind;
    IF EXISTS (
      SELECT 1 FROM noblenext_waypoint_action src LEFT JOIN rps_waypoint_action dst USING (PathId, NodeId)
      WHERE dst.NodeId IS NULL OR src.kind <> dst.kind OR src.emote <> dst.emote
    ) THEN
      SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'RPS: waypoint action copy verification failed';
    END IF;
  END IF;
  IF EXISTS (
    SELECT 1 FROM creature_addon ca JOIN waypoint_path p ON p.PathId = ca.PathId AND p.Comment = 'NobleNext route'
    LEFT JOIN waypoint_path_node n ON n.PathId = p.PathId
    LEFT JOIN rps_waypoint_action a ON a.PathId = n.PathId AND a.NodeId = n.NodeId
    WHERE n.NodeId IS NULL OR a.NodeId IS NULL
  ) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'RPS: native route has missing actions; old tables retained';
  END IF;
  COMMIT;
  SET SESSION group_concat_max_len = v_concat_limit;
  -- DDL follows the verified commit. A failed cleanup can be rerun without
  -- overwriting existing RPS actions. Route coordinates and bindings are untouched.
  DROP TABLE IF EXISTS noblenext_waypoint, noblenext_waypoint_action, noblenext_waypoint_migration;
END//
CALL rps_retire_legacy_waypoint_tables()//
DROP PROCEDURE rps_retire_legacy_waypoint_tables//
DELIMITER ;
