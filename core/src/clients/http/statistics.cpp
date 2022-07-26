#include <clients/http/statistics.hpp>

#include <curl-ev/error_code.hpp>

#include <userver/logging/log.hpp>
#include <userver/utils/statistics/common.hpp>
#include <userver/utils/statistics/metadata.hpp>
#include <userver/utils/statistics/percentile_format_json.hpp>

USERVER_NAMESPACE_BEGIN

namespace clients::http {

RequestStats::RequestStats(Statistics& stats) : stats_(stats) {
  stats_.easy_handles++;
}

RequestStats::~RequestStats() { stats_.easy_handles--; }

void RequestStats::Start() { start_time_ = std::chrono::steady_clock::now(); }

void RequestStats::FinishOk(int code, int attempts) {
  stats_.AccountError(Statistics::ErrorGroup::kOk);
  stats_.AccountStatus(code);
  if (attempts > 1) stats_.retries += attempts - 1;
  StoreTiming();
}

void RequestStats::FinishEc(std::error_code ec, int attempts) {
  stats_.AccountError(Statistics::ErrorCodeToGroup(ec));
  if (attempts > 1) stats_.retries += attempts - 1;
  StoreTiming();
}

void RequestStats::StoreTiming() {
  auto now = std::chrono::steady_clock::now();
  auto diff = now - start_time_;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
  stats_.timings_percentile.GetCurrentCounter().Account(ms);
}

void RequestStats::StoreTimeToStart(std::chrono::microseconds micro_seconds) {
  /* There is a race between multiple easy handles, we don't care which of them
   * writes its time_to_start. If boost::asio pool is full, we'll see big
   * numbers anyway. */
  stats_.last_time_to_start_us = micro_seconds.count();
}

void RequestStats::AccountOpenSockets(size_t sockets) {
  stats_.socket_open += sockets;
}

Statistics::ErrorGroup Statistics::ErrorCodeToGroup(std::error_code ec) {
  using ErrorCode = curl::errc::EasyErrorCode;

  if (ec.category() != curl::errc::GetEasyCategory())
    return ErrorGroup::kUnknown;

  switch (static_cast<ErrorCode>(ec.value())) {
    case ErrorCode::kCouldNotResolveHost:
      return ErrorGroup::kHostResolutionFailed;

    case ErrorCode::kOperationTimedout:
      return ErrorGroup::kTimeout;

    case ErrorCode::kSslConnectError:
    case ErrorCode::kPeerFailedVerification:
    case ErrorCode::kSslCipher:
    case ErrorCode::kSslCertproblem:
    case ErrorCode::kSslCacertBadfile:
    case ErrorCode::kSslIssuerError:
    case ErrorCode::kSslCrlBadfile:
      return ErrorGroup::kSslError;

    case ErrorCode::kTooManyRedirects:
      return ErrorGroup::kTooManyRedirects;

    case ErrorCode::kSendError:
    case ErrorCode::kRecvError:
    case ErrorCode::kCouldNotConnect:
      return ErrorGroup::kSocketError;

    default:
      return ErrorGroup::kUnknown;
  }
}

Statistics::Statistics() {
  /* No way to init std::array<std::atomic<T>, N> w/o explicit default ctr :( */
  for (auto& status : reply_status) status = 0;
}

const char* Statistics::ToString(ErrorGroup error) {
  switch (error) {
    case ErrorGroup::kOk:
      return "ok";
    case ErrorGroup::kHostResolutionFailed:
      return "host-resolution-failed";
    case ErrorGroup::kSocketError:
      return "socket-error";
    case ErrorGroup::kTimeout:
      return "timeout";
    case ErrorGroup::kSslError:
      return "ssl-error";
    case ErrorGroup::kTooManyRedirects:
      return "too-many-redirects";
    case ErrorGroup::kUnknown:
    case ErrorGroup::kCount:
      break;
  }
  return "unknown-error";
}

void Statistics::AccountError(ErrorGroup error) {
  error_count[static_cast<int>(error)]++;
}

void Statistics::AccountStatus(int code) {
  try {
    reply_status.at(code - kMinHttpStatus)++;
  } catch (const std::out_of_range&) {
    LOG_WARNING() << "Non-standard HTTP status code: " << code
                  << ", skipping statistics accounting";
  }
}

formats::json::ValueBuilder StatisticsToJson(const InstanceStatistics& stats,
                                             FormatMode format_mode) {
  formats::json::ValueBuilder json;
  json["timings"]["1min"] =
      utils::statistics::PercentileToJson(stats.timings_percentile)
          .ExtractValue();
  utils::statistics::SolomonSkip(json["timings"]["1min"]);

  formats::json::ValueBuilder errors;
  for (int i = 0; i < Statistics::kErrorGroupCount; i++) {
    const auto error_group = static_cast<Statistics::ErrorGroup>(i);
    errors[Statistics::ToString(error_group)] = stats.error_count[i];
  }
  utils::statistics::SolomonChildrenAreLabelValues(errors, "http_error");
  json["errors"] = errors;

  formats::json::ValueBuilder statuses(formats::json::Type::kObject);
  for (const auto& [code, count] : stats.reply_status) {
    statuses[std::to_string(code)] = count;
  }
  utils::statistics::SolomonChildrenAreLabelValues(statuses, "http_code");
  json["reply-statuses"] = std::move(statuses);

  json["retries"] = stats.retries;
  json["pending-requests"] = stats.easy_handles;

  if (format_mode == FormatMode::kModeAll) {
    json["last-time-to-start-us"] = stats.last_time_to_start_us;
    json["event-loop-load"][utils::statistics::DurationToString(
        utils::statistics::kDefaultMaxPeriod)] = stats.multi.current_load;

    // Destinations may reuse sockets from other destination,
    // it is very unjust to account active/closed sockets
    json["sockets"]["close"] = stats.multi.socket_close;
    json["sockets"]["throttled"] = stats.multi.socket_ratelimit;
    json["sockets"]["active"] =
        stats.multi.socket_open - stats.multi.socket_close;
  }
  json["sockets"]["open"] = stats.multi.socket_open;

  return json;
}

formats::json::ValueBuilder PoolStatisticsToJson(const PoolStatistics& stats) {
  formats::json::ValueBuilder json;
  InstanceStatistics sum_stats;

  sum_stats.Add(stats.multi);
  for (size_t i = 0; i < stats.multi.size(); i++) {
    auto key = "worker-" + std::to_string(i);
    json[key] = StatisticsToJson(stats.multi[i]);
    utils::statistics::SolomonLabelValue(json[key], "http_worker_id");
  }

  json["pool-total"] = StatisticsToJson(sum_stats);
  utils::statistics::SolomonSkip(json["pool-total"]);
  return json;
}

InstanceStatistics::InstanceStatistics(const Statistics& other)
    : easy_handles(other.easy_handles.load()),
      last_time_to_start_us(other.last_time_to_start_us.load()),
      timings_percentile(other.timings_percentile.GetStatsForPeriod()),
      retries(other.retries.load()) {
  for (size_t i = 0; i < error_count.size(); i++)
    error_count[i] = other.error_count[i].load();

  for (size_t i = 0; i < other.reply_status.size(); i++) {
    const auto& value = other.reply_status[i].load();
    auto status = i + Statistics::kMinHttpStatus;
    if (value || IsForcedStatusCode(status)) reply_status[status] = value;
  }

  multi.socket_open = other.socket_open;
}

bool InstanceStatistics::IsForcedStatusCode(int status) {
  return status == 200 || status == 400 || status == 401 || status == 500;
}

uint64_t InstanceStatistics::GetNotOkErrorCount() const {
  uint64_t result{0};

  for (size_t i = 0; i < Statistics::kErrorGroupCount; i++) {
    auto error_group = static_cast<Statistics::ErrorGroup>(i);
    if (error_group == ErrorGroup::kOk) continue;

    result += error_count[i];
  }
  return result;
}

void InstanceStatistics::Add(const std::vector<InstanceStatistics>& stats) {
  for (const auto& stat : stats) {
    easy_handles += stat.easy_handles;
    last_time_to_start_us +=
        stat.last_time_to_start_us;  // will be divided later

    timings_percentile.Add(stat.timings_percentile);

    for (size_t i = 0; i < Statistics::kErrorGroupCount; i++) {
      error_count[i] += stat.error_count[i];
    }
    retries += stat.retries;

    multi += stat.multi;
  }

  if (!stats.empty()) {
    last_time_to_start_us /= stats.size();
    multi.current_load /= stats.size();
  }
}

}  // namespace clients::http

USERVER_NAMESPACE_END
