# Google AI Pro 2Y — Desktop Utility (`g2y-gui`)

Compact portrait (`420x640` fixed) standalone C11 + GTK3 desktop frontend wrapping `/home/arvin/.gemini/antigravity/scratch/google-ai-pro-2y`.

## Build & Run

```sh
make
make test
make package
./dist/g2y-gui
```

To target a different local clone: `./dist/g2y-gui --repo /path/to/google-ai-pro-2y` (or set `G2Y_REPO`).

To run the GUI natively at 60 FPS locally while executing scripts on a VPS over an SSH ControlMaster mux + reverse ADB/Frida tunnels:

```sh
ssh -N -M -S /tmp/g2y-vps.sock -o ExitOnForwardFailure=yes \
    -R 15037:127.0.0.1:5037 -R 27042:127.0.0.1:27042 root@YOUR_VPS_IP
./dist/g2y-gui --ssh root@YOUR_VPS_IP --ssh-sock /tmp/g2y-vps.sock --remote-repo ~/google-ai-pro-2y
```

## Controls

- `Plan` selector + `Show` — picks preset (`All Plans`, `Combo Stack`, `Pro 12m`/`+6m`, `Ultra 5x`/`20x`) and runs read-only `scan query` to populate the clickable `DETECTED PLANS` list
- `Setup` — runs `scripts/setup.sh` directly to create `.venv` with `frida==17.22.2` and verify `adb`
- `Check` — runs `scripts/run.sh check` (device, root, `frida-server`, and billing module probe)
- `Plan` — runs `scripts/run.sh combo plan --device [--account EMAIL]` (dry-run verdict and steps)
- `Run` — runs the active preset (`combo run --device --execute --yes`, `step1..4`, or `ultra 5x|20x`)
- `Next Step` — sends Enter (`\n`) to stdin when `combo run` pauses after opening Play checkout on the device
- `Stop` / `Clear` / `Copy` — stops active child process, clears log + detected plans, or copies full log to clipboard
