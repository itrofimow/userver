#include <userver/utest/utest.hpp>

#include <iostream>

#include <userver/engine/sleep.hpp>

#include <userver/storages/mysql/cluster.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

constexpr auto kMasterHost = storages::mysql::ClusterHostType::kMaster;

struct Row final {
  int id{};
  std::string value;
};

struct Id final {
  std::int32_t id{};
};

}  // namespace

UTEST(Cluster, TypedWorks) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  auto rows =
      cluster
          .Execute(storages::mysql::ClusterHostType::kMaster, deadline,
                   "SELECT Id, Value FROM test WHERE Id=? OR Value=?", 1, "two")
          .AsVector<Row>();

  for (const auto& [id, value] : rows) {
    // std::cout << id << ": " << value << std::endl;
  }
}

UTEST(Cluster, TypedSizeMismatch) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  auto rows = cluster.Execute(kMasterHost, deadline, "SELECT Id FROM test")
                  .AsVector<Id>();

  // for (const auto [id] : rows) {
  // std::cout << id << " ";
  //}
  std::cout << std::endl;
}

UTEST(Cluster, TypedEmptyResult) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  auto rows =
      cluster.Execute(kMasterHost, deadline,
                      "INSERT INTO test(Id, Value) VALUES(?, ?)", 5, "five");
}

UTEST(Cluster, AsSingleRow) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  const auto select_with_limit = [&cluster, deadline](int limit) {
    return cluster.Execute(kMasterHost, deadline,
                           "SELECT Id, Value FROM test limit ?", limit);
  };

  EXPECT_ANY_THROW(select_with_limit(0).AsSingleRow<Row>());
  EXPECT_ANY_THROW(select_with_limit(2).AsSingleRow<Row>());
  EXPECT_NO_THROW(select_with_limit(1).AsSingleRow<Row>());
}

UTEST(Cluster, InsertMany) {
  const auto deadline =
      engine::Deadline::FromDuration(std::chrono::seconds{500});

  storages::mysql::Cluster cluster{};

  std::vector<Row> rows{{11, "55zxc"}, {22, "66asdwe"}, {33, "77ok"}};
  cluster.InsertMany(deadline, "INSERT INTO test(Id, Value) VALUES(?, ?)",
                     rows);
}

UTEST(Cluster, InsertManySimple) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  std::vector<Id> ids{{1}, {2}, {3}, {4}, {5}, {6}};
  cluster.InsertMany(deadline, "INSERT INTO test(Id) VALUES(?)", ids);
}

UTEST(Cluster, InsertOne) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  Row row{7, "seven"};
  cluster.InsertOne(deadline, "INSERT INTO test(Id, Value) VALUES(?, ?)", row);
}

UTEST(StreamedResult, Works) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  auto stream =
      cluster.Select(kMasterHost, deadline, "SELECT Id, Value FROM test")
          .AsStreamOf<Row>();

  std::move(stream).ForEach(
      [](Row&&) {
        // std::cout << row.id << " " << row.value << std::endl;
      },
      1, deadline);
}

UTEST(Cluster, DISABLED_BigInsert) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  const std::string long_string_to_avoid_sso{
      "hi i am some long string that doesn't fit in sso"};

  constexpr int kRowsCount = 100000;

  std::vector<Row> rows;
  rows.reserve(kRowsCount);
  for (int i = 0; i < kRowsCount; ++i) {
    rows.push_back({i, long_string_to_avoid_sso});
  }

  cluster.InsertMany(deadline, "INSERT INTO test(Id, Value) VALUES(?, ?)",
                     rows);

  rows = cluster.Execute(kMasterHost, deadline, "SELECT Id, Value FROM test")
             .AsVector<Row>();

  // cluster.Execute(kMasterHost, deadline, "DELETE FROM test");
}

UTEST(Cluster, WorksWithConsts) {
  const auto deadline = engine::Deadline::FromDuration(utest::kMaxTestWaitTime);

  storages::mysql::Cluster cluster{};

  const int id = 5;
  cluster.Select(kMasterHost, deadline, "SELECT Id, Value FROM test WHERE Id=?",
                 id);
}

USERVER_NAMESPACE_END
