ALTER TABLE `outbox`
  ADD COLUMN `traceparent` varchar(55) NULL AFTER `payload`;
