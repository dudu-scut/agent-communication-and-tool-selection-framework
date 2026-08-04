# PR1 Platform / WSL / JSON Gateway Report

## Scope completed

- Added root `docker-compose.yml` with PostgreSQL, Redis, ordered SQL migration job, RPC server, Node JSON-to-gRPC proxy, and an Nginx-served frontend. Internal traffic uses Compose DNS; the proxy is configured with `GRPC_TARGET: rpc-server:50051`.
- Added Dockerfiles for the C++ backend, Node proxy, and frontend, plus frontend Nginx routing for JSON RPC and SPA fallback.
- Retired the old `deploy/docker-compose.gateway.yaml` Envoy deployment. The retained Envoy reference is marked deprecated, no longer uses a host IP, and no longer uses port 8081.
- Made Vite point only to the Node JSON proxy. Added package `test`, `typecheck`, and `lint` scripts; lockfile metadata now matches the moved `@types/file-saver` development dependency.
- Updated root CMake to require 3.20, use `BUILD_TESTING` for GTest/tests, avoid global `CMAKE_CXX_FLAGS`, and keep MCP disabled unless `ENABLE_MCP=ON`.
- Added `scripts/bootstrap-wsl.sh`, which rejects non-WSL2 and `/mnt/*` checkouts, checks/installs the documented Ubuntu packages, and creates local development certificates.
- Updated `run.sh` to use root Compose, avoid `set -e` unsafe post-increments, manage the Node proxy PID/log/listener check, and make setup verify WSL2 plus declared tools/libraries. Removed the absolute-path `scripts/start_backend.sh` and corrected sample Agent registration ports.
- Updated README, CLAUDE.md, `.env.example`, ignore rules, and tracked runtime PID hygiene.

## Test-first evidence

`gateway/proxy/test/platform-contract.test.mjs` was added before implementation. Its first run failed as intended because the root Compose file and WSL bootstrap did not yet exist, Vite still described the deprecated protocol, and `run.sh` had neither a proxy PID nor safe counters. The final focused run passed all 4 checks.

## Verification passed

- `node --test gateway/proxy/test/platform-contract.test.mjs` — 4/4 passed.
- `npm run test && npm run typecheck && npm run lint` in `gateway/proxy` — 9/9 tests passed; syntax/typecheck and lint passed.
- `npm run test && npm run typecheck && npm run lint && npm run build` in `frontend` — passed. The production build completed; Vite retained its pre-existing >500 kB chunk warning.
- `node --check gateway/proxy/server.mjs` — passed.
- `git diff --check` and `git diff --cached --check` — passed.

## Verification blockers

- The Windows host cannot create a WSL Bash instance (`E_ACCESSDENIED`), so `bash -n` and an actual `run.sh`/bootstrap execution could not run here.
- `docker`/Docker Compose and `cmake` are absent on the host, so `docker compose config`, image builds/container smoke tests, and C++ CMake/CTest could not run. The bootstrap/setup checks intentionally surface these prerequisites in WSL2.

## Editing note

The required `apply_patch` tool could add files but could not update tracked files due to its Windows split-root sandbox failure. Per parent-agent direction, scoped UTF-8 unified patches were applied with `git apply` under explicit elevation. `git diff --check` was run afterward.
