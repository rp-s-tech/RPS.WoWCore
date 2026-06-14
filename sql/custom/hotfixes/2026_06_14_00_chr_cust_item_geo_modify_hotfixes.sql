--
-- Table structure for table `chr_cust_item_geo_modify`
--
DROP TABLE IF EXISTS `chr_cust_item_geo_modify`;
CREATE TABLE `chr_cust_item_geo_modify` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `GeosetType` int NOT NULL DEFAULT '0',
  `Original` int NOT NULL DEFAULT '0',
  `Override` int NOT NULL DEFAULT '0',
  `VerifiedBuild` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`VerifiedBuild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
