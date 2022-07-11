#include "connection.hpp"

#include <userver/utils/assert.hpp>

#include <urabbitmq/channel_ptr.hpp>
#include <urabbitmq/impl/amqp_channel.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

Connection::Connection(clients::dns::Resolver& resolver,
                       engine::ev::ThreadControl& thread,
                       const ConnectionSettings& settings)
    : handler_{resolver,
               thread,
               // TODO : fix me
               {"amqp://guest:guest@localhost/"}},
      conn_{handler_},
      settings_{settings},
      queue_{settings.max_channels} {
  for (size_t i = 0; i < settings_.max_channels; ++i) {
    AddChannel();
  }
}

Connection::~Connection() {
  while (true) {
    auto* ptr = TryPop();
    if (!ptr) break;

    Drop(ptr);
  }
}

ChannelPtr Connection::Acquire() { return {shared_from_this(), Pop()}; }

void Connection::Release(impl::IAmqpChannel* channel) noexcept {
  UASSERT(channel);

  channel->ResetCallbacks();

  if (!queue_.bounded_push(channel)) {
    Drop(channel);
  }
}

bool Connection::IsBroken() const { return handler_.IsBroken(); }

impl::IAmqpChannel* Connection::Pop() {
  auto* ptr = TryPop();

  if (!ptr) {
    // TODO : fix me
    throw std::runtime_error{"oh well"};
  }
  return ptr;
}

impl::IAmqpChannel* Connection::TryPop() {
  impl::IAmqpChannel* ptr{nullptr};
  if (queue_.pop(ptr)) {
    return ptr;
  }

  return nullptr;
}

std::unique_ptr<impl::IAmqpChannel> Connection::CreateChannel() {
  switch (settings_.mode) {
    case ConnectionMode::kUnreliable:
      return std::make_unique<impl::AmqpChannel>(conn_);
    case ConnectionMode::kReliable:
      return std::make_unique<impl::AmqpReliableChannel>(conn_);
  }
}

void Connection::Drop(impl::IAmqpChannel* channel) {
  std::default_delete<impl::IAmqpChannel>{}(channel);
}

void Connection::AddChannel() {
  auto* ptr = CreateChannel().release();
  if (!queue_.bounded_push(ptr)) {
    Drop(ptr);
  }
}

}  // namespace urabbitmq

USERVER_NAMESPACE_END