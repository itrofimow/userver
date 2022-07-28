#pragma once

#include <memory>

#include <boost/lockfree/queue.hpp>

#include <userver/utils/periodic_task.hpp>

#include <urabbitmq/channel_ptr.hpp>
#include <urabbitmq/impl/amqp_channel.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

namespace impl {

class AmqpConnectionHandler;
class AmqpConnection;

}  // namespace impl

class ChannelPool : public std::enable_shared_from_this<ChannelPool> {
 public:
  enum class ChannelMode { kDefault, kReliable };

  static std::shared_ptr<ChannelPool> Create(
      impl::AmqpConnectionHandler& handler, impl::AmqpConnection& connection,
      ChannelMode mode, size_t max_channels);
  ~ChannelPool();

  ChannelPtr Acquire();
  void Release(std::unique_ptr<impl::IAmqpChannel>&& channel) noexcept;

  void NotifyChannelAdopted() noexcept;
  void Stop() noexcept;

  bool IsWriteable() const noexcept;

 protected:
  ChannelPool(impl::AmqpConnectionHandler& handler,
              impl::AmqpConnection& connection, ChannelMode mode,
              size_t max_channels);

 private:
  impl::IAmqpChannel* Pop();
  impl::IAmqpChannel* TryPop();

  std::unique_ptr<impl::IAmqpChannel> CreateChannel();
  void Drop(impl::IAmqpChannel* channel);

  void AddChannel();

  engine::ev::ThreadControl thread_;
  impl::AmqpConnectionHandler* handler_;
  impl::AmqpConnection* connection_;
  ChannelMode channel_mode_;
  size_t max_channels_;

  // const ConnectionSettings settings_;
  boost::lockfree::queue<impl::IAmqpChannel*> queue_;

  std::atomic<size_t> size_{0};
  std::atomic<size_t> given_away_{0};

  std::atomic<bool> broken_{false};
  utils::PeriodicTask monitor_{};
};

}  // namespace urabbitmq

USERVER_NAMESPACE_END
