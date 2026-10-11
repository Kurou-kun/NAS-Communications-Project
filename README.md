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

### Install the .rmskin (recommended)

1. Download `NASCP_<version>.rmskin` from **Releases**
2. Open it - Rainmeter's skin installer does the rest: it installs the correct
   architecture of the plugin, keeps a copy in `@Vault`, and installs the
   `NASCP` example skin
3. The installer closes and restarts Rainmeter itself

The installer compares the DLL's **FileVersion resource**, not the file name.
Release 1.0.0 shipped a DLL with no version resource at all, which Rainmeter
reads as `0.0.0.0`.

### Manual install

Rename the `.rmskin` to `.zip` and copy `Plugins\64bit\NASCP.dll` (or `32bit`)
into:

```
%APPDATA%\Rainmeter\Plugins\
```

Close Rainmeter first, or it will be holding the old DLL open and the copy
will fail.

---

## Building from Source

### Requirements

- Visual Studio 2022 or newer with the **Desktop development with C++** workload
- Windows 10 / 11 SDK

The Rainmeter API header and both import libraries are vendored under
`external/rainmeter/`, so there is nothing else to download.

### Build Steps

```
git clone https://github.com/Kurou-kun/NAS-Communications-Project.git
cd NAS-Communications-Project

msbuild NASCP.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild NASCP.vcxproj /p:Configuration=Release /p:Platform=Win32
```

The DLLs land in `x64\Release\NASCP.dll` and `Win32\Release\NASCP.dll`. They
link the static CRT, so no Visual C++ redistributable is needed on the target
machine.

To build the installable package:

```
.\tools\build-package.ps1       # -> dist\NASCP_<version>.rmskin
```

The script refuses to package DLLs whose FileVersion or architecture marker
disagrees with `package/RMSKIN.ini`, so bump `NASCP.rc` and `RMSKIN.ini`
together on anything that ships.

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

Which machine to read. The value is matched against the label named by
`DeviceLabel=`.

```
Device=myserver
```

### DeviceLabel

The Prometheus label `Device=` is matched against. Default `job`, the only
label stock node_exporter is guaranteed to carry. Use another one if your
scrape config labels targets itself.

```
DeviceLabel=job       ; default - matches job="myserver"
DeviceLabel=device    ; matches device="myserver"
```

> **If you label targets `device`, mind the collision.** node_exporter already
> uses `device` for NICs, disks and filesystems. Prometheus keeps the target's
> label and renames the metric's to `exported_device`; the plugin follows that
> rename automatically when `DeviceLabel=device`, so the interface filter keeps
> excluding `lo`, `docker*` and `veth*`.

### Mount

Which filesystem the `drive_*` metrics describe. Default `/`.

On a NAS, `/` is the boot pool - point this at a data pool instead:

```
Mount=/
Mount=/mnt/Data/Torrents
```

A host can be read at several mountpoints at once; each `Device` + `Mount`
pair is tracked separately.

### TempChip

`chip=~` regex selecting the hwmon sensor `cpu_temp` reads. Default
`.*coretemp.*|.*k10temp.*|.*zenpower.*|.*pci.*`.

Prometheus regexes are **fully anchored**, so `coretemp.*` does not match a
chip actually named `platform_coretemp_0` - hence the leading `.*`. To find
what your host exposes, query `node_hwmon_temp_celsius` and read the `chip`
label.

```
TempChip=.*coretemp.*
```

### PollInterval

How often the plugin re-queries Prometheus, in milliseconds. Default `2000`,
minimum `250`.

The skin's own `Update=` controls how often the *meter* reads the cached
value; this controls how often the network is touched. Scrape intervals are
typically 15s, so polling much faster mostly costs requests.

```
PollInterval=5000
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

## Reading -1

**`-1` means "no reading", never a value.** It is returned when Prometheus
cannot be reached, when it answers with an HTTP error, or when the query comes
back empty because the exporter does not publish that metric. Zero is reserved
for genuine zeros - an idle NIC reads `0`, an unreachable host reads `-1`.

`status` is the one to watch: `1` scraped and up, `0` scraped and down, `-1`
Prometheus itself is unreachable.

```ini
[MeasureStatus]
Measure=Plugin
Plugin=NASCP
URL=prometheus.example.com
Device=myserver
Proxy=1
Data=status
Substitute="1":"up","0":"down","-1":"unreachable"
```

---

## Supported Data Values

Every value is node_exporter's raw base unit - **Hz**, **bytes**, **bytes per
second**, **seconds**, **degrees C**, percentages as `0-100`. The plugin scales
nothing, so let the skin do it: `AutoScale=1` for byte counts, and a `Calc`
measure dividing by 1e6 to turn `cpu_clock` into MHz.

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

### Drive (the filesystem named by `Mount=`)

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

Each **endpoint + device + mount** combination gets its own client, each with
its own background poll thread and value cache. Measures never touch the
network: they read the cache and return immediately, so a slow or unreachable
Prometheus can never stall Rainmeter's update loop.

Only the metric groups some measure has actually asked for are queried. A skin
reading CPU and RAM costs two HTTP requests per poll; reading everything costs
twelve. Counters and filesystem size/avail are batched into single queries
where PromQL allows it - `rate()` drops the `__name__` label, so rates cannot
be batched the same way.

Polling runs at `PollInterval=` (default 2000ms) independent of the skin's
`Update=`, which only controls how often meters re-read the cache. HTTP
timeouts are 3s per phase.

---

## Known Limitations

- `net_speed` reports what node_exporter sees. Inside Docker that is the
  container's veth, not the host NIC - run node_exporter on the host with
  `--path.rootfs` for real link speeds
- The first poll after a skin loads has not returned yet, so measures read
  `-1` for a moment
- A metric group is only queried once some measure has asked for it, so a
  newly added measure picks up its first value one poll later
- Only `prometheus` is implemented for `Type=`; the provider interface exists
  for others but nothing else ships

---

## Requirements

- Windows 10 or Windows 11 x64
- Rainmeter 4.5.26 or newer
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