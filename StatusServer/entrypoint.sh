#!/bin/sh
CONFIG=/app/config_status.ini

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

if cfg.has_section('chatserver1'):
    cfg['chatserver1']['Host'] = os.getenv('CHAT_HOST', '127.0.0.1')

if cfg.has_section('chatserver2'):
    cfg['chatserver2']['Host'] = os.getenv('CHAT2_HOST', '127.0.0.1')

with open('$CONFIG', 'w') as f:
    cfg.write(f)
EOF

exec ./status_server
