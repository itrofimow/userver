#include <userver/storages/postgres/pipeline.hpp>

#include <stdexcept>

#include <storages/postgres/detail/connection.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::postgres {

Pipeline::QueryMeta::QueryMeta(const Query& query,
                               std::string prepared_statement_name,
                               detail::DynamicQueryParameters&& params)
    : query{query},
      prepared_statement_name{std::move(prepared_statement_name)},
      params{std::move(params)} {}

Pipeline::Pipeline(detail::ConnectionPtr&& conn) : conn_{std::move(conn)} {}

Pipeline::Pipeline(Pipeline&&) noexcept = default;

Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

Pipeline::~Pipeline() {
  if (conn_) {
    Gather();
  }
}

void Pipeline::Reserve(std::size_t size) { queries_.reserve(size); }

std::vector<ResultSet> Pipeline::Gather() {
  ValidateUsage();

  tracing::Span gather_span{"pipeline_gather"};
  auto scope = gather_span.CreateScopeTime();

  for (const auto& meta : queries_) {
    conn_->AddIntoPipeline(meta.prepared_statement_name,
                           detail::QueryParameters{meta.params}, scope);
  }

  auto result = conn_->GatherPipeline();

  { [[maybe_unused]] const detail::ConnectionPtr tmp{std::move(conn_)}; }

  return result;
}

const UserTypes& Pipeline::GetConnectionUserTypes() const {
  return conn_->GetUserTypes();
}

void Pipeline::DoAddQuery(TimeoutDuration timeout, const Query& query,
                          detail::DynamicQueryParameters&& params) {
  ValidateUsage();

  auto prepared_statement_name =
      conn_->PrepareStatement(query, detail::QueryParameters{params}, timeout);
  queries_.emplace_back(query, std::move(prepared_statement_name),
                        std::move(params));
}

void Pipeline::ValidateUsage() const {
  if (!conn_) {
    throw std::logic_error{"The pipeline is finalized and no longer usable."};
  }
}

}  // namespace storages::postgres

USERVER_NAMESPACE_END
