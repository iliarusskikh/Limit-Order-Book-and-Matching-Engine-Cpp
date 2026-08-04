# Matching rules (v1)

Normative behaviour of this engine. Implementation must match these rules.
Prices and quantities are **`int64_t` integer ticks / lots** — no floating point on the match path.

Example scale (documentation only): 1 tick = 0.01 currency units → price `10025` displays as 100.25.

---

## Priority

1. **Price priority** — better prices trade first  
   - Aggressive buy consumes lowest ask first, then next, …  
   - Aggressive sell consumes highest bid first, then next, …
2. **Time priority** — at one price, orders match in **arrival order (FIFO)**.

Self-trade prevention is **off** in v1 (same owner may be maker and taker).

---

## Limit orders

### Buy limit
- If not marketable (`asks` empty **or** `limit < best ask`): **rest** on the bid book at `limit`.
- If marketable (`limit >= best ask`): match asks while `best ask <= limit` and qty remains.
- Any **unfilled remainder rests** on the bid at `limit`.

### Sell limit
- Symmetric vs bids: marketable when `limit <= best bid`; residual rests on the ask.

---

## Market orders

- **Buy market:** match asks from best upward until filled or asks empty. Price on the request is **ignored** (ACCEPT logs `price=0`).
- **Sell market:** symmetric vs bids.
- Unfilled remainder is **cancelled** (`CANCEL … reason=RESIDUAL`) — market orders **never rest**.
  (This is the IOC-market policy for v1.)

---

## Trade price

Each fill executes at the **resting (maker) order’s price**, not the aggressor’s limit.

Example: ask rests `10 @ 101`; buy limit `10 @ 105` arrives → trade at **101**.

---

## Cancel

- `cancel(order_id)` removes remaining qty of a resting order.
- Unknown id or already fully filled → **`REJECT reason=UnknownOrderId`**; book unchanged.
- User cancel emits `CANCEL … reason=USER`.

---

## Validation rejects

| Condition | Reason | Book |
|-----------|--------|------|
| `qty <= 0` | `InvalidQty` | unchanged |
| Duplicate live `order_id` | `DuplicateId` | unchanged |
| Cancel unknown id | `UnknownOrderId` | unchanged |

Order ids are **supplied by the caller / scenario** (not engine-assigned).

---

## Invariants

After every successful mutating call:

1. If both sides non-empty: **`best_bid < best_ask`** (no locked or crossed book).
2. Live orders have `qty > 0`.
3. Every live id appears in exactly one book location + the id index.
4. Level `total_qty` equals the sum of remaining qtys at that price.
5. No market orders rest on the book.

---

## Event order

Successful `NEW`:

```text
ACCEPT → zero or more TRADE → optional CANCEL (market residual only)
```

Failed `NEW` / `CANCEL`:

```text
REJECT
```

Residual **limit** rest is silent (visible via `SNAPSHOT` / later `CANCEL`).

---

## Not in v1

Amend/replace, FOK, named limit-IOC, stop/iceberg, fees, multi-symbol, networking, persistence, STP modes.
