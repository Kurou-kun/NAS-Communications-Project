# NAS Communications Project

NAS Communications Project (NASCP) is a **Rainmeter plugin** that retrieves hardware and system metrics from devices running **Prometheus** with **node_exporter**.

It does not require any local monitoring software — all data is pulled remotely from a Prometheus endpoint.

The plugin is designed to provide **lightweight and flexible remote device monitoring** for Rainmeter skins.

---

> [!WARNING]
> **AI ASSISTED DEVELOPMENT**
>
> This project was developed with the assistance of AI tools.
>
> AI systems were used to help with:
> - code generation
> - debugging assistance
> - documentation writing
> - architectural suggestions
>
> All code, design decisions, and final implementations were reviewed and validated by the project author.
>
> AI-generated output may occasionally contain mistakes or inefficient implementations.
> If you encounter any issues, please report them.

---

## Features

- Native Rainmeter plugin
- Remote metric retrieval via Prometheus HTTP API
- No dependency on local monitoring software
- Modular provider architecture
- Shared client instance per endpoint for efficiency
- Async HTTP requests — never blocks Rainmeter
- Reverse proxy support (Traefik, Nginx, etc.)
- Automatic endpoint normalization (hostname, IP, domain)
- Extensible provider system for future backends

---

## Supported Metrics

| Category | Metrics |
|----------|---------|
| General | Status, Uptime, Load |
| CPU | Usage, Temperature, Clock |
| RAM | Percent, Used, Total, Free |
| Swap | Percent, Used, Total, Free |
| Network | Download, Upload, Speed, Download Total, Upload Total |
| Drive | Used Percent, Used, Free, Total, Read Speed, Write Speed, Read Total, Write Total |

---

## Architecture

The plugin uses a modular provider architecture.

```
Rainmeter Measure
      │
      ▼
Plugin.cpp
      │
      ▼
Query::Builder
      │
      ▼
Query::IQuery
      │
      └── Providers::Prometheus::PrometheusQuery
```

Each provider implements the `IQuery` interface and handles its own HTTP communication, query building and response parsing.

---

## Project Structure

```
source/
├── Plugin.h / Plugin.cpp           ← DLL entry, per-measure state
├── Utils/
│   ├── Debug.h / Debug.cpp         ← Rainmeter log integration
│   └── StringUtils.h               ← Input normalization
├── Query/
│   ├── IQuery.h                    ← Provider interface
│   ├── Builder.h / Builder.cpp     ← Factory + endpoint normalization
└── Providers/
    └── Prometheus/
        ├── PrometheusQuery.h
        └── PrometheusQuery.cpp     ← Async HTTP + PromQL + response parsing
```

---

## Installation

### Download Prebuilt Plugin

1. Open the GitHub repository
2. Navigate to **Releases**
3. Download the latest `NASCP.dll`
4. Place the file into the Rainmeter plugins directory:

```
%APPDATA%\Rainmeter\Plugins\
```

5. Refresh Rainmeter

---

## Building from Source

### Requirements

- Visual Studio 2022
- Windows 10 / 11 SDK
- [Rainmeter Plugin SDK](https://github.com/rainmeter/rainmeter-plugin-sdk)

### Build Steps

Clone the repository:

```
git clone https://github.com/Kurou-kun/NASCommunicationsProject.git
cd NASCommunicationsProject
```

Place the Rainmeter SDK files into:

```
external/rainmeter/RainmeterAPI.h
external/rainmeter/Rainmeter.lib
```

Open `NAS Communications Project.sln` in Visual Studio, set configuration to `Release x64` and build.

The compiled plugin will be located in:

```
x64\Release\NASCP.dll
```

Copy the DLL into:

```
%APPDATA%\Rainmeter\Plugins\
```

and refresh Rainmeter.

---

## Basic Usage

Example measure:

```ini
[MeasureCPU]
Measure=Plugin
Plugin=NASCP
URL=prometheus.example.com
Device=myserver
Type=prometheus
Proxy=1
Data=cpu_usage
```

Example meter:

```ini
[MeterCPU]
Meter=String
MeasureName=MeasureCPU
Text=CPU: %1%
NumOfDecimals=1
```

---

## Measure Parameters

### URL

The Prometheus endpoint hostname, IP, or domain.

```
URL=prometheus.example.com
URL=192.168.1.50
URL=192.168.1.50:9090
```

### Device

The Prometheus job name for the target device.

```
Device=myserver
```

### Type

The monitoring backend to use. Currently only `prometheus` is supported.

```
Type=prometheus
```

### Proxy

Set to `1` if Prometheus is behind a reverse proxy (HTTPS on port 443).

```
Proxy=0    ; Direct connection (default)
Proxy=1    ; Reverse proxy / HTTPS
```

### Data

The metric to retrieve. See full list below.

```
Data=cpu_usage
```

### Query

Optional. Overrides `Data=` with a raw PromQL expression.

```
Query=rate(node_cpu_seconds_total{job="myserver",mode="idle"}[1m])
```

### Debug

Debug logging level.

```
Debug=0    ; Errors only (default)
Debug=1    ; Full trace
```

---

## Supported Data Values

### General

| Value | Description | Returns |
|-------|-------------|---------|
| `status` | Device online/offline | `1` / `0` |
| `uptime` | Time since last boot | string (`Xd Xh Xm Xs`) |
| `load` | System load average (1m) | float |

### CPU

| Value | Description | Unit |
|-------|-------------|------|
| `cpu_usage` | CPU utilization | `%` |
| `cpu_temp` | CPU temperature | `°C` |
| `cpu_clock` | Current CPU frequency | `Hz` |

### RAM

| Value | Description | Unit |
|-------|-------------|------|
| `ram_percent` | RAM usage | `%` |
| `ram_used` | RAM used | bytes |
| `ram_total` | RAM total | bytes |
| `ram_free` | RAM available | bytes |

### Swap

| Value | Description | Unit |
|-------|-------------|------|
| `swap_percent` | Swap usage | `%` |
| `swap_used` | Swap used | bytes |
| `swap_total` | Swap total | bytes |
| `swap_free` | Swap free | bytes |

### Network

| Value | Description | Unit |
|-------|-------------|------|
| `net_download` | Download speed | bytes/s |
| `net_upload` | Upload speed | bytes/s |
| `net_speed` | NIC link speed | bytes/s |
| `net_downloadtotal` | Total bytes received | bytes |
| `net_uploadtotal` | Total bytes sent | bytes |

### Drive (/ partition)

| Value | Description | Unit |
|-------|-------------|------|
| `drive_usedpercent` | Disk usage | `%` |
| `drive_used` | Disk used | bytes |
| `drive_free` | Disk free | bytes |
| `drive_total` | Disk total | bytes |
| `drive_readspeed` | Disk read speed | bytes/s |
| `drive_writespeed` | Disk write speed | bytes/s |
| `drive_read` | Total bytes read | bytes |
| `drive_write` | Total bytes written | bytes |

---

## Prometheus Setup

Add your device as a scrape target in `prometheus.yml`:

```yaml
scrape_configs:
  - job_name: "myserver"
    static_configs:
      - targets: ["192.168.1.50:9100"]
        labels:
          device: "myserver"
```

### Recommended node_exporter flags

```yaml
command:
  - "--collector.hwmon"     # CPU temperature
  - "--collector.ethtool"   # NIC speed
  - "--collector.cpu"       # CPU frequency
```

---

## Backend Behavior

All measures pointing to the same endpoint share a **single HTTP client instance**.

This means:

- HTTP requests fire once per update cycle per endpoint
- multiple Rainmeter measures reuse the same cached values
- performance overhead remains minimal regardless of measure count

The polling frequency follows the **Rainmeter skin update rate**.
A minimum interval of **100ms** is enforced to prevent abuse.

---

## Known Limitations

- `net_speed` returns `0` when node_exporter runs inside Docker — use bare metal installation for full NIC speed support
- Drive metrics refer to the `/` partition only (v1.0.0)
- CPU temperature uses `coretemp.*` (Intel) and `pci.*` (AMD) chip selectors automatically

---

## Requirements

- Windows 10 or Windows 11 x64
- Rainmeter 4.0 or newer
- Prometheus with node_exporter on the target device

---

## License

This project is licensed under the **Creative Commons Attribution-ShareAlike 4.0 International License (CC BY-SA 4.0)**.

You are free to:

- **Share** — copy and redistribute the material in any medium or format
- **Adapt** — remix, transform, and build upon the material

Under the following terms:

- **Attribution** — You must give appropriate credit to the original author.
- **ShareAlike** — If you remix, transform, or build upon the material, you must distribute your contributions under the same license.

Full license text:
https://creativecommons.org/licenses/by-sa/4.0/