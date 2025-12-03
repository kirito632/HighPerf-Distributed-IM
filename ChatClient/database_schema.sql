-- 好友系统数据库表结构
-- 请在你的MySQL数据库中执行以下SQL语句

-- 1. 好友申请表
CREATE TABLE IF NOT EXISTS friend_requests (
    id INT AUTO_INCREMENT PRIMARY KEY,
    from_uid INT NOT NULL,
    to_uid INT NOT NULL,
    desc TEXT,
    status TINYINT DEFAULT 0 COMMENT '0:待处理 1:已同意 2:已拒绝',
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_to_uid (to_uid),
    INDEX idx_from_uid (from_uid),
    INDEX idx_status (status)
);

-- 2. 好友关系表
CREATE TABLE IF NOT EXISTS friends (
    id INT AUTO_INCREMENT PRIMARY KEY,
    uid1 INT NOT NULL,
    uid2 INT NOT NULL,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY unique_friendship (uid1, uid2),
    INDEX idx_uid1 (uid1),
    INDEX idx_uid2 (uid2)
);

-- 3. 确保用户表有必要的字段（如果还没有的话）
-- 注意：这些字段可能已经存在于你的user表中
ALTER TABLE user ADD COLUMN IF NOT EXISTS nick VARCHAR(50) DEFAULT '';
ALTER TABLE user ADD COLUMN IF NOT EXISTS icon VARCHAR(255) DEFAULT '';
ALTER TABLE user ADD COLUMN IF NOT EXISTS sex TINYINT DEFAULT 0;
ALTER TABLE user ADD COLUMN IF NOT EXISTS desc TEXT;

-- 4. 插入一些测试数据（可选）
-- 假设你已经有一些用户数据，这里插入一些好友申请和好友关系的测试数据

-- 插入好友申请测试数据（请根据你的实际用户ID调整）
-- INSERT INTO friend_requests (from_uid, to_uid, desc, status) VALUES 
-- (2, 1, '你好，我想加你为好友', 0),
-- (3, 1, '我们认识一下', 0);

-- 插入好友关系测试数据（请根据你的实际用户ID调整）
-- INSERT INTO friends (uid1, uid2) VALUES 
-- (1, 4),
-- (4, 1),
-- (1, 5),
-- (5, 1);
