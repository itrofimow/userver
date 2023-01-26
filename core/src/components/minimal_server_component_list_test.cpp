#include <userver/components/minimal_server_component_list.hpp>

#include <fmt/format.h>

#include <userver/components/run.hpp>
#include <userver/fs/blocking/read.hpp>
#include <userver/fs/blocking/temp_directory.hpp>  // for fs::blocking::TempDirectory
#include <userver/fs/blocking/write.hpp>  // for fs::blocking::RewriteFileContents

#include <components/component_list_test.hpp>
#include <userver/utest/utest.hpp>

USERVER_NAMESPACE_BEGIN

namespace {

constexpr std::string_view kRuntimeConfigMissingParam = R"~({
  "USERVER_TASK_PROCESSOR_PROFILER_DEBUG": {},
  "USERVER_LOG_REQUEST": true,
  "USERVER_CHECK_AUTH_IN_HANDLERS": false,
  "USERVER_HTTP_PROXY": "",
  "USERVER_CANCEL_HANDLE_REQUEST_BY_DEADLINE": false,
  "USERVER_NO_LOG_SPANS":{"names":[], "prefixes":[]},
  "USERVER_TASK_PROCESSOR_QOS": {
    "default-service": {
      "default-task-processor": {
        "wait_queue_overload": {
          "action": "ignore",
          "length_limit": 5000,
          "time_limit_us": 3000
        }
      }
    }
  },
  "USERVER_CACHES": {},
  "USERVER_RPS_CCONTROL_ACTIVATED_FACTOR_METRIC": 5,
  "USERVER_LRU_CACHES": {},
  "USERVER_DUMPS": {},
  "USERVER_HANDLER_STREAM_API_ENABLED": false,
  "HTTP_CLIENT_CONNECTION_POOL_SIZE": 1000,
  "HTTP_CLIENT_CONNECT_THROTTLE": {
    "max-size": 100,
    "token-update-interval-ms": 0
  },
  "HTTP_CLIENT_ENFORCE_TASK_DEADLINE": {
    "cancel-request": false,
    "update-timeout": false
  },
  "USERVER_RPS_CCONTROL_ENABLED": true,
  "USERVER_RPS_CCONTROL": {
    "down-level": 8,
    "down-rate-percent": 1,
    "load-limit-crit-percent": 50,
    "load-limit-percent": 0,
    "min-limit": 2,
    "no-limit-seconds": 300,
    "overload-off-seconds": 8,
    "overload-on-seconds": 8,
    "up-level": 2,
    "up-rate-percent": 1
  },
  "USERVER_RPS_CCONTROL_CUSTOM_STATUS": {},
  "SAMPLE_INTEGER_FROM_RUNTIME_CONFIG": 42
})~";

constexpr std::string_view kStaticConfig = R"(
components_manager:
  coro_pool:
    initial_size: 50
    max_size: 500
  default_task_processor: main-task-processor
  event_thread_pool:
    threads: 4
# /// [Sample task-switch tracing]
# yaml
  task_processors:
    fs-task-processor:
      thread_name: fs-worker
      worker_threads: 2
    main-task-processor:
      thread_name: main-worker
      worker_threads: 4
      task-trace:
        every: 1
        max-context-switch-count: 50
        logger: tracer
  components:
    logging:
      fs-task-processor: fs-task-processor
      loggers:
        tracer:
          file_path: $tracer_log_path
          file_path#fallback: '@null'
          level: $tracer_level  # set to debug to get stacktraces
          level#fallback: info
# /// [Sample task-switch tracing]
        default:
          file_path: '@stderr'
          level: warning
    tracer:
        service-name: config-service
    dynamic-config:
      fs-cache-path: $runtime_config_path
      fs-task-processor: main-task-processor
    dynamic-config-fallbacks:
        fallback-path: $runtime_config_path
    server:
      listener:
          port: 8087
          task_processor: main-task-processor
    statistics-storage: # Nothing
    auth-checker-settings: # Nothing
    manager-controller:  # Nothing
config_vars: )";

class ServerMinimalComponentList : public ComponentList {
 protected:
  const std::string& GetTempRoot() const { return temp_root_.GetPath(); }

  std::string GetRuntimeConfigPath() const {
    return temp_root_.GetPath() + "/runtime_config.json";
  }

  std::string GetConfigVarsPath() const {
    return temp_root_.GetPath() + "/config_vars.json";
  }

  const std::string& GetStaticConfig() const { return static_config_; }

 private:
  fs::blocking::TempDirectory temp_root_ =
      fs::blocking::TempDirectory::Create();
  std::string static_config_ = std::string{kStaticConfig} + GetConfigVarsPath();
};

}  // namespace

TEST_F(ServerMinimalComponentList, Basic) {
  constexpr std::string_view kConfigVarsTemplate = R"(
    runtime_config_path: {0}
  )";
  const auto config_vars =
      fmt::format(kConfigVarsTemplate, GetRuntimeConfigPath());

  fs::blocking::RewriteFileContents(GetRuntimeConfigPath(),
                                    tests::kRuntimeConfig);
  fs::blocking::RewriteFileContents(GetConfigVarsPath(), config_vars);

  components::RunOnce(components::InMemoryConfig{GetStaticConfig()},
                      components::MinimalServerComponentList());
}

TEST_F(ServerMinimalComponentList, TraceSwitching) {
  constexpr std::string_view kConfigVarsTemplate = R"(
    runtime_config_path: {0}
    tracer_log_path: {1}
  )";
  const std::string logs_path = GetTempRoot() + "/tracing_log.txt";
  const auto config_vars =
      fmt::format(kConfigVarsTemplate, GetRuntimeConfigPath(), logs_path);

  fs::blocking::RewriteFileContents(GetRuntimeConfigPath(),
                                    tests::kRuntimeConfig);
  fs::blocking::RewriteFileContents(GetConfigVarsPath(), config_vars);

  components::RunOnce(components::InMemoryConfig{GetStaticConfig()},
                      components::MinimalServerComponentList());

  logging::LogFlush();

  const auto logs = fs::blocking::ReadFileContents(logs_path);
  EXPECT_NE(logs.find(" changed state to kQueued"), std::string::npos);
  EXPECT_NE(logs.find(" changed state to kRunning"), std::string::npos);
  EXPECT_NE(logs.find(" changed state to kCompleted"), std::string::npos);
  EXPECT_EQ(logs.find("stacktrace= 0# "), std::string::npos);
}

TEST_F(ServerMinimalComponentList, TraceStacktraces) {
  constexpr std::string_view kConfigVarsTemplate = R"(
    runtime_config_path: {0}
    tracer_log_path: {1}
    tracer_level: debug
  )";
  const std::string logs_path = GetTempRoot() + "/tracing_st_log.txt";

  fs::blocking::RewriteFileContents(GetRuntimeConfigPath(),
                                    tests::kRuntimeConfig);
  fs::blocking::RewriteFileContents(
      GetConfigVarsPath(),
      fmt::format(kConfigVarsTemplate, GetRuntimeConfigPath(), logs_path));

  components::RunOnce(components::InMemoryConfig{GetStaticConfig()},
                      components::MinimalServerComponentList());

  logging::LogFlush();

  const auto logs = fs::blocking::ReadFileContents(logs_path);
  EXPECT_NE(logs.find(" changed state to kQueued"), std::string::npos);
  EXPECT_NE(logs.find(" changed state to kRunning"), std::string::npos);
  EXPECT_NE(logs.find(" changed state to kCompleted"), std::string::npos);
  EXPECT_NE(logs.find("stacktrace= 0# "), std::string::npos);
}

TEST_F(ServerMinimalComponentList, MissingRuntimeConfigParam) {
  constexpr std::string_view kConfigVarsTemplate = R"(
    runtime_config_path: {0}
  )";
  const auto config_vars =
      fmt::format(kConfigVarsTemplate, GetRuntimeConfigPath());

  fs::blocking::RewriteFileContents(GetRuntimeConfigPath(),
                                    kRuntimeConfigMissingParam);
  fs::blocking::RewriteFileContents(GetConfigVarsPath(), config_vars);

  try {
    components::RunOnce(components::InMemoryConfig{GetStaticConfig()},
                        components::MinimalServerComponentList());
    FAIL() << "Missing runtime config value was not reported";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string_view{e.what()}.find("USERVER_LOG_REQUEST_HEADERS"),
              std::string_view::npos)
        << "'USERVER_LOG_REQUEST_HEADERS' is missing in error message: "
        << e.what();
  }
}

USERVER_NAMESPACE_END
