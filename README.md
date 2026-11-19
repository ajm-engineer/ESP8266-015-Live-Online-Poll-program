# ESP8266 Live Poll

A live audience poll that runs entirely on an ESP-01S (ESP8266) Wi-Fi module. Guests connect with their phones, sign in with a name, vote, and watch the results update in real time. An admin page manages the poll, and a "big screen" page shows live results on a TV or projector. No internet, app or server is required.

## Features

- **Two network modes**
  - Joins an existing Wi-Fi network and serves the poll at `http://livepoll.local`
  - Or broadcasts its own network, where phones get an automatic sign-in pop-up (captive portal)
- **Guests**
  - Sign in with a name and an optional access code
  - One vote per device, changeable while voting is open
  - Live result bars that update every few seconds
- **Admin page** (password protected)
  - Create a poll with up to 6 answers, close or reopen voting, and reset votes
  - Choose whether results show before people vote
  - See signed-in guests, remove one, or sign everyone out
- **Big screen page** with large live results and joining instructions
- **Saves to flash:** the poll and votes survive a reboot
- **Works in light and dark mode**, following the phone's setting

## Hardware

- ESP-01S module (any ESP8266 board with at least 1 MB of flash works)
- USB-to-serial adapter for ESP-01, with a way to hold GPIO0 to GND during upload
- 3.3 V supply. The ESP8266 is not 5 V tolerant.

## Setup

1. Install the ESP8266 board package in the Arduino IDE (Boards Manager → "esp8266").
2. Open `LivePoll/LivePoll.ino`.
3. Tools settings:
   - Board: **Generic ESP8266 Module**
   - Flash Size: an option with a file system, such as **1MB (FS:64KB OTA:~470KB)**. With "FS:none" the poll still runs but resets on reboot.
   - Reset Method: **no dtr (aka ck)**
   - Upload Speed: **115200**
4. Edit the settings at the top of the sketch. At minimum, change `ADMIN_PASS`.
5. Hold GPIO0 to GND while plugging in the adapter, upload, then unplug and plug back in without GPIO0 grounded.
6. Open the Serial Monitor at 115200 baud to see the address, sign-ins and votes.

## Settings

| Setting | Default | Description |
| --- | --- | --- |
| `WIFI_SSID`, `WIFI_PASSWORD` | empty | Wi-Fi network to join. Leave the name empty to always run its own network. |
| `LOCAL_NAME` | `livepoll` | Address on an existing network: `http://livepoll.local` |
| `AP_SSID` | `Live Poll` | Name of its own network |
| `AP_PASSWORD` | empty | Empty for an open network, or 8+ characters for WPA2 |
| `GUEST_CODE` | empty | Access code guests must enter. Empty means name only. |
| `ADMIN_USER`, `ADMIN_PASS` | `admin` / `changeme` | Admin login. Change the password. |
| `PORTAL_TITLE` | `Live Poll` | Title shown on every page |
| `WELCOME_NOTE` | | Note shown to guests on its own network |
| `SESSION_MINUTES` | 240 | How long a sign-in lasts |
| `AP_CHANNEL` | 6 | Wi-Fi channel for its own network |
| `LED_PIN` | 2 | Status LED, blinks on sign-ins and votes. Use -1 to disable. |
| `RELEASE_AFTER_SIGNIN` | `false` | On its own network: `false` keeps the sign-in window open on phones (best for polls); `true` tells phones they're online after sign-in |

## Using it

- **Guests:** join the network (or open `http://livepoll.local` on the shared Wi-Fi), enter a name, and tap an answer.
- **Admin:** open `/admin` in a regular browser tab and sign in. Phones' captive-portal windows often can't show the password prompt.
- **Big screen:** from the admin page, open **Big screen** on a TV or laptop.

## Limitations and security notes

- Everything runs over plain HTTP, so anything typed, including the admin password, crosses the network unencrypted. Don't reuse a password you use elsewhere.
- Its own network is open by default. Set `AP_PASSWORD` if you don't want anyone nearby to join.
- The ESP8266 allows at most 8 devices on its own network at once.
- Sign-ins are kept in memory and reset on reboot. Votes are kept.
- On an existing network:
  - 2.4 GHz only, WPA2 (not WPA3-only)
  - Guest networks with client isolation block devices from reaching the poll
  - Devices are told apart by IP address
- One vote per device is a convenience, not a guarantee. It isn't designed for elections or anything where votes must be secure.

## Disclaimer

This software is provided "as is", without warranty of any kind. The author is not responsible for any damage, loss or misuse resulting from its use. Only run it where you have permission to operate a wireless network, follow your local radio and privacy laws, and don't use it to impersonate other networks or to collect information from people without their consent.

## License

MIT. See [LICENSE](LICENSE).
