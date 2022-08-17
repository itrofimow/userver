#include <userver/urabbitmq/client.hpp>

#include <userver/formats/json/value.hpp>

#include <userver/urabbitmq/admin_channel.hpp>
#include <userver/urabbitmq/channel.hpp>

#include <urabbitmq/client_impl.hpp>
#include <urabbitmq/connection.hpp>
#include <urabbitmq/connection_ptr.hpp>
#include <urabbitmq/make_shared_enabler.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

std::shared_ptr<Client> Client::Create(clients::dns::Resolver& resolver,
                                       const ClientSettings& settings) {
  return std::make_shared<MakeSharedEnabler<Client>>(resolver, settings);
}

Client::Client(clients::dns::Resolver& resolver, const ClientSettings& settings)
    : impl_{resolver, settings} {}

Client::~Client() = default;

void Client::DeclareExchange(const Exchange& exchange, Exchange::Type type,
                             utils::Flags<Exchange::Flags> flags,
                             engine::Deadline deadline) {
  tracing::Span span{"declare_exchange"};
  auto awaiter = impl_->GetConnection()->GetChannel().DeclareExchange(
      exchange, type, flags, deadline);
  awaiter.Wait(deadline);
}

void Client::DeclareQueue(const Queue& queue, utils::Flags<Queue::Flags> flags,
                          engine::Deadline deadline) {
  tracing::Span span{"declare_queue"};
  auto awaiter =
      impl_->GetConnection()->GetChannel().DeclareQueue(queue, flags, deadline);
  awaiter.Wait(deadline);
}

void Client::BindQueue(const Exchange& exchange, const Queue& queue,
                       const std::string& routing_key,
                       engine::Deadline deadline) {
  tracing::Span span{"bind_queue"};
  auto awaiter = impl_->GetConnection()->GetChannel().BindQueue(
      exchange, queue, routing_key, deadline);
  awaiter.Wait(deadline);
}

void Client::RemoveExchange(const Exchange& exchange,
                            engine::Deadline deadline) {
  tracing::Span span{"remove_exchange"};
  auto awaiter =
      impl_->GetConnection()->GetChannel().RemoveExchange(exchange, deadline);
  awaiter.Wait(deadline);
}

void Client::RemoveQueue(const Queue& queue, engine::Deadline deadline) {
  tracing::Span span{"remove_queue"};
  auto awaiter =
      impl_->GetConnection()->GetChannel().RemoveQueue(queue, deadline);
  awaiter.Wait(deadline);
}

void Client::Publish(const Exchange& exchange, const std::string& routing_key,
                     const std::string& message, MessageType type,
                     engine::Deadline deadline) {
  tracing::Span span{"publish"};
  impl_->GetConnection()->GetChannel().Publish(exchange, routing_key, message,
                                               type, deadline);
}

void Client::PublishReliable(const Exchange& exchange,
                             const std::string& routing_key,
                             const std::string& message, MessageType type,
                             engine::Deadline deadline) {
  tracing::Span span{"reliable_publish"};
  auto awaiter = impl_->GetConnection()->GetReliableChannel().Publish(
      exchange, routing_key, message, type, deadline);
  awaiter.Wait(deadline);
}

AdminChannel Client::GetAdminChannel() { return {impl_->GetConnection()}; }

Channel Client::GetChannel() { return {impl_->GetConnection()}; }

ReliableChannel Client::GetReliableChannel() {
  return {impl_->GetConnection()};
}

formats::json::Value Client::GetStatistics() const {
  return impl_->GetStatistics();
}

}  // namespace urabbitmq

USERVER_NAMESPACE_END
