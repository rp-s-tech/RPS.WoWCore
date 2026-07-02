--
-- Align chr_model column name with current DB2/CSV layout (BarberShopCameraRotationFacing).
--
ALTER TABLE `chr_model`
    CHANGE `BarberShopCameraHeightOffsetScale` `BarberShopCameraRotationFacing` float NOT NULL DEFAULT '0'
    AFTER `BarberShopCameraOffsetScale`;

--
-- Table structure for table `chr_class_ui_chr_model_info`
--
DROP TABLE IF EXISTS `chr_class_ui_chr_model_info`;
CREATE TABLE `chr_class_ui_chr_model_info` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `ChrModel_9_0_1_35522_001Override1` float NOT NULL DEFAULT '0',
  `ChrModel_9_0_1_35522_001Override2` float NOT NULL DEFAULT '0',
  `ChrModel_9_0_1_35522_001Override3` float NOT NULL DEFAULT '0',
  `ChrModelID` int NOT NULL DEFAULT '0',
  `ChrClassesID` tinyint NOT NULL DEFAULT '0',
  `ChrCreateFacingOverride` float NOT NULL DEFAULT '0',
  `Field_11_1_0_58731_004` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_model_material`
--
DROP TABLE IF EXISTS `chr_model_material`;
CREATE TABLE `chr_model_material` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `CharComponentTextureLayoutsID` int unsigned NOT NULL DEFAULT '0',
  `TextureType` int NOT NULL DEFAULT '0',
  `Width` int NOT NULL DEFAULT '0',
  `Height` int NOT NULL DEFAULT '0',
  `Flags` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34615_006` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_model_texture_layer`
--
DROP TABLE IF EXISTS `chr_model_texture_layer`;
CREATE TABLE `chr_model_texture_layer` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `TextureType` int NOT NULL DEFAULT '0',
  `Layer` int NOT NULL DEFAULT '0',
  `Flags` int NOT NULL DEFAULT '0',
  `BlendMode` int NOT NULL DEFAULT '0',
  `TextureSectionTypeBitMask` int NOT NULL DEFAULT '0',
  `TextureSectionTypeBitMask2` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34365_0061` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34365_0062` int NOT NULL DEFAULT '0',
  `Field_9_0_1_34365_0063` int NOT NULL DEFAULT '0',
  `ChrModelTextureTargetID1` int NOT NULL DEFAULT '0',
  `ChrModelTextureTargetID2` int NOT NULL DEFAULT '0',
  `CharComponentTextureLayoutsID` int unsigned NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

--
-- Table structure for table `chr_model_texture_target`
--
DROP TABLE IF EXISTS `chr_model_texture_target`;
CREATE TABLE `chr_model_texture_target` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
