#include <userver/storages/postgres/query_queue.hpp>

#include <boost/container/small_vector.hpp>
#include <stdexcept>

#include <storages/postgres/detail/connection.hpp>
#include <userver/utils/scope_guard.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::postgres {

struct QueryQueue::QueriesStorage final {
  struct QueryMeta final {
    std::string prepared_statement_name;
    std::unique_ptr<ParamsStorageBase> params;

    QueryMeta() = default;

    QueryMeta(QueryMeta&&) noexcept = default;
    QueryMeta& operator=(QueryMeta&&) noexcept = default;

    QueryMeta(std::string prepared_statement_name,
              std::unique_ptr<ParamsStorageBase>&& params)
        : prepared_statement_name{std::move(prepared_statement_name)},
          params{std::move(params)} {}
    ~QueryMeta() = default;
  };

  boost::container::small_vector<QueryMeta, 24> queries;
};

QueryQueue::ParamsStorageBase::~ParamsStorageBase() = default;

std::size_t QueryQueue::ParamsStorageBase::Size() const { return DoSize(); }

const char* const* QueryQueue::ParamsStorageBase::ParamBuffers() const {
  return DoParamBuffers();
}

const Oid* QueryQueue::ParamsStorageBase::ParamTypesBuffer() const {
  return DoParamTypesBuffer();
}

const int* QueryQueue::ParamsStorageBase::ParamLengthsBuffer() const {
  return DoParamLengthsBuffer();
}

const int* QueryQueue::ParamsStorageBase::ParamFormatsBuffer() const {
  return DoParamFormatsBuffer();
}

QueryQueue::QueryQueue(detail::ConnectionPtr&& conn) : conn_{std::move(conn)} {
  if (!conn_->IsPipelineActive()) {
    throw std::logic_error{"Your client doesn't support pipelining"};
  }
}

QueryQueue::QueryQueue(QueryQueue&&) noexcept = default;

QueryQueue& QueryQueue::operator=(QueryQueue&&) noexcept = default;

QueryQueue::~QueryQueue() = default;

void QueryQueue::Reserve(std::size_t size) {
  queries_storage_->queries.reserve(size);
}

std::vector<ResultSet> QueryQueue::Collect(TimeoutDuration timeout) {
  ValidateUsage();

  tracing::Span collect_span{"query_queue_collect"};
  auto scope = collect_span.CreateScopeTime();

  const USERVER_NAMESPACE::utils::ScopeGuard reset_guard{[this] {
    [[maybe_unused]] const detail::ConnectionPtr tmp{std::move(conn_)};
  }};

  if (queries_storage_->queries.empty()) {
    return {};
  }

  for (const auto& meta : queries_storage_->queries) {
    // TODO : think about per-statement timeouts here
    conn_->AddIntoPipeline(meta.prepared_statement_name,
                           detail::QueryParameters{*meta.params}, scope);
  }

  auto result = conn_->GatherPipeline(timeout);
  if (result.size() != queries_storage_->queries.size()) {
    throw std::runtime_error{"TODO results count mismatch"};
  }
  return result;
}

const UserTypes& QueryQueue::GetConnectionUserTypes() const {
  return conn_->GetUserTypes();
}

void QueryQueue::DoPush(TimeoutDuration prepare_timeout, const Query& query,
                        std::unique_ptr<ParamsStorageBase>&& params) {
  ValidateUsage();

  auto prepared_statement_name = conn_->PrepareStatement(
      query, detail::QueryParameters{*params}, prepare_timeout);
  queries_storage_->queries.emplace_back(std::move(prepared_statement_name),
                                         std::move(params));
}

void QueryQueue::ValidateUsage() const {
  if (!conn_) {
    throw std::logic_error{
        "The query queue is finalized and no longer usable."};
  }
}

}  // namespace storages::postgres

USERVER_NAMESPACE_END
