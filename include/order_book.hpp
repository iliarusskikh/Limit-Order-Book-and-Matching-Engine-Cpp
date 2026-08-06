#pragma once

// =============================================================================
// order_book.hpp — Resting liquidity for one instrument.
//
// Purpose: store bids/asks as price → Level (FIFO deque of Order), maintain an
// id→location index for cancels, and expose helpers the match loop uses to
// take liquidity from the best opposite price.
//
// Decisions:
//   - std::map for sorted prices (best = begin())
//   - std::deque for time priority within a price
//   - unordered_map id index (cancel without scanning the whole book)
//   - incremental Level::total_qty for cheap L2 snapshots
//
// Implementations live in matching_engine.hpp (header-only v1).
// =============================================================================

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob {

// Where a live order sits: which side and at which price key.
// Used by by_id_ so cancel can jump to the right Level.
struct BookLocation {
  Side side{};
  std::int64_t price{};
};

// All resting orders at one price on one side.
struct Level {
  std::int64_t total_qty{0};   // sum of orders[].qty (kept in sync on add/fill/cancel)
  std::deque<Order> orders;    // FIFO: front = oldest = next to match
};

// Result of consuming liquidity from the best bid or best ask.
struct MakerFill {
  std::uint64_t maker_id{};  // resting order that was hit
  std::int64_t price{};      // maker price (trade price)
  std::int64_t qty{};        // filled size this step
};

// In-memory book: two sorted price maps + id index.
class OrderBook {
public:
  // Insert a resting order at order.price on order.side.
  // Returns false if order_id already exists or qty <= 0.
  bool add(const Order& order);

  // Remove a resting order by id (full cancel of remaining qty).
  // Returns removed qty, or nullopt if id unknown.
  std::optional<std::int64_t> remove(std::uint64_t order_id);

  // L2 snapshot: up to `depth` best bid levels (highest price first).
  std::vector<BookLevel> bids(std::size_t depth) const;

  // L2 snapshot: up to `depth` best ask levels (lowest price first).
  std::vector<BookLevel> asks(std::size_t depth) const;

  // True if order_id is currently resting (present in by_id_).
  bool contains(std::uint64_t order_id) const {
    return by_id_.find(order_id) != by_id_.end();
  }

  // Highest bid price, or nullopt if no bids.
  std::optional<std::int64_t> best_bid_price() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
  }

  // Lowest ask price, or nullopt if no asks.
  std::optional<std::int64_t> best_ask_price() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
  }

  // Invariant helper: empty side OK; else best_bid < best_ask (no lock/cross).
  bool is_uncrossed() const {
    if (bids_.empty() || asks_.empty()) return true;
    return bids_.begin()->first < asks_.begin()->first;
  }

  // Match against the oldest order at the best ask; up to `qty`.
  // Updates maker remaining / level / index. nullopt if no asks or qty <= 0.
  std::optional<MakerFill> take_best_ask(std::int64_t qty);

  // Match against the oldest order at the best bid; up to `qty`.
  // Updates maker remaining / level / index. nullopt if no bids or qty <= 0.
  std::optional<MakerFill> take_best_bid(std::int64_t qty);

private:
  // greater<> so begin() is the highest bid (best bid).
  std::map<std::int64_t, Level, std::greater<std::int64_t>> bids_;
  // default less<> so begin() is the lowest ask (best ask).
  std::map<std::int64_t, Level> asks_;
  // Fast cancel: id → {side, price}, then scan that level's deque.
  std::unordered_map<std::uint64_t, BookLocation> by_id_;
};

}  // namespace lob
