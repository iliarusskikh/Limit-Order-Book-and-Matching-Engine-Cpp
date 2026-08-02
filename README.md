# Limit Order Book & Matching Engine (C++)

In-memory **CLOB** for a single instrument: validate → match → rest/cancel → emit events.  
Price-time priority, partial fills, cancels, L2 depth snapshots, golden-test driven.

| Spec / theory | |
|---------------|---|
| [`docs/matching_rules.md`](docs/matching_rules.md) | Normative matching rules |
| [`theory/index.html`](theory/index.html) | Theory notes (start with Architecture) |
| [`theory/Theory_Design_Decisions.html`](theory/Theory_Design_Decisions.html) | Locked design choices |

---

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/lob_tests                                          # G1–G9
./build/matcher tests/scenarios/demo_walk_book.txt         # scenario file
./build/matcher                                            # interactive REPL
```

Interactive mode reads the same DSL from the terminal (`HELP`, `QUIT` / Ctrl-D to exit):

```text
> NEW 1 BUY LIMIT 10 100
ACCEPT id=1 side=BUY type=LIMIT qty=10 price=100
> SNAPSHOT 5
SNAPSHOT depth=5
  BID px=100 qty=10 n=1
> QUIT
```

Requires C++17.

---

## Architecture

```text
  scenario / CLI ──► MatchingEngine
                       validate → match → rest / cancel → Event[]
                              │
                              ▼
                         OrderBook
                       bids: map<price, Level>   (best = begin, greater<>)
                       asks: map<price, Level>   (best = begin)
                       Level: total_qty + deque<Order>  (FIFO)
                       index: order_id → {side, price}
```

Public mutation API: `submit` / `cancel` only. Snapshots via `bids(depth)` / `asks(depth)`.

Detail: [`theory/Theory_Architecture.html`](theory/Theory_Architecture.html).

---

## Scope lock (v1)

| In | Out |
|----|-----|
| One symbol, limit + market | Networking / multi-symbol |
| Price-time priority, partials | Amend, FOK, stops, icebergs |
| Full cancel by id | Fees, persistence, threads |
| L2 snapshots, scenario CLI | GUI, live MDS link |
| Integer tick prices | `double` on match path |

---

## Design decisions

| Topic | Choice | Why / rejected |
|-------|--------|----------------|
| Book | `map` + `deque` + id index | Clear price-time semantics; flat ladder / intrusive lists later |
| Level qty | Incremental `total_qty` | Fast L2 without rescanning |
| Trade price | **Maker (resting)** price | Standard quote convention |
| Market residual | **Cancel** (`RESIDUAL`) | Pure market has no rest price; ≈ IOC market |
| Order ids | Scenario-supplied; reject duplicates | Deterministic golden tests |
| Events | Accept → trades → residual cancel | Stable CLI / diff logs |
| Concurrency | Single-threaded | Correctness first |
| Self-trade | Allowed | STP is a future flag |

More: [`theory/Theory_Design_Decisions.html`](theory/Theory_Design_Decisions.html).

---

## Golden tests (G1–G9)

| ID | Covers |
|----|--------|
| G1 | Rest non-marketable limits |
| G2 | Exact fill at maker price |
| G3 | Partial fill + residual rest |
| G4 | Walk multiple price levels |
| G5 | FIFO at same price |
| G6 | Cancel + unknown-id reject |
| G7 | Market vs thin book; residual cancel |
| G8 | Never leave a crossed book |
| G9 | Bad qty / duplicate id; book unchanged |

C++ harness: `./build/lob_tests`. Scenario twins: `tests/scenarios/`.

---

## Scenario DSL

```text
NEW <id> <BUY|SELL> <LIMIT|MARKET> <qty> <price>
CANCEL <id>
SNAPSHOT <depth>
CHECK_BEST_BID <price> <qty>
CHECK_BEST_ASK <price> <qty>
CHECK_TRADE_COUNT <n>
CHECK_NO_CROSS
HELP                         # interactive
QUIT | EXIT                  # interactive (or Ctrl-D)
```

Market orders: pass `price=0` (ignored on the match path).  
File mode: `./build/matcher tests/scenarios/….txt`. Interactive: `./build/matcher`.

---

## Layout

```text
include/   types, order_book, matching_engine, scenario, event_format
src/       matcher CLI
tests/     lob_tests + scenarios/
docs/      matching_rules.md
theory/    Theory_*.html notes
```

MIT licensed. Possible extensions: explicit TIF (IOC/FOK), amend/replace, multi-symbol, journal/replay, pooled order nodes — see theory Design decisions.
