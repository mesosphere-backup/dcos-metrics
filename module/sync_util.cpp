#include "sync_util.hpp"

size_t metrics::sync_util::Tickets::locked_get_next_ticket() {
  size_t ticket = ++next_ticket;
  tickets.insert(ticket);
  //DLOG(INFO) << "Created ticket " << ticket << " (" << tickets.size() << " now outstanding)";
  return ticket;
}

bool metrics::sync_util::Tickets::locked_find_ticket(size_t ticket) {
  auto iter = tickets.find(ticket);
  bool found = iter != tickets.end();
  if (found) {
    //DLOG(INFO) << "Found ticket " << ticket << " (" << tickets.size() << " outstanding)";
  } else {
    //DLOG(INFO) << "Didn't find ticket " << ticket << " (" << tickets.size() << " outstanding)";
  }
  return found;
}

void metrics::sync_util::Tickets::locked_clear_ticket(size_t ticket) {
  tickets.erase(ticket);
  //DLOG(INFO) << "Erased ticket " << ticket << " (" << tickets.size() << " left outstanding)";
}

std::shared_ptr<metrics::sync_util::Tickets> metrics::sync_util::tickets = std::make_shared<metrics::sync_util::Tickets>();
