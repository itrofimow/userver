#pragma once

/// @file userver/storages/postgres/query_queue.hpp
/// @brief An utility to execute multiple queries in a single network
/// round-trip.

#include <memory>

#include <userver/storages/postgres/query.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <userver/storages/postgres/io/user_types.hpp>

#include <userver/storages/postgres/detail/connection_ptr.hpp>
#include <userver/storages/postgres/detail/query_parameters.hpp>

#include <userver/utils/fast_pimpl.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::postgres {

/// @brief A container to enqueue queries in FIFO order and execute them all
/// within a single network round-trip.
///
/// Acquired from `Cluster`, one is expected to `Push` some queries into the
/// queue and then `Collect` them into vector of results.
///
/// From the client point of view `Collect` is transactional: either all the
/// queries succeed or `Collect` rethrows the first error encountered. However,
/// this is *NOT* the case for the server: server treats all the provided
/// queries independently and is likely to  execute subsequent queries even
/// after prior failures.
/// Due to this it's actively discouraged to queue any but read-only queries.
///
/// @warning No transactional guarantees are provided, using the class for
/// modifying queries is actively discouraged.
///
/// @note Queries may or may not be sent to the server prior to `Collect` call.
///
/// @note Requires pipelining to be enabled in the driver,
/// throws `std::logic_error` on construction otherwise.
class QueryQueue final {
 public:
  explicit QueryQueue(detail::ConnectionPtr&& conn);

  QueryQueue(QueryQueue&&) noexcept;
  QueryQueue& operator=(QueryQueue&&) noexcept;

  QueryQueue(const QueryQueue&) = delete;
  QueryQueue& operator=(const QueryQueue&) = delete;

  ~QueryQueue();

  /// Reserve internal storage to hold this amount of queries.
  void Reserve(std::size_t size);

  /// Add a query into the queue.
  /// This method might prepare the query server-side, if needed.
  template <typename... Args>
  void Push(TimeoutDuration prepare_timeout, const Query& query,
            const Args&... args);

  /// Collect results of all the queued queries.
  /// Either returns a vector of N `ResultSet`s, where N is the number of
  /// queries enqueued, or rethrow the first error encountered, be that a query
  /// execution error or a timeout.
  [[nodiscard]] std::vector<ResultSet> Collect(TimeoutDuration timeout);

 private:
  class ParamsStorageBase {
   public:
    virtual ~ParamsStorageBase();

    std::size_t Size() const;
    const char* const* ParamBuffers() const;
    const Oid* ParamTypesBuffer() const;
    const int* ParamLengthsBuffer() const;
    const int* ParamFormatsBuffer() const;

   protected:
    virtual std::size_t DoSize() const = 0;
    virtual const char* const* DoParamBuffers() const = 0;
    virtual const Oid* DoParamTypesBuffer() const = 0;
    virtual const int* DoParamLengthsBuffer() const = 0;
    virtual const int* DoParamFormatsBuffer() const = 0;
  };

  template <std::size_t ParamsCount>
  class ParamsStorage final : public QueryQueue::ParamsStorageBase {
   public:
    ~ParamsStorage() override = default;

    detail::StaticQueryParameters<ParamsCount>& GetParams() { return params_; }

   private:
    const detail::StaticQueryParameters<ParamsCount>& GetParams() const {
      return params_;
    }

    std::size_t DoSize() const override { return GetParams().Size(); }

    const char* const* DoParamBuffers() const override {
      return GetParams().ParamBuffers();
    }

    const Oid* DoParamTypesBuffer() const override {
      return GetParams().ParamTypesBuffer();
    }

    const int* DoParamLengthsBuffer() const override {
      return GetParams().ParamLengthsBuffer();
    }

    const int* DoParamFormatsBuffer() const override {
      return GetParams().ParamFormatsBuffer();
    }

    detail::StaticQueryParameters<ParamsCount> params_;
  };

  const UserTypes& GetConnectionUserTypes() const;

  void DoPush(TimeoutDuration prepare_timeout, const Query& query,
              std::unique_ptr<ParamsStorageBase>&& params);

  void ValidateUsage() const;

  detail::ConnectionPtr conn_;

  struct QueriesStorage;
  USERVER_NAMESPACE::utils::FastPimpl<QueriesStorage, 1024, 8> queries_storage_;
};

template <typename... Args>
void QueryQueue::Push(TimeoutDuration prepare_timeout, const Query& query,
                      const Args&... args) {
  auto params = std::make_unique<ParamsStorage<sizeof...(args)>>();
  params->GetParams().Write(GetConnectionUserTypes(), args...);
  DoPush(prepare_timeout, query, std::move(params));
}

}  // namespace storages::postgres

USERVER_NAMESPACE_END
