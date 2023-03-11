#include <userver/utest/using_namespace_userver.hpp>

/// [Hello service sample - component]
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/utils/daemon_run.hpp>

#include <userver/clients/dns/component.hpp>
#include <userver/storages/postgres/postgres.hpp>
#include <userver/testsuite/testsuite_support.hpp>

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

class HelloFromPg final : public server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "handler-hello-from-pg";

  HelloFromPg(const components::ComponentConfig& config,
              const components::ComponentContext& context)
      : server::handlers::HttpHandlerBase{config, context},
        pg_{context.FindComponent<components::Postgres>("hello-db")
                .GetCluster()} {}

  std::string HandleRequestThrow(
      const server::http::HttpRequest&,
      server::request::RequestContext&) const override {
    const storages::postgres::CommandControl cc{
        std::chrono::milliseconds{5000}, std::chrono::milliseconds{5000}};

    const auto pg_res =
        pg_->Execute(storages::postgres::ClusterHostType::kMaster, cc,
                     "SELECT * from userver_pg_bench");
    return std::to_string(pg_res.RowsAffected());
  }

 private:
  storages::postgres::ClusterPtr pg_;
};

}  // namespace samples::hello
/// [Hello service sample - component]

/// [Hello service sample - main]
int main(int argc, char* argv[]) {
  const auto component_list = components::MinimalServerComponentList()
                                  .Append<samples::hello::Hello>()
                                  // pg stuff
                                  .Append<components::TestsuiteSupport>()
                                  .Append<clients::dns::Component>()
                                  .Append<components::Postgres>("hello-db")
                                  .Append<samples::hello::HelloFromPg>();
  return utils::DaemonMain(argc, argv, component_list);
}
/// [Hello service sample - main]
