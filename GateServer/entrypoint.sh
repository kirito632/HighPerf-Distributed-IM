#!/bin/sh
CONFIG=/app/config_gate.ini

python3 - <<EOF
import configparser, os

# 禁止 key 转小写
cfg = configparser.RawConfigParser()
cfg.optionxform = str
cfg.read('$CONFIG')

if cfg.has_section('VerifyServer'):
    cfg['VerifyServer']['Host'] = os.getenv('VERIFY_HOST', '127.0.0.1')
    cfg['VerifyServer']['Port'] = os.getenv('VERIFY_PORT', '50051')

if cfg.has_section('StatusServer'):
    cfg['StatusServer']['Host'] = os.getenv('STATUS_HOST', '127.0.0.1')
    cfg['StatusServer']['Port'] = os.getenv('STATUS_PORT', '50052')

if cfg.has_section('Mysql'):
    cfg['Mysql']['Host'] = os.getenv('MYSQL_HOST', '127.0.0.1')

if cfg.has_section('Redis'):
    cfg['Redis']['Host'] = os.getenv('REDIS_HOST', '127.0.0.1')
    cfg['Redis']['Port'] = os.getenv('REDIS_PORT', '6380')
    cfg['Redis']['Passwd'] = os.getenv('REDIS_PASS', '123456')

with open('$CONFIG', 'w') as f:
    cfg.write(f)
EOF

exec ./gate_server
