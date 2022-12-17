#include <storages/mysql/impl/mysql_connection.hpp>

#include <stdexcept>

#include <fmt/format.h>

#include <userver/clients/dns/resolver.hpp>
#include <userver/logging/log.hpp>
#include <userver/tracing/scope_time.hpp>
#include <userver/utils/assert.hpp>

#include <storages/mysql/impl/metadata/mysql_server_info.hpp>
#include <storages/mysql/impl/mysql_plain_query.hpp>
#include <storages/mysql/settings/settings.hpp>
#include <userver/storages/mysql/impl/io/extractor.hpp>

USERVER_NAMESPACE_BEGIN

namespace storages::mysql::impl {

namespace {

constexpr std::chrono::milliseconds kDefaultCloseTimeout{200};

}

MySQLConnection::MySQLConnection(clients::dns::Resolver& resolver,
                                 const settings::EndpointInfo& endpoint_info,
                                 const settings::AuthSettings& auth_settings,
                                 engine::Deadline deadline)
    : socket_{-1, 0}, statements_cache_{*this, 10} {
  InitSocket(resolver, endpoint_info, auth_settings, deadline);
  server_info_ = metadata::MySQLServerInfo::Get(mysql_);

  const auto server_info = metadata::MySQLServerInfo::Get(mysql_);
  LOG_INFO() << "MySQL connection initialized."
             << " Server type: " << server_info.server_type_str << " "
             << server_info.server_version.ToString();
}

MySQLConnection::~MySQLConnection() {
  // We close the connection before resetting statements cache, so that reset
  // doesn't do potentially slow I/O

  // quoting the docs:
  // mysql_close() sends a COM_QUIT request to the server, though it does not
  // wait for any reply. Thus, theoretically it can block (if the socket buffer
  // is full), though in practise it is probably unlikely to occur frequently.
  Close(engine::Deadline::FromDuration(kDefaultCloseTimeout));
}

MySQLResult MySQLConnection::ExecutePlain(const std::string& query,
                                          engine::Deadline deadline) {
  auto broken_guard = GetBrokenGuard();

  MySQLPlainQuery mysql_query{*this, query};
  mysql_query.Execute(deadline);
  return mysql_query.FetchResult(deadline);
}

MySQLStatementFetcher MySQLConnection::ExecuteStatement(
    const std::string& statement, io::ParamsBinderBase& params,
    engine::Deadline deadline, std::optional<std::size_t> batch_size) {
  auto broken_guard = GetBrokenGuard();

  auto& mysql_statement = PrepareStatement(statement, deadline, batch_size);

  return mysql_statement.Execute(deadline, params);
}

void MySQLConnection::ExecuteInsert(const std::string& insert_statement,
                                    io::ParamsBinderBase& params,
                                    engine::Deadline deadline) {
  ExecuteStatement(insert_statement, params, deadline, std::nullopt);
}

void MySQLConnection::Ping(engine::Deadline deadline) {
  auto broken_guard = GetBrokenGuard();

  int err = 0;
  socket_.RunToCompletion(
      [this, &err] { return mysql_ping_start(&err, &mysql_); },
      [this, &err](int mysql_events) {
        return mysql_ping_cont(&err, &mysql_, mysql_events);
      },
      deadline);

  if (err != 0) {
    throw std::runtime_error{GetNativeError("Failed to ping the server")};
  }
}

void MySQLConnection::Commit(engine::Deadline deadline) {
  auto broken_guard = GetBrokenGuard();

  my_bool err = 0;
  socket_.RunToCompletion(
      [this, &err] { return mysql_commit_start(&err, &mysql_); },
      [this, &err](int mysql_events) {
        return mysql_commit_cont(&err, &mysql_, mysql_events);
      },
      deadline);

  if (err != 0) {
    throw std::runtime_error{GetNativeError("Failed to commit a transaction")};
  }
}

void MySQLConnection::Rollback(engine::Deadline deadline) {
  auto broken_guard = GetBrokenGuard();

  my_bool err = 0;
  socket_.RunToCompletion(
      [this, &err] { return mysql_rollback_start(&err, &mysql_); },
      [this, &err](int mysql_events) {
        return mysql_rollback_cont(&err, &mysql_, mysql_events);
      },
      deadline);

  if (err != 0) {
    throw std::runtime_error{
        GetNativeError("Failed to rollback a transaction")};
  }
}

MySQLSocket& MySQLConnection::GetSocket() { return socket_; }

MYSQL& MySQLConnection::GetNativeHandler() { return mysql_; }

bool MySQLConnection::IsBroken() const { return broken_.load(); }

const char* MySQLConnection::GetNativeError() { return mysql_error(&mysql_); }

std::string MySQLConnection::GetNativeError(std::string_view prefix) {
  return fmt::format("{}: {}. Errno: {}", prefix, mysql_error(&mysql_),
                     mysql_errno(&mysql_));
}

std::string MySQLConnection::EscapeString(std::string_view source) {
  auto buffer = std::make_unique<char[]>(source.length() * 2 + 1);
  std::size_t escaped_length = mysql_real_escape_string(
      &mysql_, buffer.get(), source.data(), source.length());

  return std::string(buffer.get(), escaped_length);
}

const metadata::MySQLServerInfo& MySQLConnection::GetServerInfo() const {
  return server_info_;
}

MySQLConnection::BrokenGuard MySQLConnection::GetBrokenGuard() {
  return BrokenGuard{*this};
}

void MySQLConnection::InitSocket(clients::dns::Resolver& resolver,
                                 const settings::EndpointInfo& endpoint_info,
                                 const settings::AuthSettings& auth_settings,
                                 engine::Deadline deadline) {
  const auto addr_vec = resolver.Resolve(endpoint_info.host, deadline);

  for (const auto& addr : addr_vec) {
    const auto ip = addr.Domain() == engine::io::AddrDomain::kInet6
                        ? fmt::format("[{}]", addr.PrimaryAddressString())
                        : addr.PrimaryAddressString();
    if (DoInitSocket(ip, endpoint_info.port, auth_settings, deadline)) {
      return;
    } else if (deadline.IsReached()) {
      break;
    }
  }

  throw std::runtime_error{
      "Failed to connect to any of the the resolved addresses"};
}

bool MySQLConnection::DoInitSocket(const std::string& ip, std::uint32_t port,
                                   const settings::AuthSettings& auth_settings,
                                   engine::Deadline deadline) {
  mysql_init(&mysql_);
  mysql_options(&mysql_, MYSQL_OPT_NONBLOCK, nullptr);

  auto mysql_events = mysql_real_connect_start(
      &connect_ret_, &mysql_, ip.c_str(), auth_settings.user.c_str(),
      auth_settings.password.c_str(), auth_settings.database.c_str(), port,
      nullptr /* unix_socket */, 0 /* client_flags */);

  const auto fd = mysql_get_socket(&mysql_);
  if (fd == -1) {
    LOG_WARNING() << GetNativeError("Failed to connect");
    return false;
  }

  socket_.SetFd(fd);
  socket_.SetEvents(mysql_events);

  try {
    while (socket_.ShouldWait()) {
      mysql_events = socket_.Wait(deadline);
      socket_.SetEvents(
          mysql_real_connect_cont(&connect_ret_, &mysql_, mysql_events));
    }

    if (!connect_ret_) {
      LOG_WARNING() << GetNativeError("Failed to connect");
      Close(deadline);
      return false;
    }
  } catch (const std::exception& ex) {
    Close(deadline);
    return false;
  }

  return true;
}

void MySQLConnection::Close(engine::Deadline deadline) noexcept {
  UASSERT(socket_.IsValid());

  try {
    socket_.RunToCompletion([this] { return mysql_close_start(&mysql_); },
                            [this](int mysql_event) {
                              return mysql_close_cont(&mysql_, mysql_event);
                            },
                            deadline);
  } catch (const std::exception& ex) {
    LOG_WARNING() << "Failed to correctly release a connection: " << ex.what();
  }
}

MySQLStatement& MySQLConnection::PrepareStatement(
    const std::string& statement, engine::Deadline deadline,
    std::optional<std::size_t> batch_size) {
  auto& mysql_statement =
      statements_cache_.PrepareStatement(statement, deadline);

  if (batch_size.has_value()) {
    UASSERT(*batch_size > 0);
    mysql_statement.SetReadonlyCursor(*batch_size);
  } else {
    mysql_statement.SetNoCursor();
  }

  return mysql_statement;
}

MySQLConnection::BrokenGuard::BrokenGuard(MySQLConnection& connection)
    : exceptions_on_enter_{std::uncaught_exceptions()},
      broken_{connection.broken_} {
  if (broken_.load()) {
    throw std::runtime_error{"Connection is broken"};
  }
}

MySQLConnection::BrokenGuard::~BrokenGuard() {
  if (exceptions_on_enter_ != std::uncaught_exceptions()) {
    broken_.store(true);
  }
}

}  // namespace storages::mysql::impl

USERVER_NAMESPACE_END
