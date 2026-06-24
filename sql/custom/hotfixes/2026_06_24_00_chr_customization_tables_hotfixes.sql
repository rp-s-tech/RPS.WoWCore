--
-- Table structure for table `chr_customization`
--
DROP TABLE IF EXISTS `chr_customization`;
CREATE TABLE `chr_customization` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `Name` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `Sex` tinyint NOT NULL DEFAULT '0',
  `BaseSection` int NOT NULL DEFAULT '0',
  `UiCustomizationType` int NOT NULL DEFAULT '0',
  `Flags` int NOT NULL DEFAULT '0',
  `ComponentSection1` int NOT NULL DEFAULT '0',
  `ComponentSection2` int NOT NULL DEFAULT '0',
  `ComponentSection3` int NOT NULL DEFAULT '0',
  `RaceID` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_locale`
--
DROP TABLE IF EXISTS `chr_customization_locale`;
CREATE TABLE `chr_customization_locale` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `locale` varchar(4) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  `Name_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`locale`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
/*!50500 PARTITION BY LIST  COLUMNS(locale)
(PARTITION deDE VALUES IN ('deDE') ENGINE = InnoDB,
 PARTITION esES VALUES IN ('esES') ENGINE = InnoDB,
 PARTITION esMX VALUES IN ('esMX') ENGINE = InnoDB,
 PARTITION frFR VALUES IN ('frFR') ENGINE = InnoDB,
 PARTITION itIT VALUES IN ('itIT') ENGINE = InnoDB,
 PARTITION koKR VALUES IN ('koKR') ENGINE = InnoDB,
 PARTITION ptBR VALUES IN ('ptBR') ENGINE = InnoDB,
 PARTITION ruRU VALUES IN ('ruRU') ENGINE = InnoDB,
 PARTITION zhCN VALUES IN ('zhCN') ENGINE = InnoDB,
 PARTITION zhTW VALUES IN ('zhTW') ENGINE = InnoDB) */;

--
-- Table structure for table `chr_customization_bone_set`
--
DROP TABLE IF EXISTS `chr_customization_bone_set`;
CREATE TABLE `chr_customization_bone_set` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `BoneFileDataID` int NOT NULL DEFAULT '0',
  `ModelFileDataID` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_category`
--
DROP TABLE IF EXISTS `chr_customization_category`;
CREATE TABLE `chr_customization_category` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `CategoryName` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `CustomizeIcon` int NOT NULL DEFAULT '0',
  `CustomizeIconSelected` int NOT NULL DEFAULT '0',
  `OrderIndex` int NOT NULL DEFAULT '0',
  `CameraZoomLevel` int NOT NULL DEFAULT '0',
  `Flags` int NOT NULL DEFAULT '0',
  `SpellShapeshiftFormID` int NOT NULL DEFAULT '0',
  `CameraDistanceOffset` float NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_category_locale`
--
DROP TABLE IF EXISTS `chr_customization_category_locale`;
CREATE TABLE `chr_customization_category_locale` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `locale` varchar(4) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  `CategoryName_lang` text CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci,
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`locale`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
/*!50500 PARTITION BY LIST  COLUMNS(locale)
(PARTITION deDE VALUES IN ('deDE') ENGINE = InnoDB,
 PARTITION esES VALUES IN ('esES') ENGINE = InnoDB,
 PARTITION esMX VALUES IN ('esMX') ENGINE = InnoDB,
 PARTITION frFR VALUES IN ('frFR') ENGINE = InnoDB,
 PARTITION itIT VALUES IN ('itIT') ENGINE = InnoDB,
 PARTITION koKR VALUES IN ('koKR') ENGINE = InnoDB,
 PARTITION ptBR VALUES IN ('ptBR') ENGINE = InnoDB,
 PARTITION ruRU VALUES IN ('ruRU') ENGINE = InnoDB,
 PARTITION zhCN VALUES IN ('zhCN') ENGINE = InnoDB,
 PARTITION zhTW VALUES IN ('zhTW') ENGINE = InnoDB) */;

--
-- Table structure for table `chr_customization_cond_model`
--
DROP TABLE IF EXISTS `chr_customization_cond_model`;
CREATE TABLE `chr_customization_cond_model` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `CreatureModelDataID` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34081_001_1` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34081_001_2` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34081_001_3` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34081_001_4` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_conversion`
--
DROP TABLE IF EXISTS `chr_customization_conversion`;
CREATE TABLE `chr_customization_conversion` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `ChrRacesID` tinyint NOT NULL DEFAULT '0',
  `Sex` tinyint NOT NULL DEFAULT '0',
  `OptionID` int NOT NULL DEFAULT '0',
  `Data` int NOT NULL DEFAULT '0',
  `ChrCustomizationChoiceID` int NOT NULL DEFAULT '0',
  `DependentOptionID` int NOT NULL DEFAULT '0',
  `DependentData` int NOT NULL DEFAULT '0',
  `Field_3_4_0_45166_007` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_glyph_pet`
--
DROP TABLE IF EXISTS `chr_customization_glyph_pet`;
CREATE TABLE `chr_customization_glyph_pet` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `ChrCustomizationChoiceID` int NOT NULL DEFAULT '0',
  `ChrCustomizationOptionID` int NOT NULL DEFAULT '0',
  `CreatureID` int NOT NULL DEFAULT '0',
  `CreatureID2` int NOT NULL DEFAULT '0',
  `CreatureDisplayInfoID` int NOT NULL DEFAULT '0',
  `CreatureDisplayInfoID2` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_skinned_model`
--
DROP TABLE IF EXISTS `chr_customization_skinned_model`;
CREATE TABLE `chr_customization_skinned_model` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `CollectionsFileDataID` int NOT NULL DEFAULT '0',
  `GeosetType` tinyint NOT NULL DEFAULT '0',
  `GeosetID` int NOT NULL DEFAULT '0',
  `Modifier` int NOT NULL DEFAULT '0',
  `Flags` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_vis_req`
--
DROP TABLE IF EXISTS `chr_customization_vis_req`;
CREATE TABLE `chr_customization_vis_req` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `AllowedDisplayedItemSlots` int NOT NULL DEFAULT '0',
  `Flags` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_customization_voice`
--
DROP TABLE IF EXISTS `chr_customization_voice`;
CREATE TABLE `chr_customization_voice` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `Field_10_0_0_44895_000` tinyint NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
