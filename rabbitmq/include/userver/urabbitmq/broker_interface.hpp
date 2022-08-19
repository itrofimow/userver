#pragma once

#include <string>

#include <userver/engine/deadline.hpp>
#include <userver/utils/flags.hpp>

#include <userver/urabbitmq/typedefs.hpp>

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {

class IAdminInterface {
 public:
  /// @brief Declare an exchange.
  ///
  /// @param exchange name of the exchange
  /// @param type exchange type
  /// @param flags exchange flags
  /// @param deadline execution deadline
  virtual void DeclareExchange(const Exchange& exchange, Exchange::Type type,
                               utils::Flags<Exchange::Flags> flags,
                               engine::Deadline deadline) = 0;

  /// @brief overload of DeclareExchange
  virtual void DeclareExchange(const Exchange& exchange, Exchange::Type type,
                               engine::Deadline deadline) = 0;

  /// @brief overload of DeclareExchange
  virtual void DeclareExchange(const Exchange& exchange,
                               engine::Deadline deadline) = 0;

  /// @brief Declare a queue.
  ///
  /// @param queue name of the queue
  /// @param flags queue flags
  /// @param deadline execution deadline
  virtual void DeclareQueue(const Queue& queue,
                            utils::Flags<Queue::Flags> flags,
                            engine::Deadline deadline) = 0;

  /// @brief overload of DeclareQueue
  virtual void DeclareQueue(const Queue& queue, engine::Deadline deadline) = 0;

  /// @brief Bind a queue to an exchange.
  ///
  /// @param exchange the source exchange
  /// @param queue the target queue
  /// @param routing_key the routing key
  /// @param deadline execution deadline
  virtual void BindQueue(const Exchange& exchange, const Queue& queue,
                         const std::string& routing_key,
                         engine::Deadline deadline) = 0;

  /// @brief Remove an exchange.
  ///
  /// @param exchange name of the exchange to remove
  /// @param deadline execution deadline
  virtual void RemoveExchange(const Exchange& exchange,
                              engine::Deadline deadline) = 0;

  /// @brief Remove a queue.
  ///
  /// @param queue name of the queue to remove
  /// @param deadline execution deadline
  virtual void RemoveQueue(const Queue& queue, engine::Deadline deadline) = 0;
};

class IChannelInterface {
 public:
  /// @brief Publish a message to an exchange
  ///
  /// You have to supply the name of the exchange and a routing key. RabbitMQ
  /// will then try to send the message to one or more queues.
  /// By default unroutable messages are silently discarded
  ///
  /// @param exchange the exchange to publish to
  /// @param routing_key the routing key
  /// @param message the message to send
  /// @param deadline execution deadline
  ///
  /// @note This method is `fire and forget` (no delivery guarantees),
  /// use `ReliableChannel::Publish` for delivery guarantees.
  virtual void Publish(const Exchange& exchange, const std::string& routing_key,
                       const std::string& message, MessageType type,
                       engine::Deadline deadline) = 0;
};

class IReliableChannelInterface {
 public:
  /// @brief Publish a message to an exchange and
  /// await confirmation from the broker
  ///
  /// You have to supply the name of the exchange and a routing key. RabbitMQ
  /// will then try to send the message to one or more queues.
  /// By default unroutable messages are silently discarded
  ///
  /// @param exchange the exchange to publish to
  /// @param routing_key the routing key
  /// @param message the message to send
  /// @param deadline execution deadline
  virtual void PublishReliable(const Exchange& exchange,
                               const std::string& routing_key,
                               const std::string& message, MessageType type,
                               engine::Deadline deadline) = 0;
};

}  // namespace urabbitmq

USERVER_NAMESPACE_END
