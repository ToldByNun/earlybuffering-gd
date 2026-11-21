# Independent AI architecture (clean-room)

**Purpose:** Design a private Geode mod AI/assist stack that **does not** depend on Nova’s authentication service, credentials, session tokens, or proprietary model weights.

**Basis:** Architecture observations from the read-only Nova study (call graphs, host roles, gating) plus EarlyBuffer’s existing modules. This is a **design** document — not a patch plan for `alquerez.nova.dll`.

**Hard constraints**

- Original C++ under EarlyBuffer / frirecracker conventions (`AGENTS.md`).
- Independently trained or user-supplied models only.
- Local settings and model files under the mod’s Geode save/config dirs.
- No Nova Workers login/account protocol.
- No reuse of Nova credentials, tokens, or `model.bin` / `NVM2` weights.
- No modification of Nova’s DLL and no hooks whose purpose is to defeat Nova license checks.

---

## 1. Design principle

Treat authentication as **orthogonal product infrastructure**, not as part of the physics/search/neural loop.

```
┌─────────────────────────────────────────────────────────┐
│ Geode entry (early_buffer_module.cpp) — thin            │
└──────────────────────────┬──────────────────────────────┘
                           │
     ┌─────────────────────▼─────────────────────┐
     │ Config / StageConfig / mod.json settings  │  local only
     └─────────────────────┬─────────────────────┘
                           │
     ┌─────────────────────▼─────────────────────┐
     │ Assist controller                         │
     │  scope → plan → arm → fire                │
     └───────┬─────────────────────┬─────────────┘
             │                     │
   ┌─────────▼─────────┐ ┌─────────▼──────────┐
   │ Shadow simulator  │ │ Optional neural    │
   │ planning/search   │ │ candidate scorer   │
   └─────────┬─────────┘ └─────────┬──────────┘
             │                     │
             └──────────┬──────────┘
                        │
              ┌─────────▼─────────┐
              │ GD hooks (PlayLayer│
              │ / UILayer / pause) │
              └───────────────────┘

Optional (non-auth): cloud config catalog with explicit trust policy
```

Nova couples “identity → CDN model → unlock UI”. EarlyBuffer should keep **identity absent** and **models local**.

---

## 2. Component designs

### 2.1 Module initialization

**Use documented Geode interfaces**

- `GEODE_MOD` / `$execute` / `$on_mod(Loaded)` for registration.
- Keep `src/early_buffer_module.cpp` as a thin delegator (existing pattern).
- Static init only for hook tables and default config — never for network login.

**Do not implement**

- Login-key POST to third-party Workers.
- Account password capture UI for a remote license server.
- DPAPI wrappers whose only purpose is storing foreign `acct-token` material.

### 2.2 Configuration management

| Need | Independent approach |
|---|---|
| User toggles | `mod.json` settings + `StageConfig` cache (`src/config/`) |
| Per-level learning | Existing Hybrid / AI learning stores under mod save dir |
| Import/export | Bundle codec (`core/bundle/`) and optional cloud catalog with **user-trusted** hosts only (`src/cloud/`) |

Integrity for optional downloads: keep SHA-256 / host allowlists already sketched in `cloud/transport` — that is **content integrity**, not user authentication to Nova.

### 2.3 Shadow simulation

Reuse and extend EarlyBuffer’s simulator:

- `core/sim/shadow_simulator` (+ `runtime/`, `world/`, `evaluation/`).
- Ghost ownership markers already documented in `docs/dev/architecture.md`.
- Contracts: survival, delay verify, frozen eval — unchanged conceptually.

This maps to the *role* Nova’s `TrajectoryBufferSim` plays, implemented with **original** code and GD bindings (Geode `$modify`, public member docs for 2.2081).

### 2.4 Candidate evaluation and route planning

| Piece | Independent home |
|---|---|
| Live / Adaptive / IW search | `core/sim/planning/search/` |
| Ranking / opportunities | `assist/planning/` |
| Bake / verify | `core/bake/` |
| Trigger world | `core/trigger_world/` |

Planner paths stay driven by **settings + budgets**, not by a remote “verified” flag.

### 2.5 Neural model loading (optional AI tab)

**Local-only contract**

1. Packaged prior shipped with the mod **or** user-placed files under the mod config directory (e.g. `.neural` / project-defined extension — **not** Nova `NVM2` blobs).
2. Loader validates **our** magic/version and tensor shapes.
3. Failure mode: disable scorer, fall back to classical planner — never block the whole mod behind a login wall.

**Explicit non-goals**

- Fetching weights from `*.alquerez.workers.dev`.
- Parsing or converting Nova `model.bin`.
- Embedding Nova-derived weights.

Training: keep Train Session / Hybrid RL tooling that already lives in-tree; data stays on-device unless the user opts into *our* sharing flows.

### 2.6 Feature extraction

Define features from **documented game state** available to mods (player pose, mode, speed, nearby objects via level iteration helpers in `core/level/`, shadow observations). Document the feature vector in-repo so models are reproducible without reverse-engineering another vendor’s extractor.

### 2.7 Autoplay / intervention execution

EarlyBuffer product stance: timing assist, with **opt-in** AI autoplay for self-play — already described in `docs/assist_reference.md`.

Implementation surface:

- Input buffer / arm / fire in `assist/runtime` and hooks in `src/hooks/`.
- No dependency on Nova login UI (`SignInPop`, login-key dialogs).

### 2.8 UI

| Nova concept (study) | Independent replacement |
|---|---|
| Settings > Login | Omit |
| `lockedRow` by license | Use normal setting enablement / Safe Mode / scope gates |
| BufferWindow monolith | Existing `src/ui/settings`, pause bridge, overlays |

Use Geode `Popup`, `TextInput`, pause menu hooks as today — without account fields.

---

## 3. What we learn from Nova without copying it

Useful **architectural** takeaways (ideas, not code):

1. **Separate sim from scoring** — trajectory/shadow search can run without a neural net; net ranks candidates.
2. **Local cache then refresh** — good pattern for *our* optional assets; refresh must not require a proprietary login.
3. **Clear user messaging when AI is unavailable** — e.g. “no model file”, not “log in to unlock”.
4. **Keep online identity out of the hot path** — Nova’s bootstrap mixes login POST with model load; we must not.

---

## 4. Suggested work packages (implementation order)

1. **Document local model file contract** (magic, version, tensors) under `docs/dev/` + loader tests.
2. **Wire scorer behind a setting** that only checks file presence/version.
3. **Keep planner classical fallback** on any model error.
4. **Optional:** trusted cloud configs for *settings packs* only (existing cloud stack), still auth-free.
5. **Regression:** Real Assist / Wave gates per `AGENTS.md` after planner/neural changes.

No work package may include “emulate Nova login”, “spoof Workers”, or “patch alquerez.nova.dll”.

---

## 5. Mapping to repository layout

| Design component | Primary paths |
|---|---|
| Entry | `src/early_buffer_module.cpp` |
| Settings | `mod.json`, `src/config/` |
| Assist loop | `src/core/assist/` |
| Shadow + plan | `src/core/sim/` |
| Neural (independent) | AI/learning areas already referenced from assist docs; extend without Nova formats |
| Hooks | `src/hooks/` |
| UI | `src/ui/` |
| Optional HTTPS | `src/cloud/transport/` (allowlisted, integrity-checked) |

Coding standards: paired `snake_case` headers, no project namespaces, early returns, RelWithDebInfo builds, offline gates — as in `AGENTS.md`.

---

## 6. Acceptance checks

- Cold start with empty save: assist classical paths work; AI scorer off with a clear reason.
- No network call required for core assist.
- Any network feature uses EarlyBuffer policy hosts, never Nova login endpoints.
- Ship artifacts contain only first-party or clearly licensed weights.
- Code review rejects Nova DLL patches and auth-bypass hooks.
