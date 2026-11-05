-- Step 2: durable message identity, conversation key, and delivery time.
ALTER TABLE `messages`
  ADD COLUMN `client_msg_id` varchar(64) NULL AFTER `payload`,
  ADD COLUMN `conversation_id` varchar(64) NULL AFTER `client_msg_id`,
  ADD COLUMN `delivered_at` timestamp NULL DEFAULT NULL AFTER `status`,
  ADD UNIQUE KEY `uk_messages_from_client` (`from_uid`, `client_msg_id`),
  ADD KEY `idx_messages_to_cursor` (`to_uid`, `id`);
