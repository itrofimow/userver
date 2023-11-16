#pragma once

#include <userver/storages/postgres/query.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <userver/storages/postgres/io/user_types.hpp>

#include <userver/storages/postgres/detail/connection_ptr.hpp>
#include <userver/storages/postgres/detail/query_parameters.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::postgres {

class Pipeline final {
 public:
  explicit Pipeline(detail::ConnectionPtr&& conn);

  Pipeline(Pipeline&&) noexcept;
  Pipeline& operator=(Pipeline&&) noexcept;

  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;

  ~Pipeline();

  void Reserve(std::size_t size);

  template <typename... Args>
  void AddQuery(TimeoutDuration timeout, const Query& query,
                const Args&... args);

  std::vector<ResultSet> Gather();

 private:
  const UserTypes& GetConnectionUserTypes() const;

  void DoAddQuery(TimeoutDuration timeout, const Query& query,
                  detail::DynamicQueryParameters&& params);

  void ValidateUsage() const;

  struct QueryMeta final {
    Query query;
    std::string prepared_statement_name;
    detail::DynamicQueryParameters params;

    QueryMeta(const Query& query, std::string prepared_statement_name,
              detail::DynamicQueryParameters&& params);
  };

  detail::ConnectionPtr conn_;
  std::vector<QueryMeta> queries_;
};

template <typename... Args>
void Pipeline::AddQuery(TimeoutDuration timeout, const Query& query,
                        const Args&... args) {
  detail::DynamicQueryParameters params;
  params.Write(GetConnectionUserTypes(), args...);
  DoAddQuery(timeout, query, std::move(params));
}

}  // namespace storages::postgres

USERVER_NAMESPACE_END
