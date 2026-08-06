-- Dress NPC / custom outfit races: ensure creature_model_info rows exist for player race displays.
-- Safe to re-run. Rows for DisplayIDs missing from CreatureDisplayInfo.db2 are ignored at load time;
-- ObjectMgr::LoadCreatureOutfits synthesizes runtime fallbacks for those cases.
INSERT IGNORE INTO `creature_model_info` (`DisplayID`, `BoundingRadius`, `CombatReach`, `DisplayID_Other_Gender`, `VerifiedBuild`) VALUES
(105540, 0, 0, 0, 0),
(105328, 0, 0, 0, 0),
(105213, 0, 0, 0, 0),
(112642, 0, 0, 0, 0),
(113958, 0, 0, 0, 0),
(114211, 0, 0, 0, 0),
(106003, 0, 0, 0, 0),
(107058, 0, 0, 0, 0),
(107056, 0, 0, 0, 0),
(105268, 0, 0, 0, 0),
(115279, 0, 0, 0, 0),
(115281, 0, 0, 0, 0),
(116539, 0, 0, 0, 0),
(116687, 0, 0, 0, 0),
(126177, 0, 0, 0, 0),
(113609, 0, 0, 0, 0);
