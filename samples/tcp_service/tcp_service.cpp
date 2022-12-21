#include <userver/utest/using_namespace_userver.hpp>

/// [TCP sample - component]
#include <userver/components/minimal_component_list.hpp>
#include <userver/components/tcp_acceptor_base.hpp>
#include <userver/utils/daemon_run.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace samples::tcp {

class Hello final : public components::TcpAcceptorBase {
 public:
  static constexpr std::string_view kName = "tcp-echo";

  // Component is valid after construction and is able to accept requests
  Hello(const components::ComponentConfig& config,
        const components::ComponentContext& context)
      : TcpAcceptorBase(config, context) {}

  void ProcessSocket(engine::io::Socket&& sock) override;
};

}  // namespace samples::tcp

/// [TCP sample - component]

namespace samples::tcp {

constexpr std::string_view k200OkResponse =
    "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";

/// [TCP sample - ProcessSocket]
void Hello::ProcessSocket(engine::io::Socket&& sock) {
  std::string data;
  data.resize(1024);

  while (!engine::current_task::ShouldCancel()) {
    const auto read_bytes = sock.ReadSome(data.data(), data.size(), {});

    const auto sent_bytes =
        sock.SendAll(k200OkResponse.data(), k200OkResponse.size(), {});
    if (sent_bytes != k200OkResponse.size()) {
      return;
    }
  }
}
/// [TCP sample - ProcessSocket]

/// [TCP sample - GetStaticConfigSchema]

}  // namespace samples::tcp

/// [TCP sample - main]
int main(int argc, const char* const argv[]) {
  const auto component_list =
      components::MinimalComponentList().Append<samples::tcp::Hello>();

  return utils::DaemonMain(argc, argv, component_list);
}
/// [TCP sample - main]
