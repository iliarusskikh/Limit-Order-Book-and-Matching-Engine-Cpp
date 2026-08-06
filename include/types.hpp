#pragma once

// =============================================================================
// types.hpp — Shared value types for the limit-order-book matching engine.
//
// Purpose: define sides, order kinds, events, and POD structs used by
// OrderBook, MatchingEngine, scenario runner, and tests. No behaviour here.
//
// Decisions:
//   - enum class (not bool / strings) for sides and types
//   - int64_t prices in integer ticks (never double on the match path)
//   - Event as std::variant so submit/cancel return one list type
// =============================================================================

#include <cstdint>
#include <variant>

namespace lob {

// Buy rests on the bid book; Sell rests on the ask book.
enum class Side : std::uint8_t { Buy, Sell };

// Limit may rest; Market takes liquidity and never rests (residual cancelled).
enum class OrderType : std::uint8_t { Limit, Market };

// Why a NEW or CANCEL was refused (book left unchanged).
enum class RejectReason : std::uint8_t {
  DuplicateId,     // order_id already live on the book
  InvalidQty,      // qty <= 0
  UnknownOrderId   // cancel of missing / already-gone id
};

// Why quantity left the book without a trade.
enum class CancelReason : std::uint8_t {
  User,      // explicit cancel() / CANCEL command
  Residual   // market order leftover after liquidity exhausted
};

// Incoming API / scenario request. qty is the full size on entry.
struct OrderRequest {
  std::uint64_t order_id{};  // caller-supplied; must be unique while live
  Side side{};
  OrderType type{};
  std::int64_t price{};      // ticks; ignored for Market (ACCEPT prints 0)
  std::int64_t qty{};        // must be > 0
};

// Resting order stored inside a Level deque.
// qty is *remaining* and shrinks on partial fills.
// (Only limits rest in v1 — markets never call add().)
struct Order {
  std::uint64_t order_id{};
  Side side{};
  std::int64_t price{};  // limit price in ticks
  std::int64_t qty{};    // remaining size
};

// One L2 depth row: aggregated liquidity at a single price.
struct BookLevel {
  std::int64_t price{};
  std::int64_t total_qty{};     // sum of remaining sizes at this price
  std::size_t order_count{};    // number of orders at this price
};

// --- Events returned from MatchingEngine::submit / cancel ---

// Order passed validation (emitted before matching).
struct AcceptedEvent {
  std::uint64_t order_id{};
  Side side{};
  OrderType type{};
  std::int64_t qty{};    // original request qty
  std::int64_t price{};  // limit price, or 0 for market
};

// Validation or cancel failure; book unchanged.
struct RejectedEvent {
  std::uint64_t order_id{};
  RejectReason reason{};
};

// Fill at maker price.
struct TradeEvent {
  std::uint64_t maker_id{};
  std::uint64_t taker_id{};
  std::int64_t price{};
  std::int64_t qty{};
};

// User cancel or market residual cancel.
struct CancelledEvent {
  std::uint64_t order_id{};
  std::int64_t qty{};  // qty removed / discarded
  CancelReason reason{};
};

// Single event type for API responses.
using Event = std::variant<AcceptedEvent, RejectedEvent, TradeEvent, CancelledEvent>;

// --- String helpers for CLI / logs ---

inline const char* to_string(Side s) {
  return s == Side::Buy ? "BUY" : "SELL";
}

inline const char* to_string(OrderType t) {
  return t == OrderType::Limit ? "LIMIT" : "MARKET";
}

inline const char* to_string(RejectReason r) {
  switch (r) {
    case RejectReason::DuplicateId:    return "DuplicateId";
    case RejectReason::InvalidQty:     return "InvalidQty";
    case RejectReason::UnknownOrderId: return "UnknownOrderId";
  }
  return "?";
}

inline const char* to_string(CancelReason r) {
  return r == CancelReason::User ? "USER" : "RESIDUAL";
}

}  // namespace lob
