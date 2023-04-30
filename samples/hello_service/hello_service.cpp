#include <userver/utest/using_namespace_userver.hpp>

/// [Hello service sample - component]
#include <userver/clients/dns/component.hpp>
#include <userver/clients/http/client.hpp>
#include <userver/clients/http/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/utils/daemon_run.hpp>

namespace samples::hello {

class Hello final : public server::handlers::HttpHandlerBase {
 public:
  // `kName` is used as the component name in static config
  static constexpr std::string_view kName = "handler-hello-sample";

  // Component is valid after construction and is able to accept requests
  using HttpHandlerBase::HttpHandlerBase;

  std::string HandleRequestThrow(
      const server::http::HttpRequest&,
      server::request::RequestContext&) const override {
    return "Hello world!\n";
  }
};

class HelloEcho final : public server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-hello-echo";

  HelloEcho(const ::components::ComponentConfig& config,
            const ::components::ComponentContext& context)
      : server::handlers::HttpHandlerBase{config, context},
        http_client_{
            context.FindComponent<components::HttpClient>().GetHttpClient()} {}

  std::string HandleRequestThrow(
      const server::http::HttpRequest& request,
      server::request::RequestContext&) const override {
    auto http_response = http_client_.CreateNotSignedRequest()
                             ->get("http://localhost:8080/hello")
                             ->timeout(std::chrono::milliseconds{5000})
                             ->perform();

    auto& response = request.GetHttpResponse();
    for (const auto& [k, v] : http_response->headers()) {
      response.SetHeader(k, v);
    }

    return std::move(*http_response).body();
  }

 private:
  userver::clients::http::Client& http_client_;
};

}  // namespace samples::hello
/// [Hello service sample - component]

/// [Hello service sample - main]
int main(int argc, char* argv[]) {
  const auto component_list = components::MinimalServerComponentList()
                                  .Append<samples::hello::Hello>()
                                  .Append<samples::hello::HelloEcho>()
                                  .Append<clients::dns::Component>()
                                  .Append<components::HttpClient>();
  return utils::DaemonMain(argc, argv, component_list);
}
/// [Hello service sample - main]
