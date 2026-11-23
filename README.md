# Hong Kong Weather for the ZECTRIX NOTE4C

Turn the four-colour NOTE4C into a Hong Kong weather display. Choose any of the 18 districts to see a local station’s temperature and the district’s maximum recorded rainfall. The three-day forecast covers Hong Kong as a whole. Weather data comes from the Hong Kong Observatory and requires no API key.

![Weather dashboard preview for Eastern district showing temperature, rainfall, a three-day forecast, a battery percentage and an estimated charging time](docs/dashboard-preview.png)

**Supported hardware**  
ZECTRIX NOTE4C Dev Kit, PCB v1.0, ESP32-S3 N16R8, 400 × 300 SSD2683 four-colour e-paper panel. [Board guide](https://wiki.zectrix.com/en/hardware/note4c/quick-start)

**Getting started**  
Download `NOTE4C-HK-Weather.bin` from the repository’s Releases section and follow the installation steps below. Set up Wi-Fi from your phone, then choose a district or opt in to Wi-Fi-based district detection. Once configured, the board wakes to check the weather and returns to deep sleep.

## Install the compiled firmware

1. Confirm the model says **NOTE4C**. Connect the board with a USB data cable.
2. **Back up the entire 16 MB flash** before changing firmware. The dashboard uses its own partition layout and Wi-Fi settings. The commands below use esptool 4.9.0 in a Python virtual environment. Substitute the board's actual serial port. Check that the resulting file is 16,777,216 bytes and keep it for recovery.
3. Open the ZECTRIX firmware updater in desktop Chrome or Edge, select the serial device, and choose the supplied **NOTE4C-HK-Weather.bin**. For the first installation, clear **Keep device settings** and leave the updater's Wi-Fi fields blank. Confirm the write offset is **0x0000**, install, and restart. [ZECTRIX firmware updater](https://www.zectrix.com/en/firmware-updater.html)

On macOS or Linux, use the following commands for step 2. Run them from the directory where you want to save the backup.

```sh
python3 -m venv .esptool-env
.esptool-env/bin/python -m pip install esptool==4.9.0
.esptool-env/bin/python -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX read_flash 0x0 0x1000000 note4c-factory-backup.bin
wc -c note4c-factory-backup.bin
```

If USB discovery fails, hold the front BOOT button, tap the recessed left-side RESET, and release BOOT. Reconnect and select the port again.

The distributed merged image has SHA-256 `dfcba47d19a8856b484de08a180920e58cedb64b91b426637b9535a2a88e0427`.

## Connect Wi-Fi and use the dashboard

- On first boot, wait for the setup screen. Join the displayed **NOTE4C-…** Wi-Fi network from your phone using the password shown on the panel. Stay connected if the phone reports that the setup network has no internet.
- Open **http://192.168.4.1** in the phone browser. Under **Wi-Fi network**, choose your router from the list of visible 2.4 GHz networks, sorted by signal strength. Enter its password if it has one. Tap **Show** to check what you typed. **Scan again** refreshes the list and preserves your entry if the same network is still visible.
- Under **Weather district**, choose a named district or **Auto-detect using Wi-Fi**. Tap **Connect & save**. The device tests the connection before saving. If it fails, the page keeps the network and district selected so you can re-enter the password. After **Settings saved** appears, reconnect your phone to its usual Wi-Fi.
- Setup runs over local HTTP on the board’s temporary WPA2-protected hotspot. The setup network closes after five minutes. After it closes and the board sleeps, press the lower-right button to reopen setup.
- After setup, allow time for network time synchronization, the two HTTPS requests, and the full e-paper refresh. A full refresh typically takes over ten seconds. [ZECTRIX display notes](https://wiki.zectrix.com/en/hardware/note4c/quick-start)
- The board wakes on Hong Kong clock times, switches off Wi-Fi and the panel rail before sleeping, and skips the full panel refresh when the rendered image is unchanged.
- To change only the district, open setup, leave **Keep saved Wi-Fi** selected, choose your district or Auto, and tap **Save settings**.

### Physical controls

| Button (display facing you) | When to press | Action |
| --- | --- | --- |
| Front circular button | Dashboard asleep | Tap to request both feeds and check the battery now. |
| Lower-right button (Down / Power) | Board asleep | Tap to open the phone setup hotspot. Its name and password appear on the e-paper display. |
| Upper-right button (Up) | Phone setup is waiting | Tap to close setup and discard unsaved edits. With saved Wi-Fi, the board returns to weather. On first setup, the display shows how to try again. |

The upper-right button also opens setup when held during reset. Release it after startup before using it to close setup. Allow a full display refresh when switching screens. The two wake shortcuts are available again once the board sleeps. [ZECTRIX controls and pin map](https://wiki.zectrix.com/en/hardware/note4c/quick-start)

When upgrading from an earlier HKWeather release, enable **Keep device settings** in the updater. A saved manual district is retained. The initial manual selection is **Yau Tsim Mong**. To enable Auto, open setup and save **Auto-detect using Wi-Fi**.

## Network privacy

HKO weather requests use verified HTTPS and contain the feed name and language. District matching happens on the board. The firmware also uses DNS and network time to establish its connections. The services you contact can see the public IP address making the request.

Setup scans are used locally to build the network list. Your home Wi-Fi credentials are stored in the board’s settings flash. A full flash backup made after setup can contain those credentials. The downloadable firmware contains a blank settings partition, and its certificate files are public trust roots.

## Automatic district detection

Auto uses the board's 2.4 GHz Wi-Fi radio and the key-free beaconDB geolocation service. When you select Auto and save, the board sends up to 12 visible access point hardware identifiers (BSSIDs) and their signal strengths over verified HTTPS. Hidden networks and names ending in `_nomap` are excluded. Wi-Fi names, passwords, and precise coordinates are never included in the lookup request. Returned coordinates are matched in memory on the ESP32. Of the location result, the board saves the district index to flash. Selecting a named district disables location lookups. [beaconDB service](https://beacondb.net/) · [Provider privacy notice](https://beacondb.net/privacy/)

At a weather refresh the board checks location if one hour has elapsed since the last successful lookup, or sooner if it has connected through a different access point. After a failed lookup it can retry on the next network wake once 15 minutes have elapsed. The confirmed district selects the station, district rainfall and observation cache. The footer identifies the location mode. When the footer reports an update problem, the location mode appears on the station row.

| Location label | What the dashboard shows |
| --- | --- |
| HKO / AUTO | Weather for the district matched by the most recent successful check. |
| LAST DISTRICT | Weather for the last matched district. The current location could not be confirmed. |
| HKO / NO FIX | Hong Kong-wide conditions and Headquarters temperature until a district can be matched. |
| HKO / HONG KONG | The manually selected district. |

A match needs at least two eligible access points, a working internet connection, and coverage from beaconDB. The firmware disables IP fallback and checks that the reported uncertainty circle lies within a single district, with a maximum accepted radius of 500 metres and a 2-metre rounding margin. If the circle crosses a district boundary or the fix lies outside Hong Kong, the dashboard uses the fallback above. You can select your district manually at any time.

District polygons are derived from the **Home Affairs Department, Government of the Hong Kong Special Administrative Region**. The firmware includes all 18 official polygons rounded to microdegrees. The source project includes the generated coordinates and `tools/make_boundaries.py` to refresh them from the government JSON file. [District boundary dataset](https://data.gov.hk/en-data/dataset/hk-had-json1-hong-kong-administrative-boundaries) · [Source JSON](https://www.had.gov.hk/psi/hong-kong-administrative-boundaries/hksar_18_district_boundary.json)

## Refresh schedule

| Task | Scheduled time in Hong Kong |
| --- | --- |
| Current weather report | Five minutes past each hour |
| Nine-day forecast used for the three-day display | 11:35 and 16:35 daily |
| Battery and display check | At minutes 05, 20, 35 and 50 each hour |
| Network clock synchronization | On first connection with an unset clock, then at the first network check at least six hours after a successful sync |

HKO lists the current report as hourly and its forecast as twice daily, with additional issues possible. The firmware allows time for the feeds to publish. An unscheduled revision normally appears at the next check for that feed. Tap the front button while the board sleeps to check both immediately. [HKO feed frequencies](https://www.hko.gov.hk/en/abouthko/opendata_intro.htm) · [HKO hourly bulletin](https://www.hko.gov.hk/textonly/forecast/englishwx.htm) · [HKO nine-day forecast issue times for its rain probability product](https://www.hko.gov.hk/en/Whats-New/105939/The-Observatory-launches-9-day-probability-of-significant-rain-forecast)

The sleep timer is calculated after rendering, so a slow download or panel refresh does not accumulate as schedule drift. Battery-only checks use saved weather with the radio off. A failed request is retried at the next 15-minute check. A successful response carrying an earlier issue gets one additional check for that release. The device keeps time across deep sleep. With an unset clock, it waits up to 35 seconds for synchronization before falling back to cached data and a 15-minute retry. With a usable clock, a scheduled resync waits up to three seconds.

### If the display says CLOCK UNSYNCED or SET CLOCK IN SETUP

1. On an earlier HKWeather release, tap the front button while the board sleeps to retry. If the message persists, install this release with **Keep device settings** enabled.
2. While the board sleeps, tap the **lower-right button**. Join the displayed setup hotspot from a phone showing the correct time. Open **http://192.168.4.1**, keep **Keep saved Wi-Fi** selected, then tap **Save settings**.
3. When the ESP32 clock is unset, the setup page supplies the phone’s current time as part of the local save. The browser needs JavaScript for this step. After the next successful weather fetch, the display shows the observation time in HKT.

The board tries the Observatory’s network time server first. A network or phone sync also sets the onboard PCF8563 clock. If the ESP32 loses its time, the firmware can restore the clock it previously set while the RTC has power and reports a valid reading. [HKO network time service](https://www.weather.gov.hk/en/nts/ntime.htm) · [NOTE4C hardware guide](https://wiki.zectrix.com/en/hardware/note4c/quick-start)

## Battery and charging

The upper-right corner shows a battery icon filled to the estimated level, alongside the percentage at the latest battery check, including offline checks. It updates on the usual 15-minute cycle. Tap the front button while the dashboard sleeps for an earlier reading. Plug in USB-C when the red **Charge now** reminder appears at 20% or below. The next check shows **Charging** or **Fully charged** when the charger reports a stable state. Dashes beside the battery icon mean a reliable reading was unavailable.

While unplugged, the board learns its discharge rate locally. After at least six hours of readings and a drop of five percentage points, **Charge ~25 Nov 18:00**, for example, means the battery is projected to reach 20% around 6 pm on 25 November, Hong Kong time. The estimate is rounded up to an hour. Screen refreshes, network retries and temperature can change it. Charging, an invalid clock or reading, and gaps over two hours restart the estimate. Projections beyond 30 days are omitted.

The reader uses ten calibrated ADC samples on GPIO4, the board's 2:1 divider and its reference voltage-to-percentage curve. GPIO2 and GPIO1 provide the charging and full signals. The discharge history stays in RTC memory across deep sleep. [ZECTRIX pin map](https://wiki.zectrix.com/en/hardware/note4c/quick-start) · [NOTE4C reference battery reader](https://github.com/LazyYoun/youn-ink-fourcolor-firmware/blob/8907e59300fde3b77a385dba9f3070b0de17b4c8/firmware/main/boards/zectrix-s3-epaper-4.2/zectrix-s3-epaper-4.2.cc#L491-L551)

## Reading the screen

The title shows the selected district. The large number is the temperature at the station named above it. “District max rain” is the maximum rainfall recorded in the district over the displayed HKT interval. Dashes mark a missing reading or a rain gauge maintenance flag. “HQ humidity” is measured at Hong Kong Observatory Headquarters. The conditions and forecast describe the territory. The timestamp at the bottom is the **measurement time**. In the Weather Poster layout, each dated column shows the daily low and high in red, followed by the expected conditions. The large symbol beside the current temperature describes Hong Kong conditions. Humidity appears as dashes if HKO omits the matching measurement. Conditions are short English labels derived from HKO's weather icon codes. [HKO weather icons](https://www.hko.gov.hk/en/textonly/explain/wxicon.htm)

The two feeds update independently. If a request fails, its last successful result is recovered from flash. Each district has a separate observation cache. A red footer identifies a Wi-Fi, clock, or update problem. An observation more than two hours old is marked as an older reading. With a valid clock, expired forecast dates drop off the screen. Without any saved data, unavailable fields are marked explicitly.

## Build from source

Open the project folder containing `platformio.ini` in PlatformIO, or run the commands below with PlatformIO Core installed. On the first build, PlatformIO downloads the pinned platform and JSON library. The target uses Arduino-ESP32 on PlatformIO Espressif32 6.12.0, with 16 MB flash and octal PSRAM selected. The panel driver uses a 30,000-byte packed framebuffer. Wi-Fi credentials are entered on the device.

```sh
pio run -e note4c
pio run -e note4c -t upload --upload-port /dev/cu.usbmodemXXXX
pio device monitor --port /dev/cu.usbmodemXXXX --baud 115200
```

PlatformIO writes the individual build products at their respective offsets. Its generated `firmware.bin` is an application image at **0x10000**. The separately supplied **NOTE4C-HK-Weather.bin** includes the bootloader, partition table, OTA boot data, and application for installation at **0x0000**.

## Implementation and verification

- `src/panel.cpp` drives the power latch on GPIO17 and the panel rail on GPIO6. It uses BUSY 8 (active low), RESET 9, DC 10, CS 11, SCK 12, and MOSI 13. SPI mode 0 sends the black, white, yellow, and red codes in the NOTE4C reference driver's order. The driver powers down the panel after each full refresh and bounds BUSY waits. [Pinned reference driver](https://github.com/LazyYoun/youn-ink-fourcolor-firmware/blob/8907e59300fde3b77a385dba9f3070b0de17b4c8/firmware/main/boards/zectrix-s3-epaper-4.2/custom_lcd_display.cc)
- `src/main.cpp` handles Wi-Fi setup, verified HTTPS, credential and weather storage, timer wake, and the three button shortcuts. `src/weather.cpp` selects the station by name and parses the two feeds. `src/screen.cpp` renders both the dashboard and setup page. [Current observations](https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=rhrread&lang=en) · [Nine-day forecast](https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=fnd&lang=en)
- The release was cross-compiled for ESP32-S3. All 18 station and rainfall-area mappings were checked against a real HKO response. The host tests also covered missing fields, maintenance flags, an unknown icon, and a failed forecast update. The screen preview comes from the same packed framebuffer the firmware sends to the panel. The embedded CA roots were checked against the live HKO endpoint. The district matcher passed 262 generated interior, boundary and uncertainty cases across all 18 districts, with additional tests at published station locations. Host tests also covered movement, cached districts and failed lookups. The beaconDB endpoint was reachable using TLS 1.2 and correctly returned no fix for an empty scan with IP fallback disabled. Simulated setup tests covered the scan list, rescanning, failed passwords, and retaining saved Wi-Fi. Live setup scanning, a successful location fix, physical panel refresh, and battery sleep/wake still need a board test.

The panel reference licence is in NOTE4C-LICENSE.txt. The embedded Barlow Semi Condensed glyph subsets are covered by FONT-LICENSE.txt.
