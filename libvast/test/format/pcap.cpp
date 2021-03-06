#include "vast/error.hpp"
#include "vast/event.hpp"

#include "vast/concept/parseable/to.hpp"
#include "vast/concept/parseable/vast/address.hpp"
#include "vast/filesystem.hpp"

#include "vast/format/pcap.hpp"

#define SUITE format
#include "test.hpp"
#include "data.hpp"

using namespace vast;

TEST(PCAP read/write 1) {
  // Initialize a PCAP source with no cutoff (-1), and at most 5 flow table
  // entries.
  format::pcap::reader reader{traces::nmap_vsn, uint64_t(-1), 5};
  auto e = expected<event>{no_error};
  std::vector<event> events;
  while (e || !e.error()) {
    e = reader.read();
    if (e)
      events.push_back(std::move(*e));
  }
  REQUIRE(!e);
  CHECK(e.error() == ec::end_of_input);
  REQUIRE(!events.empty());
  CHECK_EQUAL(events.size(), 44u);
  CHECK_EQUAL(events[0].type().name(), "pcap::packet");
  auto pkt = get_if<vector>(events.back().data());
  REQUIRE(pkt);
  auto conn_id = get_if<vector>(pkt->at(0));
  REQUIRE(conn_id); //[192.168.1.1, 192.168.1.71, 53/udp, 64480/udp]
  auto src = get_if<address>(conn_id->at(0));
  REQUIRE(src);
  CHECK_EQUAL(*src, *to<address>("192.168.1.1"));
  MESSAGE("write out read packets");
  auto file = "vast-unit-test-nmap-vsn.pcap";
  format::pcap::writer writer{file};
  auto deleter = caf::detail::make_scope_guard([&] { rm(file); });
  for (auto& e : events)
    REQUIRE(writer.write(e));
}

TEST(PCAP read/write 2) {
  // Spawn a PCAP source with a 64-byte cutoff, at most 100 flow table entries,
  // with flows inactive for more than 5 seconds to be evicted every 2 seconds.
  format::pcap::reader reader{traces::workshop_2011_browse, 64, 100, 5, 2};
  auto e = expected<event>{no_error};
  std::vector<event> events;
  while (e || !e.error()) {
    e = reader.read();
    if (e)
      events.push_back(std::move(*e));
  }
  REQUIRE(!e);
  CHECK(e.error() == ec::end_of_input);
  REQUIRE(!events.empty());
  CHECK_EQUAL(events.size(), 36u);
  CHECK_EQUAL(events[0].type().name(), "pcap::packet");
  MESSAGE("write out read packets");
  auto file = "vast-unit-test-workshop-2011-browse.pcap";
  format::pcap::writer writer{file};
  auto deleter = caf::detail::make_scope_guard([&] { rm(file); });
  for (auto& e : events)
    if (!writer.write(e))
      FAIL("failed to write event");
}
