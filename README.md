# P25 Control Channel Logger

A lean P25 control channel data logger that decodes every TSBK and MBT message from a P25 trunked radio system and outputs structured NDJSON. No audio recording — just raw control channel intelligence.

Built on top of [trunk-recorder](https://github.com/TrunkRecorder/trunk-recorder)'s OP25/GNU Radio stack, stripped to only what's needed for control channel decoding.

## What It Does

```
RTL-SDR → GNU Radio → OP25 → P25 Parser → NDJSON (stdout)
```

Tunes your SDR to a P25 control channel and logs every decoded message:

- **Voice grants** — who's talking, on what talkgroup, what frequency
- **Unit registrations** — radios powering on/off the system  
- **Group affiliations** — which radio is listening to which talkgroup
- **Patches** — talkgroups linked together
- **System status** — WACN, SysID, RFSS, site ID, adjacent sites
- **Data grants, emergencies, encrypted calls, deny responses, and more**

30+ TSBK/MBT opcodes decoded into machine-parseable JSON.

## Default Target: Alachua County Public Safety

Pre-configured for the Alachua County (Gainesville, FL) P25 system:

| Parameter | Value |
|-----------|-------|
| System | Alachua County Public Safety (formerly GRUCom) |
| Type | P25 Phase I |
| WACN | BEE00 |
| SysID | 4D9 |
| Control Channels | 852.1875, 852.275, 853.1875, 853.675 MHz |
| RadioReference ID | 10471 |

## Quick Start

### Build (on Raspberry Pi or Linux)

```bash
# Install dependencies
sudo apt install gnuradio gnuradio-dev gr-osmosdr libboost-all-dev \
  librtlsdr-dev libssl-dev libcurl4-openssl-dev cmake build-essential \
  libuhd-dev libhackrf-dev libsndfile1-dev

# Build from trunk-recorder root
cd trunk-recorder
mkdir build && cd build
cmake ..
make -j$(nproc) p25-cc-logger

# Run (NDJSON to stdout, logs to stderr)
./p25-cc-logger --config ../p25-cc-logger/config.json
```

### Docker

```bash
# Build (from trunk-recorder root)
docker build -f p25-cc-logger/Dockerfile -t p25-cc-logger .

# Run
docker run --rm --device=/dev/bus/usb \
  -v $(pwd)/p25-cc-logger:/app \
  p25-cc-logger
```

### Convenience Script

```bash
cd trunk-recorder
chmod +x p25-cc-logger/p25-logger.sh
./p25-cc-logger/p25-logger.sh build
./p25-cc-logger/p25-logger.sh run
```

## Output Format

Every line is a self-contained JSON object (NDJSON). Application logs go to stderr, data goes to stdout.

```json
{"ts":"2026-04-03T05:15:23.456Z","sys":"alachua","nac":"0x2C1","type":"GRP_V_CH_GRANT","opcode":"0x00","opcode_name":"GRP_V_CH_GRANT","source":567890,"talkgroup":200,"talkgroup_tag":"ACSO Disp","freq":851.1875,"emergency":false,"encrypted":false,"duplex":false,"mode":false,"priority":3,"tdma":false}
{"ts":"2026-04-03T05:15:23.458Z","sys":"alachua","nac":"0x2C1","type":"GRP_AFF_RSP","opcode":"0x28","opcode_name":"GRP_AFF_RSP","source":567890,"talkgroup":200,"talkgroup_tag":"ACSO Disp"}
{"ts":"2026-04-03T05:15:23.460Z","sys":"alachua","nac":"0x2C1","type":"UNIT_REG_RSP","opcode":"0x2C","opcode_name":"UNIT_REG_RSP","source":567891}
{"ts":"2026-04-03T05:15:23.462Z","sys":"alachua","nac":"0x2C1","type":"NET_STS_BCST","opcode":"0x3B","opcode_name":"NET_STS_BCST","wacn":"0xBEE00","sys_id":"0x4D9","freq":852.1875}
{"ts":"2026-04-03T05:15:24.000Z","sys":"alachua","nac":"0x2C1","type":"_DECODE_RATE","msgs_per_sec":42,"control_freq":852.1875}
```

## jq Recipes

```bash
# Pretty print
p25-cc-logger --config config.json 2>/dev/null | jq .

# Voice grants only (who's talking)
... | jq 'select(.type == "GRP_V_CH_GRANT")'

# Emergency calls
... | jq 'select(.emergency == true)'

# Encrypted traffic
... | jq 'select(.encrypted == true)'

# Unit registrations (radios powering on)
... | jq 'select(.type == "UNIT_REG_RSP")'

# Group affiliations (who's listening to what)
... | jq 'select(.type == "GRP_AFF_RSP") | {source, talkgroup, talkgroup_tag}'

# Talkgroup activity summary
... | jq -r 'select(.talkgroup > 0) | "\(.talkgroup)\t\(.talkgroup_tag // "unknown")"' | sort | uniq -c | sort -rn

# Decode rate health check
... | jq 'select(.type == "_DECODE_RATE")'

# Patches (talkgroup linking)
... | jq 'select(.type == "PATCH_ADD" or .type == "PATCH_DELETE")'
```

## Message Types

| Type | Meaning | Key Fields |
|------|---------|------------|
| `GRP_V_CH_GRANT` | Voice channel grant | talkgroup, source, freq, emergency, encrypted |
| `GRP_V_CH_GRANT_UPDT` | Active call update | talkgroup, freq |
| `GRP_AFF_RSP` | Radio joined talkgroup | source, talkgroup |
| `UNIT_REG_RSP` | Radio registered on system | source |
| `UNIT_DEREG_ACK` | Radio left system | source |
| `ACK_RSP` | System acknowledged radio | source, talkgroup |
| `LOC_REG_RSP` | Location registration | source, talkgroup |
| `UU_V_CH_GRANT` | Private (unit-to-unit) call | source, talkgroup (target) |
| `SNDCP_DATA_CH_GRANT` | Data channel assigned | source, freq |
| `PATCH_ADD` | Talkgroups patched together | patch.supergroup, patch.ga1/ga2/ga3 |
| `PATCH_DELETE` | Patch removed | patch.supergroup |
| `RFSS_STS_BCST` | Site identification | sys_id, rfss, site_id |
| `NET_STS_BCST` | Network status | wacn, sys_id, freq |
| `ADJ_STS_BCST` | Adjacent site | rfss, site_id |
| `CC_BCST` | Control channel broadcast | freq |
| `_DECODE_RATE` | Decode health (internal) | msgs_per_sec |
| `_EVENT` | Retune/error (internal) | event, detail |

## RadioReference Config Generator

If you have a RadioReference Premium account and API key, auto-generate config for any P25 system:

```bash
pip install zeep

python p25-cc-logger/rr_config_gen.py \
  --system-id 10471 \
  --api-key YOUR_API_KEY \
  --username YOUR_RR_USER \
  --password YOUR_RR_PASS \
  --short-name alachua \
  --output-dir p25-cc-logger
```

This pulls control channels, talkgroups, sites, and modulation info directly from RadioReference and writes `config.json` + `talkgroups.csv`.

## Project Structure

```
p25-cc-logger/
├── src/
│   ├── cc_main.cc            # Lean main loop (no recorders, no plugins)
│   ├── cc_logger.cc          # NDJSON message formatter
│   └── cc_logger.h
├── config.json               # Alachua County P25 config (ready to use)
├── talkgroups.csv             # Starter talkgroup list
├── rr_config_gen.py           # RadioReference SOAP API config generator
├── Dockerfile                 # Multi-stage Docker build
└── p25-logger.sh              # Convenience wrapper script
```

## Hardware

- **SDR**: RTL-SDR (default), HackRF, Airspy, or USRP
- **Platform**: Raspberry Pi 3/4/5, any Linux box, or Docker
- **Antenna**: 800 MHz capable antenna for the 851-854 MHz range

## License

Same as [trunk-recorder](https://github.com/TrunkRecorder/trunk-recorder) (GPL-3.0).
