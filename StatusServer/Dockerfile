FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y python3 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY build_release/bin/status_server .
COPY StatusServer/StatusServer/config.ini ./config_status.ini
COPY StatusServer/entrypoint.sh .
COPY libs/ /usr/local/lib/
RUN ldconfig /usr/local/lib && chmod +x entrypoint.sh status_server

EXPOSE 50052
ENTRYPOINT ["./entrypoint.sh"]
