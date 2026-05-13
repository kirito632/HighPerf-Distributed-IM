FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y python3 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY build_release/bin/gate_server .
COPY GateServer/GateServer/config.ini ./config_gate.ini
COPY GateServer/entrypoint.sh .
# 复制宿主机运行时库（版本与编译环境完全匹配）
COPY libs/ /usr/local/lib/
RUN ldconfig /usr/local/lib && chmod +x entrypoint.sh gate_server

EXPOSE 8080
ENTRYPOINT ["./entrypoint.sh"]
