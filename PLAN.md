# PLAN — Limit Order Book & Matching Engine (C++)

**Status:** ready to implement (greenfield)  
**Working title:** `LimitOrderBook` / `LOB Matcher`  
**Folder:** `quant/projects/Limit Order Book and Matching Engine`  
**Standard:** C++11 (or C++17 if you prefer; lock one in CMake and stay consistent)  
**Deps:** C++ standard library + CMake only (no Boost, no network stack)  
**Companion project:** TCP Market Data Simulator (wire/feed). This project is the **in-memory book + match** side — deliberately **not** another sockets project.

---

## 1. Purpose (why this exists)

### Portfolio / interview story
Demonstrate **market-microstructure + systems C++** skills that complement a TCP feed simulator:

| Already shown (MDS) | Show next (this project) |
|---------------------|---------------------------|
| Binary protocol, `poll()`, reconnect | Price-time priority matching |
| Synthetic last-trade ticks | L2 book: bids/asks, depth, cancels |
| Session / I/O ownership | Event-driven engine, clear invariants |
| Inter-arrival stats | Optional match-path timing (secondary) |

**CV one-liner (target):**  
> Limit order book matching engine in C++ — price-time priority, partial fills, cancels, L2 depth snapshots, golden-test driven.

### Learning / technical purposes
1. **Correctness under rules** — matching is a state machine with invariants (no crossed book, FIFO within a price level, atomic cancel/match semantics).
2. **Data-structure judgment** — choose and justify book representation (e.g. `std::map` of price → queue of orders vs contiguous price levels).
3. **API design** — small, explicit order/event types; no god-object “Exchange” blob.
4. **Testability** — deterministic outcomes from scripted order sequences (this is what separates a portfolio LOB from a toy).
5. **Scope discipline** — same publish bar as MDS: PLAN, README, deliberate cuts, decision comments (not Phase-N banners at the end).

### What this is *not*
- Not a real exchange, not colocated HFT, not FIX/OUCH.
- Not a networked matching venue (can add later as a *separate* exercise).
- Not a full backtester / portfolio / PnL accounting system.
- Not options pricing (separate project if needed later).

---

## 2. Scope lock (v1 — must not expand mid-build)

### In scope (v1)
- Single instrument (one symbol), one book.
- Order types: **Limit** and **Market**.
- Sides: **Buy** / **Sell**.
- Actions: **New order**, **Cancel** by `order_id` (full cancel only in v1).
- Matching: **price-time priority** (better price first; FIFO at same price).
- **Partial fills** allowed; residual limit rests on book.
- Market orders: match available liquidity; **unfilled remainder cancelled** (document this; no hold of market residual).
- Self-trade: **v1 policy = allow** (simplest) — document; optional flag later.
- Outputs:
  - Fill / trade events (aggressor id, resting id, price, qty, timestamp or sequence).
  - Ack / reject reasons (unknown id, invalid qty, etc.).
  - L2 snapshot: top **N** levels (bid/ask price + aggregate size + optional order count).
- Deterministic CLI or small driver that reads a **scenario file** (see §6) and prints events.
- Unit / golden tests for scripted sequences.
- README + short `docs/matching_rules.md` + MIT LICENSE when publishing.

### Explicitly out of scope (v1)
| Cut | Why |
|-----|-----|
| Networking / TCP / multicast | Already covered by MDS; keep this CPU/logic focused |
| Multiple symbols / sharding | Scope creep |
| Amend / replace (qty or price change) | Use cancel+new in v1 |
| Stop / iceberg / FOKs / IOC as named types | IOC ≈ market; FOK can be v2 |
| Hidden / pegged / midpoint | Exchange microstructure rabbit hole |
| Persistent journal / recovery | Ops surface |
| Thread pool / lock-free / SIMD matching | Wrong complexity for v1 correctness story |
| Fees, lots, tick-size enforcement beyond simple integer ticks | Keep qty/price as integers |
| GUI | CLI + tests enough |
| Linking live to MDS process | Optional future; not v1 |

### Price / qty representation (decision)
- **`int64_t` price** in integer ticks (e.g. 1 tick = 0.01 currency → price `10025` = 100.25).
- **`int64_t` qty** in lots/shares (strictly `> 0` on entry).
- No `double` on the match path.
- Document scale in config or constants header.

---

## 3. Core matching rules (normative)

Write these into `docs/matching_rules.md` during implementation; implement exactly this:

1. **Buy limit** rests on bid if it does not cross; if limit price ≥ best ask, it **aggresses** and matches asks from best ask upward until qty depleted or no more marketable liquidity; residual (if limit) rests on bid.
2. **Sell limit** symmetric vs bids.
3. **Market buy** matches asks until qty filled or asks empty; leftover **discarded** (generate cancel/expire residual event).
4. **Market sell** symmetric.
5. At a price level, orders match in **arrival order** (FIFO).
6. A trade price is the **resting** order’s price (standard maker price convention) — **document this**.
7. Cancel removes remaining qty; cancel of unknown / already-filled id → reject.
8. Book must never remain **crossed** after an operation completes (best bid < best ask when both sides non-empty; empty side OK).
9. Each accepted new order gets a monotonic **`order_id`** (engine-assigned) unless scenario supplies ids (prefer engine-assigned for simplicity; scenarios refer to returned ids or use client_tag — pick one and stick to it).

**Recommended v1 identity model (lock this):**  
Scenario commands include explicit `order_id` chosen by the scenario author; engine **rejects duplicate ids**. Tests fully deterministic without id capture.

---

## 4. Suggested architecture

```text
                    ┌─────────────────────────────┐
  scenario / CLI ──►│  MatchingEngine             │
                    │  - validate                │
                    │  - match                   │
                    │  - rest / cancel           │
                    │  - emit Event[]            │
                    └───────────┬─────────────────┘
                                │
                    ┌───────────▼─────────────────┐
                    │  OrderBook                  │
                    │  bids: price → level queue  │
                    │  asks: price → level queue  │
                    └─────────────────────────────┘
```

### Recommended types (names flexible)

```text
enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };
enum class EventType { Accepted, Rejected, Trade, Cancelled, BookSnapshot /* optional */ };

struct Order {
  uint64_t order_id;
  Side side;
  OrderType type;
  int64_t price;   // ignored for Market on entry (or required=0)
  int64_t qty;     // remaining qty mutates as fills happen
  uint64_t seq;    // optional time-priority tie-break / arrival counter
};

struct Trade {
  uint64_t maker_id;
  uint64_t taker_id;
  int64_t price;
  int64_t qty;
};

struct BookLevel { int64_t price; int64_t total_qty; size_t order_count; };
```

### Book storage (v1 decision — pick and document)

**Default recommendation:**  
- `std::map<int64_t, std::deque<Order>, std::greater<int64_t>> bids` (best bid = `begin()`)  
- `std::map<int64_t, std::deque<Order>> asks` (best ask = `begin()`)  

**Why:** Clear price-time semantics, easy to implement correctly in a day, good interview talking point (“I’d replace maps with pooled price levels for latency later”).

**Rejected for v1:** intrusive lists + price hashed array (faster, longer to debug). Mention as future work.

### Engine API (minimal)

```text
class MatchingEngine {
public:
  std::vector<Event> submit(const OrderRequest& req);
  std::vector<Event> cancel(uint64_t order_id);
  std::vector<BookLevel> bids(size_t depth) const;
  std::vector<BookLevel> asks(size_t depth) const;
};
```

All mutation goes through `submit` / `cancel`. No public raw book mutation.

---

## 5. Repository layout (suggested)

```text
Limit Order Book and Matching Engine/
├── CMakeLists.txt
├── LICENSE                 # MIT when publishing
├── PLAN.md                 # this file
├── README.md
├── .gitignore
├── docs/
│   └── matching_rules.md
├── config/
│   └── default.conf        # tick label, snapshot_depth, optional
├── include/
│   ├── types.hpp           # Side, Order, Event, …
│   ├── order_book.hpp      # book data structure
│   ├── matching_engine.hpp # match + cancel logic
│   └── scenario.hpp        # optional: parse scenario DSL
├── src/
│   └── main.cpp            # CLI: run scenario file
├── tests/
│   ├── test_matching.cpp   # golden sequences
│   └── scenarios/          # *.txt scenario files
└── theory/                 # optional later; not required for v1 ship
```

Header-only vs `.cpp` split: either is fine. Prefer **clear boundaries** over premature `.cpp` proliferation. CMake: `matcher` executable + `lob_tests` executable.

---

## 6. Scenario file format (for demos + golden tests)

Simple line-oriented DSL (suggestion):

```text
# comments allowed
# NEW <id> <BUY|SELL> <LIMIT|MARKET> <qty> <price>
# CANCEL <id>
# SNAPSHOT <depth>
# CHECK_BEST_BID <price> <qty>     # optional assert directives for tests
# CHECK_BEST_ASK <price> <qty>
# CHECK_TRADE_COUNT <n>

NEW 1 BUY LIMIT 10 100
NEW 2 SELL LIMIT 10 101
NEW 3 BUY LIMIT 5 101
SNAPSHOT 5
CANCEL 1
```

**CLI:**
```bash
./matcher ../tests/scenarios/cross_spread.txt
./lob_tests   # runs embedded + file scenarios
```

Print a readable event log (one event per line) suitable for diffing in tests.

---

## 7. Mandatory golden scenarios (acceptance tests)

Implement at least these; each must pass:

| ID | Name | Expectation |
|----|------|-------------|
| G1 | Rest only | Buy limit below ask / sell above bid → rests; empty opposite side OK |
| G2 | Exact full fill | Bid 10@100, sell limit 10@100 → one trade 10@100; book empty |
| G3 | Partial fill + rest | Bid 10@100, sell 4@100 → trade 4; bid residual 6@100 |
| G4 | Walk the book | Multiple ask levels; large buy limit/market sweeps best then next |
| G5 | FIFO same price | Two sells @100 (id1 then id2); buy 5 → consumes id1 first |
| G6 | Cancel resting | Cancel reduces/removes level; cancel missing id → reject |
| G7 | Market vs thin book | Market buy for 100 against 30 liquidity → trades 30, residual cancelled |
| G8 | No crossed book | After every op, if both sides non-empty: best_bid < best_ask |
| G9 | Reject bad input | qty≤0, duplicate id, market with nonsense — reject, book unchanged |

Optional stretch (only if time): amend-via-cancel+new scenario documented as pattern.

---

## 8. Implementation roadmap (phased, finishable)

Collapse or stretch as needed; do **not** start Phase 3 before Phase 2 tests exist.

### Phase 0 — Skeleton
- CMake targets, README stub, types.hpp, empty engine returning Rejected.
- One failing test wired (prove test harness works).

### Phase 1 — Book structure
- Insert/cancel resting limits **without** matching (or with matching disabled flag).
- Best bid/ask + depth snapshot.
- Tests: insert ordering, cancel, snapshot aggregates.

### Phase 2 — Matching
- Limit-to-limit aggression + partials + FIFO.
- Trade events + residual resting.
- Tests G2–G5, G8.

### Phase 3 — Market orders + rejects
- Market sweep + residual policy.
- Validation/rejects G7, G9.
- CLI scenario runner.

### Phase 4 — Polish (publish bar)
- `docs/matching_rules.md`, README (architecture diagram, design table, CV one-liner).
- Decision comments in engine/book (why map+deque; maker price; market residual).
- MIT LICENSE, `.gitignore` (build/, binaries, IDE).
- Optional: simple steady_clock histogram around `submit` (p50/p99) — **secondary**, do not block ship.

---

## 9. C++ skills this project should visibly demonstrate

Call these out in README “Design decisions” / theory later:

- Strong types: `enum class Side`, `OrderType`, `EventType`
- RAII only where needed (file streams in scenario loader); **no** socket RAII required
- Value semantics for `Order` / `Event`; clear ownership of book containers
- `const` correctness on snapshots
- Integer pricing (no float on hot path)
- Algorithms over book levels (loops, early exit)
- Test-first or test-alongside discipline (`assert` or a tiny test main; Catch2 optional — **prefer zero deps**: hand-rolled checks OK)
- Optional C++17: `std::optional` for best bid/ask — only if standard locked to 17+

Avoid: inheritance hierarchies for order types, virtual matchers, singleton engine, premature templates.

---

## 10. Design decisions to lock early (write into PLAN/README)

| Decision | v1 choice | Rejected alternative |
|----------|-----------|----------------------|
| Transport | In-process API + CLI scenarios | TCP matching server |
| Book container | `map` + `deque` per level | Flat array / intrusive list |
| Price | `int64` ticks | `double` |
| Market residual | Cancel leftover | Rest as limit (wrong) |
| Trade price | Maker (resting) price | Mid or taker limit |
| Amend | Out of scope | In-place replace |
| Concurrency | Single-threaded | Mutex per book |
| Self-trade | Allow | Prevention queue |
| Order IDs | Scenario-supplied; reject duplicates | Engine-assigned only |

---

## 11. README requirements (when finishing)

Mirror MDS publish quality:

- What / why / scope lock table  
- Build & run  
- Architecture ASCII diagram  
- Design decisions table  
- How to run golden tests  
- CV one-liner  
- Link to `docs/matching_rules.md`  
- Explicit “Not a production exchange”

---

## 12. Success criteria (done means done)

v1 is **complete** when:

1. All mandatory golden scenarios G1–G9 pass.  
2. CLI runs at least one demo scenario and prints trades + snapshot.  
3. Matching rules doc matches behavior.  
4. README is publishable; out-of-scope list present.  
5. No network code; no float prices on match path.  
6. Clean Release build via CMake.

---

## 13. Future / next phases (do not build in v1)

1. IOC / FOK as explicit TIF flags  
2. Amend/replace order  
3. Self-trade prevention  
4. Multi-symbol `unordered_map<Symbol, OrderBook>`  
5. Journal + replay for deterministic recovery  
6. Price-level object pool / contiguous side arrays + benchmarks vs `map`  
7. Optional: feed trades into MDS-style binary protocol (integration demo)  
8. Stop orders / pegged  

---

## 14. Instructions for the implementing AI

1. **Work in this folder only** — do not mix into the TCP Market Data Simulator tree.  
2. Follow **scope lock** strictly; if tempted to add networking or options pricing, stop and document under Future.  
3. Implement in phase order; **Phase 2 must have tests** before polish.  
4. Prefer boring, correct C++ over cleverness.  
5. At the end, run a publish-prep pass: decision comments, README, matching_rules.md, `.gitignore`, LICENSE.  
6. Do not invent requirements that contradict §2–§3; if unspecified, choose the **simpler** option and document it in a Decision callout.  
7. Read `.gitignore` and `README.md` stubs already in this folder; expand README when shipping.

---

## 15. One-day timebox guidance (human)

| Block | Focus |
|-------|--------|
| 0–1h | Skeleton + types + test harness + G1 failing→passing book insert |
| 1–4h | Matching limits + G2–G5 |
| 4–6h | Market + rejects + CLI |
| 6–8h | Docs, README, fix edge cases, Release build |

If behind: drop CLI polish and timing histogram; **never** drop G1–G9.
