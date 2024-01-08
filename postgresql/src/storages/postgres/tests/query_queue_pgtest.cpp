#include <storages/postgres/tests/util_pgtest.hpp>

#include <userver/storages/postgres/query_queue.hpp>

USERVER_NAMESPACE_BEGIN

namespace pg = storages::postgres;

const pg::TimeoutDuration kPrepareTimeout{std::chrono::milliseconds{1000}};
const pg::TimeoutDuration kGatherTimeout{std::chrono::milliseconds{1000}};

using QueryQueueResult = std::vector<pg::ResultSet>;

UTEST_P(PostgreConnection, QueryQueueSelectOne) {
  if (GetParam().pipeline_mode != pg::PipelineMode::kEnabled) {
    return;
  }

  CheckConnection(GetConn());
  pg::QueryQueue query_queue{std::move(GetConn())};

  UEXPECT_NO_THROW(query_queue.Push(kPrepareTimeout, "SELECT 1"));
  QueryQueueResult result{};
  UEXPECT_NO_THROW(result = query_queue.Collect(kGatherTimeout));

  EXPECT_EQ(1, result.size());
  EXPECT_EQ(1, result.front().AsSingleRow<int>());
}

UTEST_P(PostgreConnection, QueryQueueSelectMultiple) {
  if (GetParam().pipeline_mode != pg::PipelineMode::kEnabled) {
    return;
  }

  CheckConnection(GetConn());
  pg::QueryQueue query_queue{std::move(GetConn())};

  constexpr int kQueriesCount = 5;
  for (int i = 0; i < kQueriesCount; ++i) {
    UEXPECT_NO_THROW(query_queue.Push(kPrepareTimeout, "SELECT $1", i));
  }
  QueryQueueResult result{};
  UEXPECT_NO_THROW(result = query_queue.Collect(kGatherTimeout));

  EXPECT_EQ(kQueriesCount, result.size());
  for (int i = 0; i < kQueriesCount; ++i) {
    EXPECT_EQ(i, result[i].AsSingleRow<int>());
  }
}

UTEST_P(PostgreConnection, QueryQueueTimeout) {
  if (GetParam().pipeline_mode != pg::PipelineMode::kEnabled) {
    return;
  }

  CheckConnection(GetConn());
  pg::QueryQueue query_queue{std::move(GetConn())};

  UEXPECT_NO_THROW(query_queue.Push(kPrepareTimeout, "SELECT 1"));
  UEXPECT_NO_THROW(query_queue.Push(kPrepareTimeout, "SELECT pg_sleep(1)"));

  QueryQueueResult result{};
  UEXPECT_THROW(result = query_queue.Collect(std::chrono::milliseconds{100}),
                pg::ConnectionTimeoutError);
}

UTEST_P(PostgreConnection, QueryQueueFinalized) {
  if (GetParam().pipeline_mode != pg::PipelineMode::kEnabled) {
    return;
  }

  CheckConnection(GetConn());
  pg::QueryQueue query_queue{std::move(GetConn())};

  UEXPECT_NO_THROW(query_queue.Push(kPrepareTimeout, "SELECT 1"));
  QueryQueueResult result{};
  UEXPECT_NO_THROW(result = query_queue.Collect(kGatherTimeout));

  UEXPECT_THROW(result = query_queue.Collect(kGatherTimeout), std::logic_error);
}

UTEST_P(PostgreConnection, QueryQueueRequiresLibpqPipelining) {
  if (GetParam().pipeline_mode == pg::PipelineMode::kEnabled) {
    // Pass if pipelining is enabled
    return;
  }

  CheckConnection(GetConn());
  UEXPECT_THROW(pg::QueryQueue{std::move(GetConn())}, std::logic_error);
}

USERVER_NAMESPACE_END
