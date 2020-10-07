#include <server/handlers/http_handler_base.hpp>

#include <fmt/format.h>
#include <boost/algorithm/string/split.hpp>

#include <components/statistics_storage.hpp>
#include <engine/task/cancel.hpp>
#include <formats/json/serialize.hpp>
#include <formats/json/value_builder.hpp>
#include <http/common_headers.hpp>
#include <logging/log.hpp>
#include <server/component.hpp>
#include <server/handlers/http_handler_base_statistics.hpp>
#include <server/http/http_error.hpp>
#include <server/http/http_method.hpp>
#include <server/http/http_request_impl.hpp>
#include <tracing/span.hpp>
#include <tracing/tags.hpp>
#include <tracing/tracing.hpp>
#include <utils/graphite.hpp>
#include <utils/log.hpp>
#include <utils/overloaded.hpp>
#include <utils/statistics/metadata.hpp>
#include <utils/statistics/percentile_format_json.hpp>
#include <utils/text.hpp>

#include "auth/auth_checker.hpp"

namespace server::handlers {
namespace {

// "request" is redundant: https://st.yandex-team.ru/TAXICOMMON-1793
// set to 1 if you need server metrics
constexpr bool kIncludeServerHttpMetrics = false;

template <typename HeadersHolder>
std::string GetHeadersLogString(const HeadersHolder& headers_holder) {
  formats::json::ValueBuilder json_headers(formats::json::Type::kObject);
  for (const auto& header_name : headers_holder.GetHeaderNames()) {
    json_headers[header_name] = headers_holder.GetHeader(header_name);
  }
  return formats::json::ToString(json_headers.ExtractValue());
}

std::vector<http::HttpMethod> InitAllowedMethods(const HandlerConfig& config) {
  std::vector<http::HttpMethod> allowed_methods;
  auto& method_list = config.method;

  if (method_list) {
    std::vector<std::string> methods;
    boost::split(methods, *method_list, [](char c) { return c == ','; });
    for (const auto& method_str : methods) {
      auto method = http::HttpMethodFromString(method_str);
      if (!http::IsHandlerMethod(method)) {
        throw std::runtime_error(method_str +
                                 " is not supported in method list");
      }
      allowed_methods.push_back(method);
    }
  } else {
    for (auto method : http::kHandlerMethods) {
      allowed_methods.push_back(method);
    }
  }
  return allowed_methods;
}

void SetFormattedErrorResponse(http::HttpResponse& http_response,
                               FormattedErrorData&& formatted_error_data) {
  http_response.SetData(std::move(formatted_error_data.external_body));
  if (formatted_error_data.content_type) {
    http_response.SetContentType(*std::move(formatted_error_data.content_type));
  }
}

const std::string kTracingTypeResponse = "response";
const std::string kTracingTypeRequest = "request";
const std::string kTracingBody = "body";
const std::string kTracingUri = "uri";

const std::string kUserAgentTag = "useragent";
const std::string kAcceptLanguageTag = "acceptlang";
const std::string kMethodTag = "method";

class RequestProcessor final {
 public:
  RequestProcessor(const HttpHandlerBase& handler,
                   const http::HttpRequestImpl& http_request_impl,
                   const http::HttpRequest& http_request,
                   request::RequestContext& context, bool log_request,
                   bool log_request_headers)
      : handler_(handler),
        http_request_impl_(http_request_impl),
        http_request_(http_request),
        process_finished_(false),
        context_(context),
        log_request_(log_request),
        log_request_headers_(log_request_headers) {}

  ~RequestProcessor() {
    try {
      auto& span = tracing::Span::CurrentSpan();
      auto& response = http_request_.GetHttpResponse();
      response.SetHeader(::http::headers::kXYaRequestId, span.GetLink());
      int response_code = static_cast<int>(response.GetStatus());
      span.AddTag(tracing::kHttpStatusCode, response_code);
      if (response_code >= 500) span.AddTag(tracing::kErrorFlag, true);
      span.SetLogLevel(
          handler_.GetLogLevelForResponseStatus(response.GetStatus()));

      if (log_request_) {
        if (log_request_headers_) {
          span.AddNonInheritableTag("response_headers",
                                    GetHeadersLogString(response));
        }
        span.AddNonInheritableTag(
            kTracingBody, handler_.GetResponseDataForLoggingChecked(
                              http_request_, context_, response.GetData()));
      }
      span.AddNonInheritableTag(kTracingUri, http_request_.GetUrl());
    } catch (const std::exception& ex) {
      LOG_ERROR() << "can't finalize request processing: " << ex.what();
    }
  }

  template <typename Func>
  bool ProcessRequestStep(const std::string& step_name,
                          const Func& process_step_func) {
    if (process_finished_) return true;
    process_finished_ = DoProcessRequestStep(step_name, process_step_func);
    return process_finished_;
  }

 private:
  template <typename Func>
  bool DoProcessRequestStep(const std::string& step_name,
                            const Func& process_step_func) {
    const auto scope_time =
        tracing::Span::CurrentSpan().CreateScopeTime("http_" + step_name);
    auto& response = http_request_.GetHttpResponse();

    try {
      process_step_func();
    } catch (const CustomHandlerException& ex) {
      auto http_status = http::GetHttpStatus(ex.GetCode());
      auto level = handler_.GetLogLevelForResponseStatus(http_status);
      LOG(level) << "custom handler exception in '" << handler_.HandlerName()
                 << "' handler in " + step_name + ": msg=" << ex
                 << ", body=" << ex.GetExternalErrorBody();
      response.SetStatus(http_status);
      if (ex.IsExternalErrorBodyFormatted()) {
        response.SetData(ex.GetExternalErrorBody());
      } else {
        SetFormattedErrorResponse(response,
                                  handler_.GetFormattedExternalErrorBody(ex));
      }
      return true;
    } catch (const std::exception& ex) {
      if (engine::current_task::ShouldCancel()) {
        LOG_WARNING() << "request task cancelled, exception in '"
                      << handler_.HandlerName()
                      << "' handler in " + step_name + ": " << ex.what();
        response.SetStatus(http::HttpStatus::kClientClosedRequest);
      } else {
        LOG_ERROR() << "exception in '" << handler_.HandlerName()
                    << "' handler in " + step_name + ": " << ex;
        http_request_impl_.MarkAsInternalServerError();
        SetFormattedErrorResponse(response,
                                  handler_.GetFormattedExternalErrorBody({
                                      ExternalBody{response.GetData()},
                                      HandlerErrorCode::kServerSideError,
                                  }));
      }
      return true;
    } catch (...) {
      LOG_WARNING() << "unknown exception in '" << handler_.HandlerName()
                    << "' handler in " + step_name + " (task cancellation?)";
      response.SetStatus(http::HttpStatus::kClientClosedRequest);
      throw;
    }

    return false;
  }

  const HttpHandlerBase& handler_;
  const http::HttpRequestImpl& http_request_impl_;
  const http::HttpRequest& http_request_;
  bool process_finished_;

  request::RequestContext& context_;
  const bool log_request_;
  const bool log_request_headers_;
};

}  // namespace

formats::json::ValueBuilder HttpHandlerBase::StatisticsToJson(
    const HttpHandlerMethodStatistics& stats) {
  formats::json::ValueBuilder result;
  formats::json::ValueBuilder total;

  total["reply-codes"] = stats.FormatReplyCodes();
  total["in-flight"] = stats.GetInFlight();
  total["too-many-requests-in-flight"] = stats.GetTooManyRequestsInFlight();
  total["rate-limit-reached"] = stats.GetRateLimitReached();

  total["timings"]["1min"] =
      utils::statistics::PercentileToJson(stats.GetTimings());
  utils::statistics::SolomonSkip(total["timings"]["1min"]);

  utils::statistics::SolomonSkip(total);
  result["total"] = std::move(total);
  return result;
}

HttpHandlerBase::HttpHandlerBase(
    const components::ComponentConfig& config,
    const components::ComponentContext& component_context, bool is_monitor)
    : HandlerBase(config, component_context, is_monitor),
      http_server_settings_(
          component_context
              .FindComponent<components::HttpServerSettingsBase>()),
      allowed_methods_(InitAllowedMethods(GetConfig())),
      statistics_storage_(
          component_context.FindComponent<components::StatisticsStorage>()),
      handler_statistics_(std::make_unique<HttpHandlerStatistics>()),
      request_statistics_(std::make_unique<HttpHandlerStatistics>()),
      auth_checkers_(auth::CreateAuthCheckers(
          component_context, GetConfig(),
          http_server_settings_.GetAuthCheckerSettings())),
      log_level_(logging::OptionalLevelFromString(
          config.ParseOptionalString("log-level"))) {
  if (allowed_methods_.empty()) {
    LOG_WARNING() << "empty allowed methods list in " << config.Name();
  }

  if (!IsEnabled()) {
    return;
  }

  if (GetConfig().max_requests_per_second) {
    const auto max_rps = *GetConfig().max_requests_per_second;
    UASSERT_MSG(
        max_rps > 0,
        "max_requests_per_second option was not verified in config parsing");
    const auto token_update_interval =
        utils::TokenBucket::Duration{std::chrono::seconds(1)} / max_rps;
    if (token_update_interval > utils::TokenBucket::Duration::zero()) {
      rate_limit_.emplace(max_rps, token_update_interval);
    }
  }

  auto& server_component =
      component_context.FindComponent<components::Server>();

  engine::TaskProcessor& task_processor =
      component_context.GetTaskProcessor(GetConfig().task_processor);
  try {
    server_component.AddHandler(*this, task_processor);
  } catch (const std::exception& ex) {
    throw std::runtime_error(std::string("can't add handler to server: ") +
                             ex.what());
  }
  /// TODO: unable to add prefix metadata ATM

  const auto graphite_subpath = std::visit(
      utils::Overloaded{[](const std::string& path) {
                          return "by-path." + utils::graphite::EscapeName(path);
                        },
                        [](FallbackHandler fallback) {
                          return "by-fallback." + ToString(fallback);
                        }},
      GetConfig().path);
  const auto graphite_path =
      fmt::format("http.{}.by-handler.{}", graphite_subpath, config.Name());
  statistics_holder_ = statistics_storage_.GetStorage().RegisterExtender(
      graphite_path, std::bind(&HttpHandlerBase::ExtendStatistics, this,
                               std::placeholders::_1));
}

HttpHandlerBase::~HttpHandlerBase() { statistics_holder_.Unregister(); }

void HttpHandlerBase::HandleRequest(const request::RequestBase& request,
                                    request::RequestContext& context) const {
  try {
    const auto& http_request_impl =
        dynamic_cast<const http::HttpRequestImpl&>(request);
    const http::HttpRequest http_request(http_request_impl);
    auto& response = http_request.GetHttpResponse();

    HttpHandlerStatisticsScope stats_scope(*handler_statistics_,
                                           http_request.GetMethod(), response);

    bool log_request = http_server_settings_.NeedLogRequest();
    bool log_request_headers = http_server_settings_.NeedLogRequestHeaders();

    const auto& parent_link =
        http_request.GetHeader(::http::headers::kXYaRequestId);
    const auto& trace_id = http_request.GetHeader(::http::headers::kXYaTraceId);
    const auto& parent_span_id =
        http_request.GetHeader(::http::headers::kXYaSpanId);

    auto span = tracing::Span::MakeSpan("http/" + HandlerName(), trace_id,
                                        parent_span_id);

    span.SetLocalLogLevel(log_level_);

    if (!parent_link.empty()) span.AddTag("parent_link", parent_link);
    span.AddNonInheritableTag(tracing::kHttpMetaType,
                              GetMetaType(http_request));
    span.AddNonInheritableTag(tracing::kType, kTracingTypeResponse);
    span.AddNonInheritableTag(tracing::kHttpMethod,
                              http_request.GetMethodStr());

    static const std::string kParseRequestDataStep = "parse_request_data";
    static const std::string kCheckAuthStep = "check_auth";
    static const std::string kCheckRatelimitStep = "check_ratelimit";
    static const std::string kHandleRequestStep = "handle_request";

    RequestProcessor request_processor(*this, http_request_impl, http_request,
                                       context, log_request,
                                       log_request_headers);

    request_processor.ProcessRequestStep(
        kCheckRatelimitStep,
        [this, &http_request] { return CheckRatelimit(http_request); });

    request_processor.ProcessRequestStep(
        kCheckAuthStep,
        [this, &http_request, &context] { CheckAuth(http_request, context); });

    request_processor.ProcessRequestStep(
        kParseRequestDataStep, [this, &http_request, &context] {
          ParseRequestData(http_request, context);
        });

    if (log_request) {
      logging::LogExtra log_extra;

      if (log_request_headers) {
        log_extra.Extend("request_headers", GetHeadersLogString(http_request));
      }
      log_extra.Extend(tracing::kType, kTracingTypeRequest);
      const auto& body = http_request.RequestBody();
      uint64_t body_length = body.length();
      log_extra.Extend("request_body_length", body_length);
      log_extra.Extend(kTracingBody, GetRequestBodyForLoggingChecked(
                                         http_request, context, body));
      log_extra.Extend(kTracingUri, http_request.GetUrl());
      log_extra.Extend(kMethodTag, http_request.GetMethodStr());

      const auto& user_agent =
          http_request.GetHeader(::http::headers::kUserAgent);
      if (!user_agent.empty()) {
        log_extra.Extend(kUserAgentTag, user_agent);
      }
      const auto& accept_language =
          http_request.GetHeader(::http::headers::kAcceptLanguage);
      if (!accept_language.empty()) {
        log_extra.Extend(kAcceptLanguageTag, accept_language);
      }

      LOG_INFO()
          << "start handling"
          // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
          << std::move(log_extra);
    }

    request_processor.ProcessRequestStep(
        kHandleRequestStep, [this, &response, &http_request, &context] {
          response.SetData(HandleRequestThrow(http_request, context));
        });
  } catch (const std::exception& ex) {
    LOG_ERROR() << "unable to handle request: " << ex;
  }
}

void HttpHandlerBase::ThrowUnsupportedHttpMethod(
    const http::HttpRequest& request) const {
  throw ClientError(HandlerErrorCode::kInvalidUsage,
                    InternalMessage{"method " + request.GetMethodStr() +
                                    " is not allowed in " + HandlerName()});
}

void HttpHandlerBase::ReportMalformedRequest(
    const request::RequestBase& request) const {
  try {
    const auto& http_request_impl =
        dynamic_cast<const http::HttpRequestImpl&>(request);
    const http::HttpRequest http_request(http_request_impl);
    auto& response = http_request.GetHttpResponse();

    SetFormattedErrorResponse(
        response,
        GetFormattedExternalErrorBody({ExternalBody{response.GetData()},
                                       HandlerErrorCode::kRequestParseError}));
  } catch (const std::exception& ex) {
    LOG_ERROR() << "unable to handle ready request: " << ex;
  }
}

const std::vector<http::HttpMethod>& HttpHandlerBase::GetAllowedMethods()
    const {
  return allowed_methods_;
}

HttpHandlerStatistics& HttpHandlerBase::GetRequestStatistics() const {
  return *request_statistics_;
}

logging::Level HttpHandlerBase::GetLogLevelForResponseStatus(
    http::HttpStatus status) const {
  auto status_code = static_cast<int>(status);
  if (status_code >= 400 && status_code <= 499) return logging::Level::kWarning;
  if (status_code >= 500 && status_code <= 599) return logging::Level::kError;
  return logging::Level::kInfo;
}

FormattedErrorData HttpHandlerBase::GetFormattedExternalErrorBody(
    const CustomHandlerException& exc) const {
  return {exc.GetExternalErrorBody()};
}

void HttpHandlerBase::CheckAuth(const http::HttpRequest& http_request,
                                request::RequestContext& context) const {
  if (!http_server_settings_.NeedCheckAuthInHandlers()) {
    LOG_DEBUG() << "auth checks are disabled for current service";
    return;
  }

  if (!NeedCheckAuth()) {
    LOG_DEBUG() << "auth checks are disabled for current handler";
    return;
  }

  auth::CheckAuth(auth_checkers_, http_request, context);
}

void HttpHandlerBase::CheckRatelimit(
    const http::HttpRequest& http_request) const {
  auto& statistics =
      handler_statistics_->GetStatisticByMethod(http_request.GetMethod());
  auto& total_statistics = handler_statistics_->GetTotalStatistics();

  if (rate_limit_) {
    const bool success = rate_limit_->Obtain();
    if (!success) {
      LOG_ERROR() << "Max rate limit reached for handler '" << HandlerName()
                  << "', limit=" << GetConfig().max_requests_per_second;
      statistics.IncrementRateLimitReached();
      total_statistics.IncrementRateLimitReached();

      throw ExceptionWithCode<HandlerErrorCode::kTooManyRequests>();
    }
  }

  auto max_requests_in_flight = GetConfig().max_requests_in_flight;
  auto requests_in_flight = statistics.GetInFlight();
  if (max_requests_in_flight &&
      (requests_in_flight > *max_requests_in_flight)) {
    LOG_ERROR() << "Max requests in flight limit reached for handler '"
                << HandlerName() << "', current=" << requests_in_flight
                << " limit=" << *max_requests_in_flight;
    statistics.IncrementTooManyRequestsInFlight();
    total_statistics.IncrementTooManyRequestsInFlight();

    throw ExceptionWithCode<HandlerErrorCode::kTooManyRequests>();
  }
}

std::string HttpHandlerBase::GetRequestBodyForLogging(
    const http::HttpRequest&, request::RequestContext&,
    const std::string& request_body) const {
  size_t limit = GetConfig().request_body_size_log_limit;
  return utils::log::ToLimitedUtf8(request_body, limit);
}

std::string HttpHandlerBase::GetResponseDataForLogging(
    const http::HttpRequest&, request::RequestContext&,
    const std::string& response_data) const {
  size_t limit = GetConfig().response_data_size_log_limit;
  return utils::log::ToLimitedUtf8(response_data, limit);
}

std::string HttpHandlerBase::GetMetaType(
    const http::HttpRequest& request) const {
  return request.GetRequestPath();
}

std::string HttpHandlerBase::GetRequestBodyForLoggingChecked(
    const http::HttpRequest& request, request::RequestContext& context,
    const std::string& request_body) const {
  try {
    return GetRequestBodyForLogging(request, context, request_body);
  } catch (const std::exception& ex) {
    LOG_ERROR() << "failed to get request body for logging: " << ex;
    return "<error in GetRequestBodyForLogging>";
  }
}

std::string HttpHandlerBase::GetResponseDataForLoggingChecked(
    const http::HttpRequest& request, request::RequestContext& context,
    const std::string& response_data) const {
  try {
    return GetResponseDataForLogging(request, context, response_data);
  } catch (const std::exception& ex) {
    LOG_ERROR() << "failed to get response data for logging: " << ex;
    return "<error in GetResponseDataForLogging>";
  }
}

formats::json::ValueBuilder HttpHandlerBase::ExtendStatistics(
    const utils::statistics::StatisticsRequest& /*request*/) {
  formats::json::ValueBuilder result;
  result["handler"] = FormatStatistics(*handler_statistics_);

  if constexpr (kIncludeServerHttpMetrics) {
    result["request"] = FormatStatistics(*request_statistics_);
  }

  return result;
}

formats::json::ValueBuilder HttpHandlerBase::FormatStatistics(
    const HttpHandlerStatistics& stats) {
  formats::json::ValueBuilder result;
  result["all-methods"] = StatisticsToJson(stats.GetTotalStatistics());
  utils::statistics::SolomonSkip(result["all-methods"]);

  if (IsMethodStatisticIncluded()) {
    formats::json::ValueBuilder by_method;
    for (auto method : GetAllowedMethods()) {
      by_method[ToString(method)] =
          StatisticsToJson(stats.GetStatisticByMethod(method));
    }
    utils::statistics::SolomonChildrenAreLabelValues(by_method, "http_method");
    utils::statistics::SolomonSkip(by_method);
    result["by-method"] = std::move(by_method);
  }
  return result;
}

}  // namespace server::handlers
