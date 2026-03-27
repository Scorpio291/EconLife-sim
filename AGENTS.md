# EconLife — Multi-Agent Coordination

This file defines the rules for parallel AI agent work on EconLife.
Both Claude Code and Codex (and any future agents) follow these rules.

---

## Active Agents

| Agent       | Context Files Read          |
|-------------|-----------------------------|
| Claude Code | CLAUDE.md, AGENTS.md        |
| Codex       | CODEX.md, AGENTS.md         |

Both agents also read per-module `CLAUDE.md` files in `simulation/modules/[name]/`
and interface specs in `docs/interfaces/[name]/INTERFACE.md`.

---

## The Parallel Work Rule

Each agent session works on exactly **one module**. Before starting work,
claim the module by creating a branch. If a branch already exists for
that module, do not work on it — pick a different module.

---

## Branch Naming Convention

```
{agent}/{module-name}-{task}
```

Examples:
- `claude/evidence-implementation`
- `codex/supply-chain-stub`
- `claude/production-optimization`
- `codex/npc-behavior-lazy-eval`

Check for existing branches before starting:
```bash
git fetch origin
git branch -r | grep -i "{module-name}"
```

---

## Module Ownership During a Session

While a branch exists for a module, that module is **claimed**.
Files owned by a claimed module:

```
simulation/modules/[module_name]/*
simulation/tests/unit/[module_name]_test.cpp
simulation/tests/integration/*[module_name]*
```

Do NOT modify files in a claimed module from another session.

---

## Shared Files — Coordination Required

These files must not be modified by agents without human approval:

- `simulation/core/*`
- `simulation/modules/register_base_game_modules.*`
- `CMakeLists.txt` (root and simulation/)
- `CLAUDE.md`, `AGENTS.md`, `CODEX.md`
- `docs/design/*`
- `docs/interfaces/*/INTERFACE.md`

---

## Commit Message Format

```
[module_name]: description
```

Examples:
- `[production]: implement batch processing`
- `[evidence]: add propagation delay tests`
- `[supply_chain]: fix determinism in route calculation`

---

## Build and Test Commands

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DECONLIFE_BUILD_TESTS=ON

# Build
cmake --build build -j

# Run all unit tests
ctest --test-dir build -L "module" --output-on-failure

# Run specific module tests
ctest --test-dir build -R "[module_name]" --output-on-failure

# Determinism tests
ctest --test-dir build -L "determinism" --output-on-failure

# Format check
find simulation/ -name "*.h" -o -name "*.cpp" | xargs clang-format --dry-run --Werror
```

---

## Before Pushing / Creating a PR

1. All unit tests pass (the full suite, not just your module)
2. Determinism tests pass
3. Code is clang-format clean
4. No `simulation/` -> `ui/` imports
5. No `std::rand`, `srand`, `std::random_device`, or system time calls in `simulation/`
6. Commit messages follow the format above

---

## Merge Target

All PRs target `main`. One module per PR. Squash merge preferred.

---

## Dependency Graph

Lower-tier modules must be implemented and merged before higher-tier
modules that depend on them.

- Tier assignments: `docs/design/EconLife_Feature_Tier_List.md`
- Per-module dependencies: each module's `CLAUDE.md` lists `runs_after` / `runs_before`
- Full architecture: `docs/design/EconLife_Technical_Design_v29.md`
