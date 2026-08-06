#pragma once

// =============================================================================
// event_format.hpp — Serialize Event variants to stable one-line strings.
//
// Purpose: CLI / golden-friendly logging. Field order is locked so outputs
// can be diffed. No timestamps (determinism).
// =============================================================================

#include "types.hpp"

#include <sstream>
#include <string>
#include <variant>

namespace lob {

// Format one event as:
//   ACCEPT id=… side=… type=… qty=… price=…
//   TRADE maker=… taker=… price=… qty=…
//   CANCEL id=… qty=… reason=USER|RESIDUAL
//   REJECT id=… reason=…
inline std::string format_event(const Event& ev) {
  std::ostringstream out;
  if (const auto* a = std::get_if<AcceptedEvent>(&ev)) {
    out << "ACCEPT id=" << a->order_id
        << " side=" << to_string(a->side)
        << " type=" << to_string(a->type)
        << " qty=" << a->qty
        << " price=" << a->price;
  } else if (const auto* r = std::get_if<RejectedEvent>(&ev)) {
    out << "REJECT id=" << r->order_id
        << " reason=" << to_string(r->reason);
  } else if (const auto* t = std::get_if<TradeEvent>(&ev)) {
    out << "TRADE maker=" << t->maker_id
        << " taker=" << t->taker_id
        << " price=" << t->price
        << " qty=" << t->qty;
  } else if (const auto* c = std::get_if<CancelledEvent>(&ev)) {
    out << "CANCEL id=" << c->order_id
        << " qty=" << c->qty
        << " reason=" << to_string(c->reason);
  }
  return out.str();
}

}  // namespace lob
