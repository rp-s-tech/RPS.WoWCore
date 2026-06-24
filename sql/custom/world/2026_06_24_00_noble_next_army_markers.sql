-- NobleNext Army Controller — marker NPCs (NobleNextLua/GM/Army)
SET NAMES utf8mb4;

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 1001170 AND 1001174;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 1001170 AND 1001174;

INSERT INTO `creature_template`
(`entry`, `KillCredit1`, `KillCredit2`, `name`, `femaleName`, `subname`, `TitleAlt`, `IconName`,
 `RequiredExpansion`, `VignetteID`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`,
 `Classification`, `dmgschool`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`,
 `unit_class`, `unit_flags`, `unit_flags2`, `unit_flags3`, `family`, `trainer_class`, `type`,
 `VehicleId`, `AIName`, `MovementType`, `ExperienceModifier`, `RacialLeader`, `movementId`,
 `WidgetSetID`, `WidgetSetUnitConditionID`, `RegenHealth`, `CreatureImmunitiesId`, `flags_extra`,
 `ScriptName`, `StringId`, `VerifiedBuild`)
VALUES
(1001170, 0, 0, 'NN Army Run', '', 'Marker', '', '', 0, 0, 35, 0, 1, 1.14286, 0.25,
 0, 0, 0, 0, 1, 1, 1, 0x00000302, 0, 0, 0, 0, 10,
 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0x00000040, 'SmartAI', '', 0),
(1001171, 0, 0, 'NN Army Walk', '', 'Marker', '', '', 0, 0, 35, 0, 1, 1.14286, 0.25,
 0, 0, 0, 0, 1, 1, 1, 0x00000302, 0, 0, 0, 0, 10,
 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0x00000040, 'SmartAI', '', 0),
(1001172, 0, 0, 'NN Army Rotate', '', 'Marker', '', '', 0, 0, 35, 0, 1, 1.14286, 0.25,
 0, 0, 0, 0, 1, 1, 1, 0x00000302, 0, 0, 0, 0, 10,
 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0x00000040, 'SmartAI', '', 0),
(1001173, 0, 0, 'NN Army Select 2yd', '', 'Marker', '', '', 0, 0, 35, 0, 1, 1.14286, 0.25,
 0, 0, 0, 0, 1, 1, 1, 0x00000302, 0, 0, 0, 0, 10,
 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0x00000040, 'SmartAI', '', 0),
(1001174, 0, 0, 'NN Army Select 5yd', '', 'Marker', '', '', 0, 0, 35, 0, 1, 1.14286, 0.25,
 0, 0, 0, 0, 1, 1, 1, 0x00000302, 0, 0, 0, 0, 10,
 0, '', 0, 1, 0, 0, 0, 0, 1, 0, 0x00000040, 'SmartAI', '', 0);

INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES
(1001170, 0, 10045, 1, 1, 0),
(1001171, 0, 10045, 1, 1, 0),
(1001172, 0, 10045, 1, 1, 0),
(1001173, 0, 10045, 1, 1, 0),
(1001174, 0, 10045, 1, 1, 0);
