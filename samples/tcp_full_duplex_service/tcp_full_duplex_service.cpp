#include <userver/utest/using_namespace_userver.hpp>

/// [TCP sample - component]
#include <userver/components/minimal_component_list.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/components/tcp_acceptor_base.hpp>
#include <userver/concurrent/queue.hpp>
#include <userver/utils/daemon_run.hpp>
#include <userver/engine/async.hpp>

namespace samples::tcp::echo {

struct Stats;

class Echo final : public components::TcpAcceptorBase {
 public:
  static constexpr std::string_view kName = "tcp-echo";

  // Component is valid after construction and is able to accept requests
  Echo(const components::ComponentConfig& config,
       const components::ComponentContext& context);

  void ProcessSocket(engine::io::Socket&& sock) override;
};

}  // namespace samples::tcp::echo

template <>
inline constexpr bool components::kHasValidate<samples::tcp::echo::Echo> = true;

/// [TCP sample - component]

namespace samples::tcp::echo {
/// [TCP sample - Stats tag]

/// [TCP sample - constructor]
Echo::Echo(const components::ComponentConfig& config,
           const components::ComponentContext& context)
    : TcpAcceptorBase(config, context) {}
/// [TCP sample - constructor]

/// [TCP sample - SendRecv]
namespace {

constexpr std::string_view k200OkResponse =
    "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";

using Queue = concurrent::SpscQueue<engine::TaskWithResult<void>>;

void DoSend(engine::io::Socket& sock, Queue::Consumer consumer) {
  engine::TaskWithResult<void> response;
  while (consumer.Pop(response)) {
    response.Wait();
    const auto sent_bytes =
        sock.SendAll(k200OkResponse.data(), k200OkResponse.size(), {});
    if (sent_bytes != k200OkResponse.size()) {
      return;
    }
  }
}

void DoRecv(engine::io::Socket& sock, Queue::Producer producer) {
  std::array<char, 1024> buf;  // NOLINT(cppcoreguidelines-pro-type-member-init)
  while (!engine::current_task::ShouldCancel()) {
    const auto read_bytes = sock.ReadSome(buf.data(), buf.size(), {});
    if (!read_bytes) {
      return;
    }

    if (!producer.Push(engine::AsyncNoSpan([]{}))) {
      return;
    }
  }
}

}  // anonymous namespace
/// [TCP sample - SendRecv]

/// [TCP sample - ProcessSocket]
void Echo::ProcessSocket(engine::io::Socket&& sock) {
  auto queue = Queue::Create();

  auto send_task =
      utils::Async("send", DoSend, std::ref(sock), queue->GetConsumer());

  DoRecv(sock, queue->GetProducer());
  send_task.SyncCancel();
}
/// [TCP sample - ProcessSocket]

}  // namespace samples::tcp::echo

/// [TCP sample - main]
int main(int argc, const char* const argv[]) {
  const auto component_list =
      components::MinimalComponentList().Append<samples::tcp::echo::Echo>();

  return utils::DaemonMain(argc, argv, component_list);
}
/// [TCP sample - main]
