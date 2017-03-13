#include "vast/chunk.hpp"

#define SUITE chunk
#include "test.hpp"

using namespace vast;

TEST(default construction) {
  auto x = chunk{};
  CHECK_EQUAL(x.data(), nullptr);
  CHECK_EQUAL(x.size(), 0u);
}

TEST(owning memory) {
  auto x = make_chunk(100);
  CHECK_EQUAL(x->size(), 100u);
}

TEST(non-owning memory) {
  char buf[128];
  auto x = make_chunk(sizeof(buf), buf);
  CHECK_EQUAL(x->size(), 128u);
  CHECK_EQUAL(x->data(), buf);
}