-- Step 5: consumer-side idempotency.
CREATE TABLE `processed_events` (
  `event_id` varchar(128) NOT NULL,
  `event_type` varchar(64) NOT NULL,
  `consumer` varchar(64) NOT NULL,
  `processed_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
