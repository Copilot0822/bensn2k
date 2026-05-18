# M5 Marine Dashboard

Lightweight React dashboard intended to be built as static files and served by the M5Stamp PLC ESP32 web server.

## Run on this PC

```sh
npm install
npm run dev
```

The current development server is started at:

```txt
http://127.0.0.1:5173/
```

## Build static files

```sh
npm run build
```

The ESP32 can serve the generated `dist/` directory once the firmware file-serving path is wired in.

For the current generic ESP32 test sketch, the built files are staged in:

```txt
../ESP32DashboardTest/data/
```

## Current behavior

- Uses mock marine data shaped like the planned `GET /data` response.
- Polls `GET /data` when hosted on the ESP32 and falls back to browser mock data during PC development.
- Saves page names, page count, widget visibility, widget order, and widget sizes in `localStorage`.
- Saves the pilot control unlock state in `localStorage`.
- Starts with Helm, Sailing, Electrical, and Debug pages, but pages can be renamed, added, or deleted.
- Widgets can be resized from the lower-right handle or from the Layout panel width/height buttons.
- Widget width uses half-column increments, so single-number cards can be set to `W 0.5`.
- Autopilot buttons include stale SeaTalk 1 lockout, tap-again confirmation for mode/tack commands, and a short command cooldown.
