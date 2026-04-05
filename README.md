# NAS Communications Project (NASCP)

A Rainmeter plugin for monitoring devices via [Prometheus](https://prometheus.io/). Query real-time metrics from any device running [node_exporter](https://github.com/prometheus/node_exporter) and display them directly on your desktop.

---

## Features

- **Real-time metrics** — CPU, RAM, disk, network, uptime and more
- **Async HTTP** — never blocks Rainmeter's UI thread
- **Shared clients** — 50 measures pointing to the same device = 1 HTTP connection
- **Proxy support** — works behind reverse proxies like Traefik
- **Auto device detection** — hostname, IP, or domain all normalize to the same client
- **Debug logging** — built-in debug levels via Rainmeter log
- **Extensible** — provider architecture ready for future backends (Zabbix, SNMP, etc.)

---

## Requirements

- [Rainmeter](https://www.rainmeter.net/) 4.0+
- [Prometheus](https://prometheus.io/) with [node_exporter](https://github.com/prometheus/node_exporter)
- Windows 10/11 x64

---

## Installation

1. Download `NASCP.dll` from [Releases](../../releases)
2. Copy to `%APPDATA%\Rainmeter\Plugins\`
3. Refresh Rainmeter

---

## Usage
```ini
[Measure]
Measure=Plugin
Plugin=NASCP
URL=prometheus.example.com    ; Prometheus endpoint
Device=myserver               ; Prometheus job name
Type=prometheus
Proxy=1                       ; Set to 1 if behind a reverse proxy (HTTPS)
Data=cpu_usage
Debug=0                       ; 0=errors only, 1=full trace
```

---

## Supported Data Types

### General
| Value | Description | Unit |
|-------|-------------|------|
| `status` | Device online/offline | `1` / `0` |
| `uptime` | Time since last boot | `Xd Xh Xm Xs` |
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

### Drive (`/` partition)
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

### Custom
```ini
Data=custom
Query=your_promql_query_here
```

---

## Example Skin
```ini
[Rainmeter]
Update=1000
AccurateText=1

[Variables]
plugin_name=NASCP
api=prometheus

[Measure_State]
Measure=Plugin
Plugin=#plugin_name#
URL=prometheus.kurou.dev
Device=tower
Type=#api#
Data=status
Proxy=1
Substitute="1":"Online","0":"Offline"

[Measure_CPU]
Measure=Plugin
Plugin=#plugin_name#
URL=prometheus.kurou.dev
Device=tower
Type=#api#
Data=cpu_usage
Proxy=1

[Meter_State]
Meter=String
MeasureName=Measure_State
Text=State: %1

[Meter_CPU]
Meter=String
MeasureName=Measure_CPU
Text=CPU: %1%
NumOfDecimals=1
```

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
  - "--collector.hwmon"    # CPU temperature
  - "--collector.ethtool"  # NIC speed
  - "--collector.cpu"      # CPU frequency
```

---

## Known Limitations

- `net_speed` returns `0` when node_exporter runs in Docker — use bare metal for full NIC speed support
- Drive metrics refer to the `/` partition only (v1.0.0)
- CPU temperature uses `coretemp.*` (Intel) and `pci.*` (AMD) chip selectors

---

## Architecture