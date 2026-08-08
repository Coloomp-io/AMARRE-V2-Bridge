# AMARRE V2 MQTT / Pub/Sub Bridge

AMARRE V2 Bridge is an MQTT client application that moves messages between a local MQTT broker and Google Cloud Pub/Sub. It is not the MQTT broker.

The current `main` branch remains the Windows-oriented workflow. The Debian ARM64 deployment assets on this branch target a Revolution Pi Connect SE running Debian 12 Bookworm on `aarch64`.

## What The Bridge Does

- MQTT telemetry to Pub/Sub: subscribes to a local MQTT telemetry topic and publishes each payload to a Pub/Sub topic.
- Pub/Sub commands to MQTT: optionally subscribes to a Pub/Sub command subscription and republishes command payloads to an MQTT command topic.

Mosquitto runs separately on the RevPi through systemd. The bridge container uses host networking so `tcp://127.0.0.1:1883` reaches the host Mosquitto broker, not a loopback interface inside the container.

## Repository Layout

- `main.cpp`, `mqtt_callback.h`, `gcp_config.h`, `pubsub_publisher.h`, `pubsub_subscriber.h`: bridge source.
- `CMakeLists.txt`: CMake build.
- `vcpkg.json`: manifest for nlohmann/json, Eclipse Paho MQTT C, and Google Cloud C++ Pub/Sub.
- `config.debian-arm64.example.json`: sanitized Debian runtime configuration.
- `Dockerfile`: multi-stage Debian build/runtime image.
- `deploy/revpi/docker-compose.yml`: RevPi Compose deployment.
- `deploy/revpi/amarre-bridge.env.example`: MQTT password environment-file template.

## Prerequisites On The RevPi

- Debian GNU/Linux 12 Bookworm, ARM64.
- Docker and Docker Compose plugin.
- Mosquitto installed and running through systemd.
- Mosquitto listening on TCP/1883.
- Mosquitto user `amarre_edge` created with a private password.
- `firewalld` allowing TCP/1883 only from the intended LAN, for example `192.168.0.0/24`.
- A fresh least-privilege Google Cloud service account JSON key.

Historical Google Cloud credentials should be treated as compromised if they were ever committed. Do not reuse old keys.

## Google Cloud IAM

Use a fresh service account for the RevPi deployment. Grant only the Pub/Sub permissions required by the enabled bridge directions:

- Telemetry MQTT to Pub/Sub enabled: allow publishing to the telemetry Pub/Sub topic.
- Command Pub/Sub to MQTT enabled: allow consuming from the command Pub/Sub subscription.

Do not commit, print, paste, or bake the service-account JSON into a Docker image.

## Runtime Configuration

Create the runtime directory on the RevPi:

```sh
sudo install -d -m 0750 /etc/amarre-v2
```

Copy and edit the sanitized example:

```sh
sudo cp config.debian-arm64.example.json /etc/amarre-v2/config.json
sudo editor /etc/amarre-v2/config.json
sudo chmod 0644 /etc/amarre-v2/config.json
```

Important: `gcp.project_id` must be the textual Google Cloud project ID, not the numeric project number. For example, use a value like `my-project-id`, not a value like `523949360823`. The deployer must supply the correct textual project ID.

For the RevPi container deployment, keep:

```json
"broker": "tcp://127.0.0.1:1883"
```

The Compose file uses `network_mode: host`, so that address reaches Mosquitto on the RevPi host.

## Secrets

Create the MQTT password environment file on the RevPi:

```sh
sudo cp deploy/revpi/amarre-bridge.env.example /etc/amarre-v2/amarre-bridge.env
sudo editor /etc/amarre-v2/amarre-bridge.env
sudo chmod 0600 /etc/amarre-v2/amarre-bridge.env
```

The file must contain:

```sh
MQTT_PASSWORD=the-private-runtime-password
```

Never commit the real file.

Place the fresh service-account key on the RevPi:

```sh
sudo cp /path/to/fresh-service-account.json /etc/amarre-v2/gcp-service-account.json
sudo chown 65532:65532 /etc/amarre-v2/gcp-service-account.json
sudo chmod 0400 /etc/amarre-v2/gcp-service-account.json
```

The container runs as UID/GID `65532`, so the credential file is readable only by that runtime identity. The Compose file mounts it read-only and sets:

```sh
GOOGLE_APPLICATION_CREDENTIALS=/var/run/secrets/gcp-service-account.json
```

## Build And Start

From the repository root on the RevPi:

```sh
docker compose -f deploy/revpi/docker-compose.yml build
docker compose -f deploy/revpi/docker-compose.yml up -d
```

The image is built for `linux/arm64`, runs as a non-root user, uses a read-only root filesystem, and limits memory to reduce pressure on the RevPi.

## Logs And Health Checks

View logs:

```sh
docker compose -f deploy/revpi/docker-compose.yml logs -f --tail=100
```

Check container restart state:

```sh
docker compose -f deploy/revpi/docker-compose.yml ps
```

Verify Mosquitto separately on the RevPi host:

```sh
systemctl status mosquitto
ss -ltnp | grep ':1883'
```

Because the bridge is an MQTT client, the container exposes no ports.

## Restart And Rollback

Restart after configuration changes:

```sh
docker compose -f deploy/revpi/docker-compose.yml restart
```

Rollback to a previous image or commit by checking out the prior revision, rebuilding, and restarting:

```sh
git checkout <previous-commit>
docker compose -f deploy/revpi/docker-compose.yml build
docker compose -f deploy/revpi/docker-compose.yml up -d
```

## Local Windows Workflow

The existing Windows CMake/vcpkg workflow is intentionally left in place. The Debian deployment uses additional files and runtime environment variables instead of replacing the Windows development path.
