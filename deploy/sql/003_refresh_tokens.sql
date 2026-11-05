CREATE TABLE IF NOT EXISTS `refresh_tokens` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `uid` INT NOT NULL,
  `token_hash` CHAR(64) NOT NULL,
  `family_id` CHAR(32) NOT NULL,
  `expires_at` DATETIME NOT NULL,
  `revoked` TINYINT NOT NULL DEFAULT 0,
  `replaced_by_hash` CHAR(64) NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_refresh_hash` (`token_hash`),
  KEY `idx_refresh_uid` (`uid`),
  KEY `idx_refresh_family` (`family_id`),
  KEY `idx_refresh_expiry` (`expires_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
