# OpticTrigeminal v3.0.0

**Artificial Cognition Kernel for Native AI Operating Environment**

OpticTrigeminal is an ambitious project that combines a from-scratch C++ AI engine (the "Kernel") with a fully-featured Clinical Healthcare System simulator.

## Project Structure

To maintain a clean and maintainable codebase, the project is divided into distinct components:

- **`src/kernel/`**: The core AI engine. Contains all neural components, the Knowledge Graph, the new Groq API client, embedding logic, and reasoning specializers.
- **`src/clinical/`**: The healthcare simulation layer. Contains the 6-patient live vital simulator, the clinical analyzer, and training scenarios.
- **`src/server/`**: The HTTP and API layer. Contains `http_server.cpp`, `auth_manager.cpp` (which enforces Role-Based Access Control), and the `main.cpp` entrypoint.
- **`src/tools/`**: Command-line interfaces and admin tools.
- **`tests/`**: Integration and stress tests.
- **`docs/`**: Centralized documentation (architecture, audits, and API references).
- **`wasm/`**: WebAssembly bindings (currently in progress for Phase 3).
- **`web/`**: The frontend UI built with Vite and Vue.

## Features

### The AI Kernel
- **16 Neural Components** across 3 development phases
- **Local Inference** with a native Knowledge Graph (10,000+ dynamic nodes)
- **Groq API Integration**: A native C++ client for the Groq API that uses system `curl` to maintain the project's strict "zero external dependencies" rule.
- **Domain-Specific Reasoning** for Mathematics, Logic, and Causality

### The Clinical Healthcare System
- **Real-time Patient Simulation**: Vital signs with crisis triggers for 6 virtual patients.
- **Auto-Charting System**: AI-assisted clinical documentation.
- **Role-Based Access Control (Phase 1.5)**: Secure server-side enforcement. You must sign in to receive an `Authorization: Bearer <token>` before accessing clinical or training endpoints.

### Instructor / Class Cohorts (mass education adoption)
For nursing programs adopting this as a training tool for a whole class, not
one nurse at a time. The `INSTRUCTOR` role (`include/cohort_manager.h`,
`src/server/cohort_manager.cpp`) manages **cohorts** -- named class
rosters:
- Bulk-imports a roster (paste name + email/student ID, one per line) and
  provisions a real, individually-authenticated `RN` account per student,
  returning each one's password exactly once for the instructor to
  distribute.
- Reviews aggregate progress across the cohort: per-student session count
  and average score, a drill-down into each student's individual sessions,
  and a cohort-wide "most-missed interventions" breakdown built from real
  `FAILURE_TRIGGERED` events -- where the class is actually struggling, not
  a guess.
- Staff accounts (including the first `INSTRUCTOR` account for a real
  deployment) and cohorts both persist to disk (`data/staff/`,
  `data/cohorts/`) and survive a server restart -- unlike the 6 demo
  accounts, which intentionally re-seed from `ACMK_*_PASSWORD` env vars
  every boot.
- An `ADMIN` can provision the first staff account of any role (including
  `INSTRUCTOR`) from the Admin dashboard's "Create Staff Account" panel,
  since instructors have no self-service signup.

## Building

### Requirements
- C++17 compatible compiler (clang++ or g++)
- Standard C++ library with threading support
- No external library dependencies, with one deliberate exception: password hashing
  vendors the official Argon2id reference implementation (`third_party/argon2/`, its
  own CC0/Apache-2.0 license — see `third_party/argon2/LICENSE`) rather than hand-
  rolling a memory-hard KDF (uses system `curl` for Groq, which is a subprocess call,
  not a linked dependency)

### Build
```bash
./build.sh
```

### Verifying a change

`./build.sh` only builds the native server + CLI + tests. There's a
**separate, independent** WASM build (`wasm/build.sh`, needs Emscripten) that
backs the in-browser "Edge Diagnostics" inference demo -- `build.sh`
succeeding is not a guarantee the WASM side still compiles or is in sync
with what's actually running in `web/public/optic-trigeminal.wasm`, and this
project's history has more than once shipped native changes with a stale
WASM build nobody re-ran by hand.

```bash
./scripts/verify_all.sh
```

runs the native build, the physiology fuzz test, the integration test
suite, the WASM build (skipped with a warning if Emscripten isn't
installed), and syncs the fresh WASM into `web/public/` + re-embeds it into
the server binary if anything changed. Run this, not just `./build.sh`,
before considering a change to `src/` or `include/` done.

## Running

If you want to use the Groq API integration, you must provide your API key. You can do this by setting an environment variable or creating a `.groq_api_key` file in the root directory:

```bash
echo "gsk_your_api_key_here" > .groq_api_key
```

Then start the server:
```bash
./build/optic-trigeminal
```

Server starts on `http://localhost:8080`

### Auth credentials

There are no hardcoded accounts. On first boot, every seed account
(`ADMIN_001`, `RN_001`, `CHARGE_001`, `PROVIDER_001`, `IT_001`,
`INSTRUCTOR_001`) gets a random password printed once to the server log --
fine for normal use, but inconvenient if you just want to show someone the
app.

**For a demo**, use `./start_demo.sh` instead of running the binary
directly -- it sets every seed account's password to a fixed `demo1234` and
starts the server. The sign-in screen's "Quick Demo Sign-In" buttons are
built for exactly this: one click signs in as any role, no password to
type or dig out of a log file. This is a convenience for local demos, not a
production credential -- don't set `ACMK_*_PASSWORD` to `demo1234` on
anything you're not the only one who can reach.

For a real deployment, set `ACMK_ADMIN_PASSWORD` / `ACMK_RN001_PASSWORD` /
`ACMK_CHARGE001_PASSWORD` / `ACMK_PROVIDER001_PASSWORD` / `ACMK_IT001_PASSWORD` /
`ACMK_INSTRUCTOR001_PASSWORD` yourself (8+ chars) before starting the
server, same as the demo script does under the hood. Any additional staff
accounts you create afterward (via the Admin dashboard, or an instructor's
own roster import) get their own persisted, randomly-generated passwords
instead -- see "Instructor / Class Cohorts" above.

### Optional environment variables

| Variable | Purpose |
|---|---|
| `ACMK_ADMIN_PASSWORD`, `ACMK_RN001_PASSWORD`, `ACMK_CHARGE001_PASSWORD`, `ACMK_PROVIDER001_PASSWORD`, `ACMK_IT001_PASSWORD`, `ACMK_INSTRUCTOR001_PASSWORD` | Set the seed accounts' passwords instead of generating random ones |
| `ACMK_ENABLE_REAL_WORLD` | Set to `1` to allow ACMK-OT sessions to request `mode: "real_world"` (requires an elevated role too). Unset = simulation-only, always |
| `FHIR_BASE_URL` | Base URL of a FHIR R4 server to connect to |
| `FHIR_TOKEN_ENDPOINT`, `FHIR_CLIENT_ID`, `FHIR_CLIENT_SECRET` | OAuth2 client-credentials for the FHIR server. See `include/fhir_client.h` for why this isn't sufficient for a production Epic connection |

## Documentation

For complete API documentation, including the new `/api/auth/sign-in` endpoint, please see:
- [API Reference](docs/API_REFERENCE.md)

For architectural details and audit reports, explore the `docs/` folder.

---

**Status**: Active prototype — see [LIMITATIONS.md](LIMITATIONS.md) for what's real vs. simplified
**Version**: 3.0.0
**Last Updated**: 2026-08-24
