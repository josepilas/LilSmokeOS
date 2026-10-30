# LilSmokeOS — WiFi, Scraping, OTA Server (J6)

This doc covers the J6 features: WiFi configuration, online ROM
metadata scraping via Screenscraper.fr, and the OTA server reference
implementation.

## WiFi

### Architecture

```
┌──────────────────────────────────────────────────────┐
│  LilSmokeUI Settings → WiFi (toggle)                 │
│       │ writes WIFI_ENABLED=1 to lilsmoke.conf       │
│       ▼                                              │
│  /etc/init.d/S20network                             │
│       │ reads lilsmoke.conf                          │
│       │ if WIFI_ENABLED=1: starts wpa_supplicant     │
│       │                              + udhcpc         │
│       ▼                                              │
│  wpa_supplicant -B -i wlan0 -c /persist/config/wifi.conf
│       │                                              │
│       ▼                                              │
│  udhcpc -i wlan0 -t 5 -n -q                         │
│       │                                              │
│       ▼                                              │
│  Network is up — scraper + OTA can reach the internet│
└──────────────────────────────────────────────────────┘
```

### Configuring WiFi from the UI

LilSmokeUI v0.1 doesn't include a WiFi connection wizard (yet). For now,
users connect via the shell:

```sh
# Scan for networks
lilsmoke-wifi scan

# Connect (creates /persist/config/wifi.conf + sets WIFI_ENABLED=1)
lilsmoke-wifi connect "MySSID" "mypassword"

# Check status
lilsmoke-wifi status
# Output: connected: 10.0.0.42

# Disconnect
lilsmoke-wifi disconnect
```

Once connected, the config persists across reboots. `S20network` will
automatically start wpa_supplicant on every boot.

### Configuring WiFi manually

```sh
# Generate a wpa_supplicant.conf entry
wpa_passphrase "MySSID" "mypassword" > /persist/config/wifi.conf

# Edit lilsmoke.conf
vi /persist/config/lilsmoke.conf
# Set: WIFI_ENABLED=1

# Start WiFi now (without reboot)
/etc/init.d/S20network start
```

### Supported WiFi hardware

LilSmokeOS includes kernel modules for the WiFi chips commonly found
in SF3000/GB350 clones:

| Chip | Module | Notes |
|---|---|---|
| RTL8188FU | `r8188fu` | Most common in 2024+ devices |
| RTL8189FS | `r8189fs` | SDIO-attached variant |
| RTL8723BS | `r8723bs` | Older devices |
| RTL8723DS | `r8723ds` | Newer devices |
| ESP8089 | `esp8089` | Some budget devices |

Modules are loaded on-demand by `S20network` when WiFi is enabled.

## ROM scraping (Screenscraper.fr)

LilSmokeOS uses [Screenscraper.fr](https://www.screenscraper.fr) for
ROM metadata and cover art. It's a free service for FOSS projects.

### Setup

1. Register at https://www.screenscraper.fr/forum/register.php
2. Get your API credentials from your profile page
3. Edit `/persist/config/lilsmoke.conf`:
   ```
   WIFI_ENABLED=1
   SS_API_USER=your_username
   SS_API_KEY=your_apikey
   ```
4. Run scraping:
   ```sh
   lilsmoke-scrape --all
   ```

### What gets scraped

For each ROM:

1. **CRC32 hash** (used as cache key)
2. **API query** to `screenscraper.fr/api2/jeuInfos.php` with:
   - System ID (mapped from our system name — see `sysid()` in the script)
   - ROM filename
   - User credentials
3. **Metadata** saved to `/persist/scrape/<hash>.json`:
   - Game title
   - Publisher
   - Release year
   - Cover art URL
4. **Cover art** downloaded to `/persist/scrape/covers/<system>/<rom>.(png|webp|jpg)`

LilSmokeUI's GridView loads PNG, JPEG, and WebP cover art from the
scrape cache on scan.

### Rate limiting

Screenscraper.fr allows 1 request/second for free users. The scraper
sleeps 1 second between ROMs to respect this.

For a 100-ROM library, scraping takes ~2 minutes. Subsequent runs are
instant (cache hits).

### Offline mode

If WiFi is off or no API key is set, the scraper writes stub metadata
files (with `"scraped": false`) so we don't retry on every boot. The
GridView falls back to displaying the ROM filename.

### System ID map

| LilSmokeOS system | Screenscraper ID |
|---|---|
| nes | 3 |
| snes | 4 |
| gb | 9 |
| gbc | 10 |
| gba | 12 |
| md | 1 |
| sms | 2 |
| gg | 23 |
| pce | 31 |
| arcade | 75 |
| lynx | 43 |
| psx | 57 |

Full list at https://www.screenscraper.fr/api2/systemesListe.php

## OTA server

LilSmokeOS ships a reference OTA server implementation in Python at
`tools/ota-server/ota-server.py`. It's a single-file HTTP server with
no external dependencies.

### Setup

```sh
cd tools/ota-server

# Create releases directory
mkdir -p releases/0.1.0

# Copy your built bundle into the release directory
cp /path/to/lilsmoke-update-0.1.0.tar.gz releases/0.1.0/

# Generate manifest.json (sha256, size, etc.)
python3 ota-server.py manifest 0.1.0 --releases ./releases

# Mark as latest
ln -sfn 0.1.0 releases/latest

# Run the server
python3 ota-server.py serve --releases ./releases --port 8080
```

### Endpoints

| Method | Path | Returns |
|---|---|---|
| GET | `/manifest.json` | Latest release's manifest |
| GET | `/lilsmoke-update-<v>.tar.gz` | Bundle download |

### Publishing a new release

```sh
# 1. Build the new FIT image (./scripts/build.sh sf3000 or gb350)
# 2. Create the update bundle:
mkdir -p /tmp/bundle
cp build/buildroot/output/images/lilsmoke.itb /tmp/bundle/
cat > /tmp/bundle/manifest.json <<EOF
{"version": "0.2.0", "min_version": "0.1.0", "release_notes": "J5+J6 added"}
EOF
cd /tmp/bundle && tar czf /tmp/lilsmoke-update-0.2.0.tar.gz .

# 3. Publish
python3 tools/ota-server/ota-server.py publish 0.2.0 \
    --bundle /tmp/lilsmoke-update-0.2.0.tar.gz \
    --notes "J5+J6 added" \
    --releases ./releases

# 4. Restart the server (or use a daemon like systemd)
```

### Production deployment

For production:

1. Put the OTA server behind nginx/apache with TLS:
   ```
   server {
       listen 443 ssl;
       server_name updates.lilsmoke.org;
       location / {
           proxy_pass http://127.0.0.1:8080;
       }
   }
   ```
2. Run it as a systemd service:
   ```ini
   [Unit]
   Description=LilSmokeOS OTA Server
   After=network.target
   
   [Service]
   Type=simple
   User=ota
   WorkingDirectory=/opt/lilsmoke-ota
   ExecStart=/usr/bin/python3 /opt/lilsmoke-ota/ota-server.py serve --releases ./releases
   Restart=always
   
   [Install]
   WantedBy=multi-user.target
   ```
3. Configure LilSmokeOS devices to use it:
   ```sh
   # In /persist/config/lilsmoke.conf:
   LILSMOKE_UPDATE_URL=https://updates.lilsmoke.org
   ```

### Device-side OTA flow

From a LilSmokeOS device:

```sh
# Or use the UI: Settings → OTA Update → Check for updates
lilsmoke-update check       # fetches manifest.json
lilsmoke-update download    # downloads + verifies SHA256
lilsmoke-update install     # writes to inactive slot
lilsmoke-update activate    # marks for next boot
lilsmoke-update reboot      # reboots into new slot
```

Or use the **OTAView** in LilSmokeUI (Settings → OTA Update) which
provides a button-driven interface to the same commands.

### Signing (future)

J6 does NOT sign update bundles. We rely on:
- HTTPS for transport integrity
- SHA256 in the manifest for bundle integrity
- The manifest being served from the same HTTPS endpoint

For stronger security, J6.1 will add Ed25519 signature verification:
- Server signs manifest.json with its private key
- Device verifies with the embedded public key
- Prevents MITM even if HTTPS is compromised

## What's NOT in J6 (deferred)

- **On-device WiFi wizard** — currently shell-only. J6.1 will add a
  proper UI flow with SSID list + password entry (needs on-screen keyboard).
- **Auto-scrape on ROM add** — currently you have to manually run
  `lilsmoke-scrape`. J6.1 may add a udev rule that triggers scraping
  when /storage/roms/ changes.
- **Bundle signing** — see "Signing (future)" above.
- **Delta updates** — full ~25 MB bundle every time. For slow WiFi
  this is painful; delta updates would help. J6.2.
- **Background OTA checks** — currently the user has to manually
  trigger `lilsmoke-update check`. J6.1 may auto-check on WiFi connect.
