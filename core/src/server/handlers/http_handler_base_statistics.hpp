#pragma once

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

#include <server/http/handler_methods.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/utils/statistics/aggregated_values.hpp>
#include <userver/utils/statistics/http_codes.hpp>
#include <userver/utils/statistics/percentile.hpp>
#include <userver/utils/statistics/recentperiod.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::handlers {

class HttpHandlerMethodStatistics final {
 public:
  void Account(unsigned int code, size_t ms) {
    reply_codes_.Account(code);
    timings_.GetCurrentCounter().Account(ms);
  }

  formats::json::Value FormatReplyCodes() const {
    return reply_codes_.FormatReplyCodes();
  }

  using Percentile = utils::statistics::Percentile<2048, unsigned int, 120>;

  Percentile GetTimings() const { return timings_.GetStatsForPeriod(); }

  size_t GetInFlight() const { return in_flight_; }

  void IncrementInFlight() { in_flight_++; }

  void DecrementInFlight() { in_flight_--; }

  void IncrementTooManyRequestsInFlight() { too_many_requests_in_flight_++; }

  size_t GetTooManyRequestsInFlight() const {
    return too_many_requests_in_flight_;
  }

  void IncrementRateLimitReached() { rate_limit_reached_++; }

  size_t GetRateLimitReached() const { return rate_limit_reached_; }

 private:
  utils::statistics::RecentPeriod<Percentile, Percentile,
                                  utils::datetime::SteadyClock>
      timings_;
  utils::statistics::HttpCodes reply_codes_{400, 401, 499, 500};
  std::atomic<size_t> in_flight_{0};
  std::atomic<size_t> too_many_requests_in_flight_{0};
  std::atomic<size_t> rate_limit_reached_{0};
};

class HttpHandlerStatistics final {
 public:
  HttpHandlerMethodStatistics& GetStatisticByMethod(http::HttpMethod method);

  const HttpHandlerMethodStatistics& GetStatisticByMethod(
      http::HttpMethod method) const;

  HttpHandlerMethodStatistics& GetTotalStatistics();

  const HttpHandlerMethodStatistics& GetTotalStatistics() const;

  void Account(http::HttpMethod method, unsigned int code,
               std::chrono::milliseconds ms);

  bool IsOkMethod(http::HttpMethod method) const;

 private:
  HttpHandlerMethodStatistics stats_;
  std::array<HttpHandlerMethodStatistics, http::kHandlerMethodsMax + 1>
      stats_by_method_;
};

class HttpHandlerStatisticsScope final {
 public:
  HttpHandlerStatisticsScope(HttpHandlerStatistics& stats,
                             http::HttpMethod method,
                             server::http::HttpResponse& response);

  ~HttpHandlerStatisticsScope();

 private:
  void Account(unsigned int code, std::chrono::milliseconds ms);

  HttpHandlerStatistics& stats_;
  const http::HttpMethod method_;
  const std::chrono::steady_clock::time_point start_time_;
  server::http::HttpResponse& response_;
};

}  // namespace server::handlers

USERVER_NAMESPACE_END
