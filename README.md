# Limit Order Book & Matching Engine (C++)

**Status:** not implemented yet — specification only.

Educational / portfolio C++ project: an in-memory **limit order book** with **price-time priority** matching, cancels, L2 depth snapshots, and golden scenario tests.

Companion to the TCP Market Data Simulator (networking/feed). This repo is **matching logic only** — no sockets in v1.

## Start here

| File | Purpose |
|------|---------|
| [`PLAN.md`](PLAN.md) | Full scope lock, rules, architecture, phases, acceptance tests |
| [`.gitignore`](.gitignore) | CMake/build/IDE ignores |

## Next steps

1. Open a new agent/chat in this folder.  
2. Prompt: implement v1 per `PLAN.md` (Phases 0→4), do not expand scope.  
3. Ship when G1–G9 pass and README/docs meet the publish bar in the plan.

## Target CV one-liner

> Limit order book matching engine in C++ — price-time priority, partial fills, cancels, L2 depth snapshots, golden-test driven.
