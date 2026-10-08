# Doom 3 BFG Edition for the PlayStation 2

This repository is the starting point for a PlayStation 2 port of
[id's Doom 3 BFG Edition](https://github.com/id-Software/DOOM-3-BFG) using the free ps2dev
SDK. The first target is an EE build and a reproducible headless engine boot. The port is
not yet buildable or playable on PS2.

The original engine remains in `neo/`. `src/ps2/`, `src/tools/` and `src/tests/` are
scaffolding for the console backend, host tools and tests. The planned first structural
change will move `neo/` to `src/neo/`; see [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)
for the milestones and acceptance checks.

The root `Makefile` is currently an unchanged copy of the Quake II PS2 build for reference.
Its targets still name Quake sources and data. The Doom 3 build and emulator tests will be
added in later commits.

Local retail data lives under `gamedata/`: `d3_bfg/` contains the BFG installation and
`d3_roe/` is reference data from Resurrection of Evil. Both are ignored by Git. The
tracked `base/` directory contains source-release configuration and render programs.
The future run/package target will stage an appropriate `base/` view beside the ELF
without replacing the tracked directory or committing retail data.

[AGENTS.md](AGENTS.md) describes repository conventions. [CVARS.md](CVARS.md) will track
backend cvars as they are implemented. The notes in `.claude/rules/` include measured
Quake II PS2 behavior; they are reference material until reproduced here.

`src/tools/scripts/symbolize.py` was adapted from the Quake II PS2 port and retains its
GPL v2 notice and accompanying license. It is a standalone host helper, not engine code.
