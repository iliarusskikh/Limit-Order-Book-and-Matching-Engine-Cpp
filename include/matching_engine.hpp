#pragma once

// =============================================================================
// matching_engine.hpp — MatchingEngine API + OrderBook method implementations.
//
// Purpose: sole mutation façade for the book. Validates requests, runs the
// price-time match loop, rests residual limits, cancels market residuals,
// and returns an ordered event list.
//
// Decisions:
//   - Accepted emitted after validation, before matching
//   - Trade price = maker (resting) price
//   - Market residual → Cancelled(RESIDUAL); never rests
//   - Scenario-/caller-supplied order ids; reject duplicates
//   - Header-only inline impl for v1 (clear boundaries over TU splits)
// =============================================================================

#include "order_book.hpp"
#include "types.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace lob {

// Owns one OrderBook. All book changes go through submit() / cancel().
class MatchingEngine {
public:
  // Validate + match + rest/cancel residual. Returns events in locked order:
  // Reject alone, or Accept → Trade* → optional Cancel(RESIDUAL).
  std::vector<Event> submit(const OrderRequest& req);

  // Remove resting order by id. Cancelled(USER) or Rejected(UnknownOrderId).
  std::vector<Event> cancel(std::uint64_t order_id);

  // Forward L2 bid snapshot (const; does not mutate).
  std::vector<BookLevel> bids(std::size_t depth) const { return book_.bids(depth); }

  // Forward L2 ask snapshot (const; does not mutate).
  std::vector<BookLevel> asks(std::size_t depth) const { return book_.asks(depth); }

  // Read-only access for invariants / tests (e.g. is_uncrossed, contains).
  const OrderBook& book() const { return book_; }

private:
  OrderBook book_;  // single-instrument resting state
};

// --- OrderBook implementations ---

// Rest an order on the correct side/price; update aggregate + id index.
inline bool OrderBook::add(const Order& order) {
  if (by_id_.find(order.order_id) != by_id_.end()) {
    return false;
  }
  if (order.qty <= 0) {
    return false;
  }

  Level* level = (order.side == Side::Buy) ? &bids_[order.price] : &asks_[order.price];
  level->orders.push_back(order);  // newest at back (FIFO)
  level->total_qty += order.qty;
  by_id_[order.order_id] = BookLocation{order.side, order.price};
  return true;
}

// Cancel-by-id: locate via index, erase from deque, fix aggregate, drop empty level.
inline std::optional<std::int64_t> OrderBook::remove(std::uint64_t order_id) {
  const auto idx = by_id_.find(order_id);
  if (idx == by_id_.end()) {
    return std::nullopt;
  }

  const BookLocation loc = idx->second;

  // Shared erase path for bids_ or asks_ (same Level layout).
  auto erase_from = [&](auto& side_map) -> std::optional<std::int64_t> {
    auto level_it = side_map.find(loc.price);
    if (level_it == side_map.end()) {
      return std::nullopt;
    }
    Level& level = level_it->second;
    auto ord_it = std::find_if(
        level.orders.begin(), level.orders.end(),
        [order_id](const Order& o) { return o.order_id == order_id; });
    if (ord_it == level.orders.end()) {
      return std::nullopt;
    }

    const std::int64_t qty = ord_it->qty;
    level.total_qty -= qty;
    level.orders.erase(ord_it);
    by_id_.erase(idx);

    if (level.orders.empty()) {
      side_map.erase(level_it);
    }
    return qty;
  };

  if (loc.side == Side::Buy) {
    return erase_from(bids_);
  }
  return erase_from(asks_);
}

// Walk bids_ from best (highest) until `depth` levels collected.
inline std::vector<BookLevel> OrderBook::bids(std::size_t depth) const {
  std::vector<BookLevel> out;
  out.reserve(depth);
  for (const auto& [price, level] : bids_) {
    if (out.size() >= depth) break;
    out.push_back(BookLevel{price, level.total_qty, level.orders.size()});
  }
  return out;
}

// Walk asks_ from best (lowest) until `depth` levels collected.
inline std::vector<BookLevel> OrderBook::asks(std::size_t depth) const {
  std::vector<BookLevel> out;
  out.reserve(depth);
  for (const auto& [price, level] : asks_) {
    if (out.size() >= depth) break;
    out.push_back(BookLevel{price, level.total_qty, level.orders.size()});
  }
  return out;
}

// Hit oldest ask at best ask price; partial or full fill of that maker.
inline std::optional<MakerFill> OrderBook::take_best_ask(std::int64_t qty) {
  if (asks_.empty() || qty <= 0) {
    return std::nullopt;
  }
  auto level_it = asks_.begin();  // best ask = lowest price
  Level& level = level_it->second;
  Order& maker = level.orders.front();

  const std::int64_t trade_qty = std::min(qty, maker.qty); // taker cannot trade more than maker has
  const MakerFill fill{maker.order_id, maker.price, trade_qty};

  maker.qty -= trade_qty;
  level.total_qty -= trade_qty;

  if (maker.qty == 0) {
    by_id_.erase(maker.order_id);
    level.orders.pop_front();
  }
  if (level.orders.empty()) {
    asks_.erase(level_it);
  }
  return fill;
}

// Hit oldest bid at best bid price; partial or full fill of that maker. Updates the book.
inline std::optional<MakerFill> OrderBook::take_best_bid(std::int64_t qty) {
  if (bids_.empty() || qty <= 0) {
    return std::nullopt;
  }
  auto level_it = bids_.begin();  // best bid = highest price
  Level& level = level_it->second;
  Order& maker = level.orders.front();

  const std::int64_t trade_qty = std::min(qty, maker.qty);  // taker cannot trade more than maker has
  const MakerFill fill{maker.order_id, maker.price, trade_qty};

  maker.qty -= trade_qty;
  level.total_qty -= trade_qty;

  if (maker.qty == 0) {
    by_id_.erase(maker.order_id);
    level.orders.pop_front();
  }
  if (level.orders.empty()) {
    bids_.erase(level_it);
  }
  return fill;
}

// --- MatchingEngine implementations ---

// Full submit path: validate → accept → match loop → rest or residual-cancel.
inline std::vector<Event> MatchingEngine::submit(const OrderRequest& req) {
  if (req.qty <= 0) {
    return {RejectedEvent{req.order_id, RejectReason::InvalidQty}};
  }
  if (book_.contains(req.order_id)) {
    return {RejectedEvent{req.order_id, RejectReason::DuplicateId}};
  }

  const bool is_market = (req.type == OrderType::Market);
  // Market has no resting price; ACCEPT logs price=0.
  const std::int64_t display_price = is_market ? 0 : req.price;

  std::vector<Event> events;
  events.push_back(AcceptedEvent{req.order_id, req.side, req.type, req.qty, display_price});

  std::int64_t remaining = req.qty;

  if (req.side == Side::Buy) {
    // Limit stops when best ask > limit; market takes any ask.
    while (remaining > 0) {
      const auto best = book_.best_ask_price();
      if (!best.has_value()) {
        break;
      }
      if (!is_market && *best > req.price) {
        break;
      }
      const auto fill = book_.take_best_ask(remaining);
      if (!fill.has_value()) {
        break;
      }
      events.push_back(
          TradeEvent{fill->maker_id, req.order_id, fill->price, fill->qty});
      remaining -= fill->qty;
    }
  } else {
    // Limit stops when best bid < limit; market takes any bid.
    while (remaining > 0) {
      const auto best = book_.best_bid_price();
      if (!best.has_value()) {
        break;
      }
      if (!is_market && *best < req.price) {
        break;
      }
      const auto fill = book_.take_best_bid(remaining);
      if (!fill.has_value()) {
        break;
      }
      events.push_back(
          TradeEvent{fill->maker_id, req.order_id, fill->price, fill->qty});
      remaining -= fill->qty;
    }
  }

  if (remaining > 0) {
    if (is_market) {
      // IOC-style market: leftover never rests.
      events.push_back(
          CancelledEvent{req.order_id, remaining, CancelReason::Residual});
    } else {
      Order resting{req.order_id, req.side, req.price, remaining};
      book_.add(resting);
    }
  }

  return events;
}

// User cancel of a resting order (or reject if already gone).
inline std::vector<Event> MatchingEngine::cancel(std::uint64_t order_id) {
  const auto qty = book_.remove(order_id);
  if (!qty.has_value()) {
    return {RejectedEvent{order_id, RejectReason::UnknownOrderId}};
  }
  return {CancelledEvent{order_id, *qty, CancelReason::User}};
}

}  // namespace lob
