# EconLife — Codex Developer Context

## Read First

- **AGENTS.md** — Multi-agent coordination rules (branch naming, module claiming, shared file rules)
- **CLAUDE.md** — Full project context (all rules apply to you too)
- This file contains Codex-specific operational instructions.

---

## What This Project Is

A deterministic C++20 agent-based economic simulation game. The simulation
runs headlessly at one tick per in-game day. The UI observes simulation
state only. January 2000 start date with dynamic era progression.

---

## Inviolable Rules

1. `simulation/` **never** imports from `ui/`
2. All randomness goes through `simulation/core/rng/DeterministicRNG`
3. Same seed + same inputs = same outputs (determinism is required)
4. Interface spec (`docs/interfaces/[module]/INTERFACE.md`) wins over implementation
5. No raw pointers in module code — use smart pointers or pool allocation
6. Floating-point accumulations use canonical sort order (`good_id` asc, `province_id` asc)

---

## Before You Write Code

1. Read the module's `CLAUDE.md` in `simulation/modules/[name]/`
2. Read the interface spec in `docs/interfaces/[name]/INTERFACE.md`
3. Understand `runs_after` / `runs_before` dependencies
4. Check the module's existing implementation files
5. Check for existing branches on this module: `git branch -r | grep -i "[module]"`

---

## Build Commands

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DECONLIFE_BUILD_TESTS=ON

# Build
cmake --build build -j

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific module tests
ctest --test-dir build -R "[module_name]" --output-on-failure

# Determinism tests
ctest --test-dir build -L "determinism" --output-on-failure

# Format code
clang-format -i simulation/modules/[module]/*.cpp simulation/modules/[module]/*.h

# Format check (dry run)
find simulation/ -name "*.h" -o -name "*.cpp" | xargs clang-format --dry-run --Werror
```

---

## Module Structure

Follow the existing pattern in `simulation/modules/`:

```
simulation/modules/[name]/
  CLAUDE.md              — module context (read this, do not modify)
  CMakeLists.txt         — build config
  [name]_module.h        — module class header (implements ITickModule)
  [name]_module.cpp      — implementation
  [name]_types.h         — data types (if needed)
```

---

## Session Workflow

1. Create branch: `codex/[module-name]-[task]`
2. Read module `CLAUDE.md` and `INTERFACE.md`
3. Implement / fix / optimize
4. Run tests, fix failures
5. Run clang-format
6. Commit with format: `[module_name]: description`
7. Push and create PR targeting `main`

---

## What NOT to Do

- Do not modify interface specs (`docs/interfaces/`)
- Do not modify shared core code (`simulation/core/`) without human approval
- Do not modify other modules' files
- Do not expand scope beyond the assigned task
- Do not use `std::rand`, `srand`, `std::random_device`, or system time in `simulation/`
- Do not guess when the spec is ambiguous — flag it in the PR description
- Do not modify `CLAUDE.md`, `AGENTS.md`, or `CODEX.md`

---

## Key Design Documents

- GDD v1.7: `docs/design/EconLife_GDD.md`
- Technical Design v29: `docs/design/EconLife_Technical_Design_v29.md`
- Feature Tier List: `docs/design/EconLife_Feature_Tier_List.md`
- AI Development Plan: `docs/design/EconLife_AI_Development_Plan_updated.md`
- Commodities & Factories: `docs/design/EconLife_Commodities_and_Factories_v23.md`

---

## Who to Ask When Unsure

- Design questions: Read GDD v1.7
- Architecture questions: Read Technical_Design_v29.md
- Scope questions: Read Feature_Tier_List.md
- If still unclear: Do not guess. Flag for human review in the PR description.
