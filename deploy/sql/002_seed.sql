USE chat_system_test;

-- Deterministic test accounts: password is Test123456.
INSERT INTO `user` (`uid`, `name`, `email`, `pwd`, `nick`, `icon`, `sex`, `desc`)
VALUES
    (1, 'imtest1', 'imtest1@example.com',
     '9a931c55ac02bf216550c464b1992a30c522dfabf6cb31deada5c716bc13a263',
     'Test One', '', 0, ''),
    (2, 'imtest2', 'imtest2@example.com',
     '9a931c55ac02bf216550c464b1992a30c522dfabf6cb31deada5c716bc13a263',
     'Test Two', '', 0, '')
ON DUPLICATE KEY UPDATE
    `pwd` = VALUES(`pwd`),
    `name` = VALUES(`name`),
    `nick` = VALUES(`nick`);

DELETE FROM `user_id`;
INSERT INTO `user_id` (`id`) VALUES (2);

INSERT INTO `friends` (`uid1`, `uid2`) VALUES (1, 2), (2, 1)
ON DUPLICATE KEY UPDATE `uid1` = VALUES(`uid1`);

-- Keep the test database deterministic across runs.
DELETE FROM `messages`;
DELETE FROM `friend_requests`;
