#include "pool_impl.hpp"

#include <memory>
#include <string>
#include <vector>

#include <userver/engine/async.hpp>
#include <userver/engine/get_all.hpp>

#include <storages/clickhouse/impl/connection.hpp>
#include <storages/clickhouse/impl/connection_ptr.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::clickhouse::impl {

namespace {

constexpr size_t kMaxSimultaneouslyConnectingClients{5};

constexpr std::chrono::seconds kMaintenanceInterval{2};
constexpr std::chrono::seconds PoolUnavailableThreshold{60};
static_assert(PoolUnavailableThreshold > kMaintenanceInterval);

const std::string kMaintenanceTaskName = "clickhouse_maintain";

struct ConnectionDeleter final {
  void operator()(Connection* conn) noexcept { delete conn; }
};

}  // namespace

bool PoolAvailabilityMonitor::IsAvailable() const {
  const auto last_successful_communication =
      last_successful_communication_.load();
  if (last_successful_communication == TimePoint{}) {
    return last_unsuccessful_communication_.load() == TimePoint{};
  }

  const auto now = Clock::now();
  return std::chrono::duration_cast<std::chrono::seconds>(
             now - last_successful_communication) < PoolUnavailableThreshold;
}

void PoolAvailabilityMonitor::AccountSuccess() noexcept {
  last_successful_communication_ = Clock::now();
}

void PoolAvailabilityMonitor::AccountFailure() noexcept {
  last_unsuccessful_communication_ = Clock::now();
}

struct PoolImpl::MaintenanceConnectionDeleter final {
  void operator()(Connection* conn) noexcept { pool.DoRelease(conn); }

  PoolImpl& pool;
};

PoolImpl::PoolImpl(clients::dns::Resolver& resolver, PoolSettings&& settings)
    : resolver_{resolver},
      pool_settings_{std::move(settings)},
      given_away_semaphore_{pool_settings_.max_pool_size},
      connecting_semaphore_{kMaxSimultaneouslyConnectingClients},
      // FP?: pointer magic in boost.lockfree
      // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
      queue_{pool_settings_.max_pool_size} {
  std::vector<engine::TaskWithResult<void>> tasks;
  tasks.reserve(pool_settings_.initial_pool_size);
  for (size_t i = 0; i < pool_settings_.initial_pool_size; ++i) {
    tasks.emplace_back(engine::AsyncNoSpan([this] { PushConnection(); }));
  }
  engine::GetAll(tasks);
}

PoolImpl::~PoolImpl() {
  StopMaintenance();

  Connection* conn = nullptr;
  while (queue_.pop(conn)) Drop(conn);
}

bool PoolImpl::IsAvailable() const {
  return availability_monitor_.IsAvailable();
}

ConnectionPtr PoolImpl::Acquire() { return {shared_from_this(), Pop()}; }

void PoolImpl::Release(Connection* conn) {
  UASSERT(conn);

  DoRelease(conn);

  given_away_semaphore_.unlock_shared();
  --GetStatistics().connections.busy;
}

void PoolImpl::DoRelease(Connection* conn) noexcept {
  UASSERT(conn);

  if (!conn->IsBroken()) {
    availability_monitor_.AccountSuccess();
  }

  if (conn->IsBroken() || !queue_.bounded_push(conn)) Drop(conn);
}

stats::PoolStatistics& PoolImpl::GetStatistics() noexcept {
  return statistics_;
}

const std::string& PoolImpl::GetHostName() const {
  return pool_settings_.endpoint_settings.host;
}

stats::StatementTimer PoolImpl::GetExecuteTimer() {
  return stats::StatementTimer{statistics_.queries};
}

stats::StatementTimer PoolImpl::GetInsertTimer() {
  return stats::StatementTimer{statistics_.inserts};
}

Connection* PoolImpl::Create() {
  try {
    auto conn = std::make_unique<Connection>(
        resolver_, pool_settings_.endpoint_settings,
        pool_settings_.auth_settings, pool_settings_.connection_settings);

    auto& stats = GetStatistics().connections;
    ++stats.created;
    ++stats.active;
    ++size_;

    return conn.release();
  } catch (const std::exception&) {
    availability_monitor_.AccountFailure();
    throw;
  }
}

void PoolImpl::PushConnection() {
  try {
    auto* conn = Create();
    if (!queue_.bounded_push(conn)) {
      Drop(conn);
    }
  } catch (const std::exception& e) {
    LOG_ERROR() << "Failed to create connection: " << e;
  }
}

void PoolImpl::Drop(Connection* conn) noexcept {
  ConnectionDeleter{}(conn);

  auto& stats = GetStatistics().connections;
  ++stats.closed;
  --stats.active;

  --size_;
}

Connection* PoolImpl::Pop() {
  const auto deadline =
      engine::Deadline::FromDuration(pool_settings_.queue_timeout);

  engine::SemaphoreLock given_away_lock{given_away_semaphore_, deadline};
  if (!given_away_lock) {
    ++GetStatistics().connections.overload;
    throw std::runtime_error{"queue wait limit exceeded"};
  }

  auto* conn = TryPop();
  if (!conn) {
    engine::SemaphoreLock connecting_lock{connecting_semaphore_, deadline};

    conn = TryPop();
    if (!conn) {
      if (!connecting_lock) {
        ++GetStatistics().connections.overload;
        throw std::runtime_error{"connection queue wait limit exceeded"};
      }
      conn = Create();
    }
  }

  UASSERT(conn);
  UASSERT(!conn->IsBroken());
  given_away_lock.Release();
  ++GetStatistics().connections.busy;

  return conn;
}

Connection* PoolImpl::TryPop() {
  Connection* conn = nullptr;
  if (queue_.pop(conn)) return conn;

  return nullptr;
}

void PoolImpl::StartMaintenance() {
  using PeriodicTask = USERVER_NAMESPACE::utils::PeriodicTask;

  maintenance_task_.Start(
      kMaintenanceTaskName,
      {kMaintenanceInterval,
       {PeriodicTask::Flags::kStrong, PeriodicTask::Flags::kCritical}},
      [this] { MaintainConnections(); });
}

void PoolImpl::StopMaintenance() { maintenance_task_.Stop(); }

void PoolImpl::MaintainConnections() {
  Connection* raw_conn = TryPop();
  if (!raw_conn) {
    if (size_ < pool_settings_.initial_pool_size) {
      PushConnection();
    }

    return;
  }

  {
    using Deleter = PoolImpl::MaintenanceConnectionDeleter;
    auto conn = std::unique_ptr<Connection, Deleter>{raw_conn, Deleter{*this}};
    try {
      conn->Ping();
    } catch (const std::exception& ex) {
      LOG_LIMITED_WARNING() << "Exception while pinging connection to '"
                            << GetHostName() << "': " << ex;
    }
  }

  if (size_ < pool_settings_.initial_pool_size) {
    PushConnection();
  }
}

}  // namespace storages::clickhouse::impl

USERVER_NAMESPACE_END
