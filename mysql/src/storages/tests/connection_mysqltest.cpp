#include <userver/utest/utest.hpp>

#include <iostream>

#include <storages/mysql/impl/mysql_connection.hpp>
#include <storages/mysql/infra/pool.hpp>

#include <userver/storages/mysql/io/extractor.hpp>
#include <userver/storages/mysql/result_set.hpp>

#include <userver/engine/sleep.hpp>

USERVER_NAMESPACE_BEGIN

UTEST(Connection, Works) { storages::mysql::impl::MySQLConnection conn{}; }

UTEST(Connection, ExecuteWorks) {
  storages::mysql::impl::MySQLConnection conn{};

  const auto res = conn.ExecutePlain("SELECT Id, Value FROM test", {});

  for (const auto& row : res) {
    for (const auto& field : row) {
      std::cout << field << "; ";
    }
    std::cout << std::endl;
  }
}

namespace {

struct Row final {
  int id;
  std::string value;
};

}  // namespace

UTEST(Connection, TypedWorks) {
  storages::mysql::impl::MySQLConnection conn{};

  const auto get_res = [&conn] {
    return storages::mysql::ResultSet{
        conn.ExecutePlain("SELECT Id, Value FROM test", {})};
  };

  {
    auto res = get_res();

    auto typed = std::move(res).AsRows<Row>();
    for (auto& row : typed) {
      std::cout << row.id << " " << row.value << std::endl;
    }
  }
  { auto res = get_res().AsContainer<std::vector<Row>>(); }
}

/*UTEST(Connection, PreparedWorks) {
  storages::mysql::impl::MySQLConnection conn{};

  // yeap, this type is in public includes, yet connection is private
  storages::mysql::io::TypedExtractor<Row> extractor{};
  conn.ExecuteStatement("SELECT Id, Value FROM test", extractor, {});
  auto data = std::move(extractor).ExtractData();

  for (const auto& row : data) {
    std::cout << row.id << " | " << row.value << std::endl;
  }
}*/

UTEST(Pool, Works) { const auto pool = storages::mysql::infra::Pool::Create(); }

USERVER_NAMESPACE_END
