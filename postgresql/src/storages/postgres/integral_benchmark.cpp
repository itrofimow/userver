#include <benchmark/benchmark.h>

#include <limits>

#include <storages/postgres/detail/connection.hpp>

#include <storages/postgres/util_benchmark.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

namespace pg = storages::postgres;
using namespace pg::bench;

BENCHMARK_F(PgConnection, BoolRoundtrip)(benchmark::State& state) {
  RunStandalone(state, [this, &state] {
    bool v = true;
    for (auto _ : state) {
      auto res = GetConnection().Execute("select $1", v);
      res.Front().To(v);
    }
  });
}

BENCHMARK_F(PgConnection, Int16Roundtrip)(benchmark::State& state) {
  RunStandalone(state, [this, &state] {
    std::int16_t v = std::numeric_limits<std::int16_t>::max();
    for (auto _ : state) {
      auto res = GetConnection().Execute("select $1", v);
      res.Front().To(v);
    }
  });
}

BENCHMARK_F(PgConnection, Int32Roundtrip)(benchmark::State& state) {
  RunStandalone(state, [this, &state] {
    std::int32_t v = std::numeric_limits<std::int32_t>::max();
    for (auto _ : state) {
      auto res = GetConnection().Execute("select $1", v);
      res.Front().To(v);
    }
  });
}

BENCHMARK_F(PgConnection, Int64Roundtrip)(benchmark::State& state) {
  RunStandalone(state, [this, &state] {
    std::int64_t v = std::numeric_limits<std::int64_t>::max();
    for (auto _ : state) {
      auto res = GetConnection().Execute("select $1", v);
      res.Front().To(v);
    }
  });
}

/*BENCHMARK_F(PgConnection, SelectLotsOfIntColumns)(benchmark::State& state) {
  RunStandalone(state, [this, &state] {
    struct Row final {
      std::int32_t a{};
      std::int32_t b{};
      std::int32_t c{};
      std::int32_t d{};
      std::int32_t e{};
      std::int32_t f{};
      std::int32_t g{};
      std::int32_t h{};
      std::int32_t j{};
      std::int32_t k{};

      bool operator==(const Row& other) const {
        return boost::pfr::structure_tie(*this) ==
               boost::pfr::structure_tie(other);
      }
    };
    const pg::Query query{"SELECT * FROM userver_pg_bench"};
    const pg::CommandControl cc{
        std::chrono::milliseconds{2000},
        std::chrono::milliseconds{2000},
    };
    for (auto _ : state) {
      auto rows = GetConnection()
                      .Execute(query, {}, cc)
                      .AsContainer<std::vector<Row>>(pg::kRowTag);

      state.PauseTiming();
      if (rows.size() != 100000 || rows[55471].a != 55471) {
        state.SkipWithError("SELECT IS BROKEN");
      }
      std::vector<Row>{}.swap(rows);
      state.ResumeTiming();
    }
  });
}*/

}  // namespace

USERVER_NAMESPACE_END
