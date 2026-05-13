#!/bin/sh
CONFIG=/app/config_chat1.ini

python3 - <<EOF
import configparser, os

cfg = configparser.RawConfigParser()
cfg.optionxform = str
cfg.read('$CONFIG')

if cfg.has_section('Mysql'):
    cfg['Mysql']['Host'] = os.getenv('MYSQL_HOST', '127.0.0.1')

if cfg.has_section('Redis'):
    cfg['Redis']['Host'] = os.getenv('REDIS_HOST', '127.0.0.1')
    cfg['Redis']['Port'] = os.getenv('REDIS_PORT', '6380')
    cfg['Redis']['Passwd'] = os.getenv('REDIS_PASS', '123456')

if cfg.has_section('StatusServer'):
    cfg['StatusServer']['Host'] = os.getenv('STATUS_HOST', '127.0.0.1')

if cfg.has_section('SelfServer'):
    # gRPC 监听用 0.0.0.0，让容器内所有网卡都能接收连接
    cfg['SelfServer']['Host'] = '0.0.0.0'
    cfg['SelfServer']['Port'] = os.getenv('SELF_PORT', '8090')
    cfg['SelfServer']['RPCPort'] = os.getenv('SELF_RPC_PORT', '50055')

with open('$CONFIG', 'w') as f:
    cfg.write(f)
EOF

exec ./chat_server
