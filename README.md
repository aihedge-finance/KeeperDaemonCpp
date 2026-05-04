# Curve Keeper Daemon

A C++20 daemon that periodically calls `report()` on Yearn V3 strategies on Ethereum Mainnet.

## Quick Start (Dev Container)

Open this repo in VS Code and choose **"Reopen in Container"**. The container will:
1. Install all system dependencies (cmake, ninja, clangd, gdb, conan).
2. Run `make deps` automatically to install Conan packages.

Then inside the container terminal:

```bash
make build   # CMake configure + compile
make test    # Smoke test (gas threshold = 0, no tx sent)
```

## Deployment (Railway)

1. Push to GitHub and connect the repo to Railway.
2. Railway auto-detects the `Dockerfile` and builds the multi-stage image.
3. Set all environment variables in the Railway dashboard (see table below).

```bash
# Manual Docker test before pushing
docker build -t curve_keeper .
docker run --env-file .env curve_keeper
```

## Environment Variables

> **Note:** Comments must appear on their own line above the key. Inline comments
> (e.g. `KEY=value # comment`) will break Railway's env parser.

| Variable | Required | Default | Description |
|---|---|---|---|
| `ETH_RPC_URL` | ✅ | — | Ethereum mainnet HTTPS RPC endpoint |
| `KEEPER_PRIVATE_KEY` | ✅ | — | 0x-prefixed 32-byte hex private key for the keeper wallet |
| `STRATEGY_ADDRESSES` | ✅ | — | Comma-separated Yearn V3 strategy addresses |
| `HARVEST_INTERVAL_SECONDS` | — | `86400` | Seconds between full harvest cycles |
| `MAX_GAS_PRICE_GWEI` | — | `30` | Skip entire cycle if baseFee exceeds this |
| `MAX_PRIORITY_FEE_GWEI` | — | `2` | EIP-1559 tip per gas in gwei |
| `RETRY_POLICY` | — | `exponential` | `exponential` or `fixed_cooldown` |
| `BASE_RETRY_DELAY_SECONDS` | — | `3600` | (Option A) Initial retry delay |
| `MAX_RETRY_DELAY_SECONDS` | — | `86400` | (Option A) Retry delay cap |
| `MAX_RETRY_ATTEMPTS` | — | `3` | (Option A) Failures before SUSPENDED |
| `RETRY_COOLDOWN_SECONDS` | — | `21600` | (Option B) Fixed cooldown window |
| `MAX_COOLDOWN_CYCLES` | — | `4` | (Option B) Cooldown cycles before SUSPENDED |

## Retry Policies

### Option A: Exponential Backoff (default)

On failure, the strategy enters `COOLDOWN` with a delay that doubles each time:
`1h → 2h → 4h → (SUSPENDED)`

After `MAX_RETRY_ATTEMPTS` consecutive failures the strategy is marked `SUSPENDED`
and will not be retried until the process is restarted.

### Option B: Fixed Cooldown

On failure, the strategy enters a fixed `RETRY_COOLDOWN_SECONDS` cooldown window.
After `MAX_COOLDOWN_CYCLES` consecutive failures it is `SUSPENDED`.

Switch via:
```
RETRY_POLICY=fixed_cooldown
```

## State & Crash Recovery

Per-strategy state is saved to `state.json` after every state transition. On startup
the file is loaded so suspended strategies remain suspended and cooldown timers resume.

> **Railway note:** The filesystem is ephemeral between redeploys. Strategies simply
> resume from `OK` state on a fresh deploy, which is acceptable for a keeper daemon.

## Adding Strategies

Append addresses to `STRATEGY_ADDRESSES` (comma-separated) and restart. New strategies
start in `OK` state; all existing state is preserved.

## Project Structure

```
src/
  main.cpp          — Entry point, signal handling
  config.h/cpp      — Env-var parsing and validation
  eth_client.h/cpp  — Boost.Beast HTTPS JSON-RPC client
  tx_signer.h/cpp   — EIP-1559 RLP encoder + secp256k1 signer + Keccak-256
  state_store.h/cpp — Atomic state.json read/write
  keeper.h/cpp      — Harvest loop, state machine, retry scheduler
```
