#ifndef NDEBUG
#include <iostream>
#endif
#include "AmqpJsonConverter.hpp"
#include "AmqpTransceiver.hpp"

#define UNUSED(x) (void)x;

using namespace amqp;

const int Transceiver::ExchangeCreationFlags = AMQP::autodelete + AMQP::durable;
std::unordered_map<Transceiver::State, Transceiver::State> Transceiver::StopTransit = {
  { eCreateChannel, eEnd },
  { eCreateExchange, eCloseChannel },
  { eCheckQueue, eCloseChannel },
  { eCreateQueue, eCloseChannel },
  { eBindQueue, eRemoveQueue },
  { eCreateConsumer, eUnbindQueue },
  { eReady, eShutdown }
};

Transceiver::Transceiver(const std::string& exchange, const std::string& queue_,
                         const std::string& route_in, bool listener):
  m_state(eEnd),
  m_connection(nullptr),
  m_exchange(exchange),
  m_queue(queue_),
  m_route_in(route_in),
  m_listener(listener),
  m_queueExist(false),
  m_qFlags(0),
  m_onBounceMessage(nullptr),
  m_onMessage(nullptr),
  m_onExit(nullptr),
  m_ec(eNoError)
{
  // если имя очереди не было задано, брокер удалит ее после закрытия канала
  if (m_queue.empty()) m_qFlags += AMQP::exclusive;
}

Transceiver::~Transceiver()
{
}

void Transceiver::onBounce(BounceCallback callback)
{
  m_onBounceMessage = callback;
}

void Transceiver::onMessage(MessageCallback callback)
{
  m_onMessage = callback;
}

void Transceiver::onExit(ExitCallback callback)
{
  m_onExit = callback;
}

bool Transceiver::send(const rapidjson::Document& message,
                       const std::string& route,
                       bool mandatory)
{
  if (m_state != eReady) return false;
  int flags = 0;
  if (mandatory) flags += AMQP::mandatory;
  // AMQP::Envelope don't owned message body, so we provide the buffer.
  std::string buffer;
  std::shared_ptr< AMQP::Envelope > envelope(ConvertFromJson(message, buffer));
#ifndef NDEBUG
std::clog << "Transceiver send " << buffer << std::endl;
#endif
  m_channel->publish(m_exchange, route, *envelope, flags)
    .onReturned([this](const AMQP::Message& message, int16_t code,
                       const std::string& description) {
#ifndef NDEBUG
std::clog << "DefferedPublisher:: onReturned()" << std::endl;
#endif
      if (this->m_onBounceMessage)
      {
#ifndef NDEBUG
std::clog << "DefferedPublisher:: onReturned() do callback" << std::endl;
#endif
        this->m_onBounceMessage(message, code, description);
      }
    });
  return true;
}

bool Transceiver::send(const std::string& message, const std::string& route,
                       bool mandatory)
{
  if (m_state != eReady) return false;
  int flags = 0;
  if (mandatory) flags += AMQP::mandatory;
  AMQP::Envelope envelope(message.data(), message.size());
  envelope.setContentType("text/plain");
  envelope.setContentEncoding("utf-8");
#ifndef NDEBUG
std::clog << "Transceiver send " << message << std::endl;
#endif
  m_channel->publish(m_exchange, route, envelope, flags)
    .onReturned([this](const AMQP::Message& message, int16_t code,
                       const std::string& description) {
#ifndef NDEBUG
std::clog << "DefferedPublisher:: onReturned()" << std::endl;
#endif
      if (this->m_onBounceMessage)
      {
#ifndef NDEBUG
std::clog << "DefferedPublisher:: onReturned() do callback" << std::endl;
#endif
        this->m_onBounceMessage(message, code, description);
      }
    });
  return true;
}

void Transceiver::OnMessage(AMQP::Channel* channel, const AMQP::Message& message,
                            uint64_t deliveryTag, bool redelivered)
{
  UNUSED(channel)
  UNUSED(message)
  UNUSED(deliveryTag)
  UNUSED(redelivered)
}

void Transceiver::start(AMQP::Connection* connection)
{
  if (m_state == eEnd)
  {
    m_connection = connection;
    m_error.clear();
    m_ec = eNoError;
    m_state = eCreateChannel;
#ifndef NDEBUG
std::clog << "Transceiver eEnd -> " << m_state << std::endl;
#endif
    StateMachine();
  }
}

void Transceiver::stop()
{
  if (m_state == eEnd) return;
  if ((m_state >= eCreateChannel) && (m_state <= eReady))
  {
#ifndef NDEBUG
std::clog << "Transceiver " << m_state << " -> ";
#endif
    m_state = StopTransit[m_state];
#ifndef NDEBUG
std::clog << m_state << std::endl;
#endif
    StateMachine();
  }
  // else nothing to do, transceiver already stops
}

void Transceiver::drop()
{
  if (m_state == eEnd) return;
  m_error = "transceiver dropped";
  m_ec = eDrop;
  m_state = eEnd;
  StateMachine();
}

void Transceiver::StateMachine()
{
  switch (m_state)
  {
    case eCreateChannel:
      if (m_connection)
        {
          std::shared_ptr<AMQP::Channel> channel =
            std::make_shared<AMQP::Channel>(m_connection);
          m_channel.swap(channel);
          m_channel->onReady([this]() {
            if (m_state != eCreateChannel) return;
            // drop unnecessary callback
            m_channel->onError(nullptr);
            m_state = m_listener ? eCheckQueue : eCreateExchange;
#ifndef NDEBUG
std::clog << "Transceiver eCreateChannel -> " << m_state << std::endl;
#endif
            StateMachine();
          });
          m_channel->onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eCreateChannel error: " << message << std::endl;
#endif
            if (m_state != eCreateChannel) return;
            m_error = message;
            m_ec = eCreateChannelError;
            m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eCreateChannel -> " << m_state << std::endl;
#endif
            StateMachine();
          });
        }
        else
        {
          m_error = "no AMQP connection";
#ifndef NDEBUG
std::clog << "Transceiver eCreateChannel error: " << m_error << std::endl;
#endif
          m_ec = eCreateChannelError;
          m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eCreateChannel -> " << m_state << std::endl;
#endif
          StateMachine();
        }
      break;
    case eCheckQueue:
      if (m_queue.empty())
        {
          m_state = eCreateExchange;
#ifndef NDEBUG
std::clog << "Transceiver eCheckQueue -> " << m_state << std::endl;
#endif
          StateMachine();
        }
        else
        {
          m_channel->declareQueue(m_queue, AMQP::passive)
            .onSuccess([this](const std::string& name, int msgcount,
                              int consumercount) {
              UNUSED(msgcount)
              UNUSED(consumercount)
              if (m_state != eCheckQueue) return;
              m_queueExist = true;
              m_recvQueue = name;
              m_state = eCreateExchange;
#ifndef NDEBUG
std::clog << "Transceiver eCheckQueue -> " << m_state << std::endl;
#endif
              StateMachine();
            })
            .onError([this](const char* message) {
              UNUSED(message)
              if (m_state != eCheckQueue) return;
              m_state = eRecreateChannel;
#ifndef NDEBUG
std::clog << "Transceiver eCheckQueue -> " << m_state << std::endl;
#endif
              StateMachine();
            });
        }
      break;
    case eRecreateChannel:
      {
        m_channel->close();
        std::shared_ptr<AMQP::Channel> channel =
          std::make_shared<AMQP::Channel>(m_connection);
        m_channel.swap(channel);
        m_channel->onReady([this]() {
          if (m_state != eRecreateChannel) return;
          // drop unnecessary callback
          m_channel->onError(nullptr);
          m_state = eCreateExchange;
#ifndef NDEBUG
std::clog << "Transceiver eRecreateChannel -> " << m_state << std::endl;
#endif
          StateMachine();
        });
        m_channel->onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eRecreateChannel error: " << message << std::endl;
#endif
          if (m_state != eRecreateChannel) return;
          m_error = message;
          m_ec = eCreateChannelError;
          m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eRecreateChannel -> " << m_state << std::endl;
#endif
          StateMachine();
        });
      }
      break;
    case eCreateExchange:
      m_channel->declareExchange(m_exchange, AMQP::topic, ExchangeCreationFlags)
        .onSuccess([this]() {
          if (m_state != eCreateExchange) return;
          m_state = m_listener ? eCreateQueue : eReady;
#ifndef NDEBUG
std::clog << "Transceiver eCreateExchange -> " << m_state << std::endl;
#endif
          StateMachine();
        })
        .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eCreateExchange error: " << message << std::endl;
#endif
          if (m_state != eCreateExchange) return;
          m_ec = eCreateExchangeError;
          m_error = message;
          m_state = eCloseChannel;
#ifndef NDEBUG
std::clog << "Transceiver eCreateExchange -> " << m_state << std::endl;
#endif
          StateMachine();
        });
      break;
    case eCreateQueue:
      if (m_queueExist)
        {
          m_state = eBindQueue;
#ifndef NDEBUG
std::clog << "Transceiver eCreateQueue -> " << m_state << std::endl;
#endif
          StateMachine();
        }
        else
        {
          m_channel->declareQueue(m_queue, m_qFlags)
            .onSuccess([this](const std::string& name, int msgcount,
                              int consumercount) {
              UNUSED(msgcount)
              UNUSED(consumercount)
              if (m_state != eCreateQueue) return;
              m_recvQueue = name;
              m_state = eBindQueue;
#ifndef NDEBUG
std::clog << "Transceiver eCreateQueue(" << m_recvQueue << ") -> " << m_state << std::endl;
#endif
              StateMachine();
            })
            .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eCreateQueue error: " << message << std::endl;
#endif
              if (m_state != eCreateQueue) return;
              m_ec = eCreateQueueError;
              m_error = message;
              m_state = eCloseChannel;
#ifndef NDEBUG
std::clog << "Transceiver eCreateQueue(" << m_recvQueue << ") -> " << m_state << std::endl;
#endif
              StateMachine();
            });
        }
      break;
    case eBindQueue:
      m_channel->bindQueue(m_exchange, m_recvQueue, m_route_in)
        .onSuccess([this]() {
          if (m_state != eBindQueue) return;
          m_state = eCreateConsumer;
#ifndef NDEBUG
std::clog << "Transceiver eBindQueue -> " << m_state << std::endl;
#endif
          StateMachine();
        })
        .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eBindQueue error: " << message << std::endl;
#endif
          if (m_state != eBindQueue) return;
          m_ec = eBindQueueError;
          m_error = message;
          m_state = eRemoveQueue;
#ifndef NDEBUG
std::clog << "Transceiver eBindQueue -> " << m_state << std::endl;
#endif
          StateMachine();
        });
      break;
    case eCreateConsumer:
      m_channel->consume(m_recvQueue)
        .onSuccess([this](const std::string& consumer) {
          if (m_state != eCreateConsumer) return;
          m_consumerTag = consumer;
          m_state = eReady;
#ifndef NDEBUG
std::clog << "Transceiver eCreateConsumer(" << m_consumerTag << ") -> " << m_state << std::endl;
#endif
          StateMachine();
        })
        .onReceived([this](const AMQP::Message &message, uint64_t deliveryTag,
                           bool redelivered) {
          // PROCESS INCOMING MESSAGES
          if (m_state != eReady) return; // ???
          if (m_onMessage)
            m_onMessage(m_channel.get(), message, deliveryTag, redelivered);
            else OnMessage(m_channel.get(), message, deliveryTag, redelivered);
        })
        .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eCreateConsumer error: " << message << std::endl;
#endif
          switch (m_state)
          {
            case eCreateConsumer:
              m_ec = eCreateConsumerError;
              m_error = message;
              m_state = eUnbindQueue;
              break;
            case eReady:
              m_ec = eChannelAbruptlyClosedError;
              m_error = message;
              m_state = eEnd;
              break;
            default:
              return;
          }
#ifndef NDEBUG
std::clog << "Transceiver eCreateConsumer(" << m_consumerTag << ") -> " << m_state << std::endl;
#endif
          StateMachine();
        });
      break;
    case eReady:
      if (!m_listener)
      {
        m_channel->onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eReady error: " << message << std::endl;
#endif
          m_ec = eChannelAbruptlyClosedError;
          m_error = message;
          m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eReady -> " << m_state << std::endl;
#endif
          StateMachine();
        });
      }
      break;
    case eShutdown:
      if (m_listener)
        {
          if (m_consumerTag.empty())
            {
              m_state = eUnbindQueue;
#ifndef NDEBUG
std::clog << "Transceiver eShutdown -> " << m_state << std::endl;
#endif
              StateMachine();
            }
            else
            {
              m_channel->cancel(m_consumerTag)
                .onSuccess([this](const std::string& consumer) {
                  if (m_consumerTag != consumer) return; // ???
                  m_state = eUnbindQueue;
#ifndef NDEBUG
std::clog << "Transceiver eShutdown(consumer cancel) -> " << m_state << std::endl;
#endif
                  StateMachine();
                })
                .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eShutdown(consumer cancel) error: " << message << std::endl;
#endif
                  m_ec = eConsumerCancelError;
                  m_error = message;
                  m_state = eUnbindQueue;
#ifndef NDEBUG
std::clog << "Transceiver eShutdown(consumer cancel) -> " << m_state << std::endl;
#endif
                  StateMachine();
                });
            }
        }
        else
        {
          m_state = eCloseChannel;
#ifndef NDEBUG
std::clog << "Transceiver eShutdown -> " << m_state << std::endl;
#endif
          StateMachine();
        }
      break;
    case eUnbindQueue:
      if (m_recvQueue.empty())
        {
          // TODO: impossible???
          m_state = eCloseChannel;
#ifndef NDEBUG
std::clog << "Transceiver eUnbindQueue -> " << m_state << std::endl;
#endif
          StateMachine();
        }
        else
        {
          // replace broken channel and unbind queue
          m_channel->close();
          std::shared_ptr<AMQP::Channel> channel =
            std::make_shared<AMQP::Channel>(m_connection);
          m_channel.swap(channel);
          m_channel->onReady([this]() {
            if (m_state != eUnbindQueue) return;
            // drop unnecessary callback
            m_channel->onError(nullptr);
            m_channel->unbindQueue(m_exchange, m_recvQueue, m_route_in)
              .onSuccess([this]() {
                m_state = eCloseChannel;
#ifndef NDEBUG
std::clog << "Transceiver eUnbindQueue(" << m_recvQueue << ") -> " << m_state << std::endl;
#endif
                StateMachine();
              })
              .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eUnbindQueue(" << m_recvQueue << ") error: " << message << std::endl;
#endif
                if (m_ec == eNoError) m_ec = eUnbindQueueError;
                if (m_error.empty()) m_error = message;
                m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eUnbindQueue -> " << m_state << std::endl;
#endif
                StateMachine();
              });
          });
          m_channel->onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eUnbindQueue restore channel error: " << message << std::endl;
#endif
            if (m_state != eUnbindQueue) return;
            m_error = message;
            m_ec = eCreateChannelError;
            m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eCreateChannel -> " << m_state << std::endl;
#endif
            StateMachine();
          });
        }
      break;
    case eRemoveQueue:
      if (m_queueExist)
        {
          m_state = eCloseChannel;
#ifndef NDEBUG
std::clog << "Transceiver eRemoveQueue -> " << m_state << std::endl;
#endif
          StateMachine();
        }
        else
        {
          // replace broken channel and remove newly created queue
          m_channel->close();
          std::shared_ptr<AMQP::Channel> channel =
            std::make_shared<AMQP::Channel>(m_connection);
          m_channel.swap(channel);
          m_channel->onReady([this]() {
            if (m_state != eRemoveQueue) return;
            // drop unnecessary callback
            m_channel->onError(nullptr);
            m_channel->removeQueue(m_recvQueue)
              .onSuccess([this](uint32_t deletedmessages) {
                UNUSED(deletedmessages)
                m_state = eCloseChannel;
#ifndef NDEBUG
std::clog << "Transceiver eRemoveQueue(" << m_recvQueue << ") -> " << m_state << std::endl;
#endif
                StateMachine();
              })
              .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eRemoveQueue(" << m_recvQueue << ") error: " << message << std::endl;
#endif
                if (m_ec == eNoError) m_ec = eRemoveQueueError;
                if (m_error.empty()) m_error = message;
                m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eRemoveQueue -> " << m_state << std::endl;
#endif
                StateMachine();
              });
          });
          m_channel->onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eRemoveQueue restore channel error: " << message << std::endl;
#endif
            if (m_state != eRemoveQueue) return;
            m_error = message;
            m_ec = eCreateChannelError;
            m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eRemoveQueue -> " << m_state << std::endl;
#endif
            StateMachine();
          });
        }
      break;
    case eCloseChannel:
      m_channel->close()
        .onSuccess([this]() {
          m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eCloseChannel -> " << m_state << std::endl;
#endif
          StateMachine();
        })
        .onError([this](const char* message) {
#ifndef NDEBUG
std::clog << "Transceiver eCloseChannel error: " << message << std::endl;
#endif
          if (m_ec == eNoError) m_ec = eCloseChannelError;
          if (m_error.empty()) m_error = message;
          m_state = eEnd;
#ifndef NDEBUG
std::clog << "Transceiver eCloseChannel -> " << m_state << std::endl;
#endif
          StateMachine();
        });
      break;
    case eEnd:
      m_connection = nullptr;
      m_recvQueue.clear();
      m_consumerTag.clear();
      m_queueExist = false;
      m_channel.reset();
#ifndef NDEBUG
std::clog << "Transceiver eEnd" << std::endl;
#endif
      if (m_onExit) m_onExit(m_ec);
      break;
    default:
      break;
  }
}
