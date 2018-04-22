#include <iostream> // debug output
#include <boost/asio/deadline_timer.hpp>
#include <boost/lexical_cast.hpp>
#include "AmqpConnectionHandler.hpp"
#include "AmqpConnector.hpp"

using namespace amqp;

template class Connector<Transceiver>;

template <class TransceiverImpl>
Connector<TransceiverImpl>::Connector(boost::asio::io_service& service,
                                      std::string brokerUrl):
  m_address(brokerUrl),
  m_nextTransceiverId(0),
  m_service(service),
  m_exiting(false),
  m_connectionHandlerReady(false),
  m_startedCb(nullptr),
  m_exitCb(nullptr)
{
}

template <class TransceiverImpl>
Connector<TransceiverImpl>::~Connector()
{
  stop();
}

template <class TransceiverImpl>
typename Connector<TransceiverImpl>::TransceiverId
Connector<TransceiverImpl>::transceiver(const std::string& exchange,
                                        const std::string& queue_,
                                        const std::string& route_in,
                                        bool listener)
{
  TransceiverPtr transceiver = std::make_shared<Transceiver>(
    exchange, queue_, route_in, listener
  );
  // TODO: thread safety
  TransceiverId id = m_nextTransceiverId++;
  while (transceiverExists(id)) id = m_nextTransceiverId++;
  m_transceivers.insert(
    std::pair<TransceiverId, TransceiverPtr>(id, transceiver)
  );
  return id;
}

template <class TransceiverImpl>
bool Connector<TransceiverImpl>::transceiverReady(TransceiverId id) const
{
  // TODO: thread safety
  auto i = m_transceivers.find(id);
  return (i == m_transceivers.end()) ? false : i->second->ready();
}

template <class TransceiverImpl>
bool Connector<TransceiverImpl>::transceiverRunning(TransceiverId id) const
{
  // TODO: thread safety
  auto i = m_transceivers.find(id);
  return (i == m_transceivers.end()) ? false : i->second->is_running();
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::onBounceMessage(
  TransceiverId id,
  typename TransceiverImpl::BounceCallback callback
)
{
  // TODO: thread safety
  auto i = m_transceivers.find(id);
  if (i == m_transceivers.end()) return;
  i->second->onBounce(callback);
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::onMessage(
  TransceiverId id,
  typename TransceiverImpl::MessageCallback callback
)
{
  // TODO: thread safety
  auto i = m_transceivers.find(id);
  if (i == m_transceivers.end()) return;
  i->second->onMessage(callback);
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::onTransceiverExit(
  TransceiverId id,
  typename TransceiverImpl::ExitCallback callback
)
{
  // TODO: thread safety
  auto i = m_transceivers.find(id);
  if (i == m_transceivers.end()) return;
  i->second->onExit(callback);
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::open(TransceiverId id)
{
  // TODO: thread safety
  if (!m_connectionHandlerReady) return;
  auto i = m_transceivers.find(id);
  if (i == m_transceivers.end()) return;
  if (!i->second->is_running())
  {
    i->second->start(m_amqpConnection.get());
    while (i->second->is_running() && !i->second->ready())
      m_service.run_one();
  }
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::close(TransceiverId id)
{
  // TODO: thread safety
  auto i = m_transceivers.find(id);
  if (i == m_transceivers.end()) return;
  if (i->second->is_running())
  {
    i->second->stop();
    while (i->second->is_running()) m_service.run_one();
  }
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::remove(TransceiverId id)
{
  // TODO: thread safety
  auto i = m_transceivers.find(id);
  if (i == m_transceivers.end()) return;
  m_transceivers.erase(i);
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::async_start(StartedCallback callback)
{
  // TODO: thread safety
  if (m_connectionHandlerReady) return;
  m_exiting = false;
  m_startedCb = callback;
  {
    auto work = std::make_shared< boost::asio::io_service::work >(m_service);
    m_sentinel.swap(work);
  }
  auto connectionHandler = std::make_shared<ConnectionHandler>(
    m_service, m_address.hostname(),
    boost::lexical_cast<std::string>(m_address.port()),
    [this](const std::string& message) {
      // on shutdown callback
#ifndef NDEBUG
std::cout << "connection handler shutdown";
#endif
      if (!message.empty())
      {
#ifndef NDEBUG
std::cout << ": " << message;
#endif
      }
#ifndef NDEBUG
std::cout << std::endl;
#endif
      if (m_exiting)
      {
        // regular stop
        m_connectionHandlerReady = false;
        if (m_exitCb) m_exitCb(eNormal);
        m_amqpConnection.reset();
        m_sentinel.reset();
        return;
      }
      if (!m_connectionHandlerReady)
      {
        // can't open connection to broker
        if (m_exitCb) m_exitCb(eBrokerConnectError);
        m_amqpConnection.reset();
        m_sentinel.reset();
        return;
      }
      if (m_connectionHandler->amqp_error())
        {
          for (auto& i: m_transceivers)
          {
            i.second->drop();
#ifndef NDEBUG
std::cout << i.first << ": " << i.second->error() << std::endl;
#endif
          }
          m_connectionHandlerReady = false;
          if (m_exitCb) m_exitCb(eAmqpError);
          m_amqpConnection.reset();
          m_sentinel.reset();
        }
        else stop();
    }
  );
  m_connectionHandler.swap(connectionHandler);
  m_connectionHandler->start([this]() {
    // on connected callback
    m_connectionHandlerReady = true;
#ifndef NDEBUG
std::cout << "connection handler m_connectionHandlerReady = " << m_connectionHandlerReady << std::endl;
#endif
    auto connection(std::make_shared<AMQP::Connection>(
      m_connectionHandler.get(), m_address.login(), m_address.vhost()
    ));
    m_amqpConnection.swap(connection);
    m_startedCb();
#ifndef NDEBUG
std::cout << "Connector::async_start() after callback" << std::endl;
#endif
    m_sentinel.reset();
#ifndef NDEBUG
std::cout << "Connector::async_start() after m_sentinel.reset()" << std::endl;
#endif
  });
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::start()
{
  // TODO: thread safety
  if (m_connectionHandlerReady) return;
  async_start([]() {});
  do
  {
    m_service.run_one();
  } while (!m_connectionHandler->stopped() && !m_connectionHandler->ready());
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::run()
{
  if (!m_connectionHandlerReady) return;
#ifndef NDEBUG
std::cout << "Connector::run()" << std::endl;
#endif
  {
    auto work = std::make_shared< boost::asio::io_service::work >(m_service);
    m_sentinel.swap(work);
  }
  for (auto& i: m_transceivers)
    if (!i.second->is_running())
    {
#ifndef NDEBUG
std::cout << "Connector::run() " << i.first << std::endl;
#endif
      i.second->start(m_amqpConnection.get());
    }
  for (auto& i: m_transceivers)
  {
    while (i.second->is_running() && !i.second->ready()) m_service.run_one();
  }
#ifndef NDEBUG
std::cout << "Connector::run() m_connectionHandlerReady = " << m_connectionHandlerReady << std::endl;
#endif
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::stop()
{
#ifndef NDEBUG
std::cout << "Connector::stop() " << m_connectionHandlerReady << std::endl;
#endif
  // TODO: thread safety
  if (!m_connectionHandlerReady) return;
  for (auto& i: m_transceivers)
  {
    i.second->stop();
  }
  m_service.post([this]() {
    for (auto& i: m_transceivers)
    {
      while (i.second->is_running()) m_service.run_one();
    }
    if (m_connectionHandler->stopped())
      {
        if (m_connectionHandlerReady)
        {
          // handler's shutdown callback will not called
          m_connectionHandlerReady = false;
          if (m_exitCb) m_exitCb(eNormal);
          m_amqpConnection.reset();
          m_sentinel.reset();
        }
      }
      else
      {
        // prevent infinite loop in handler's shutdown callback
        m_exiting = true;
#ifndef NDEBUG
std::cout << "Connector::stop() before handler stop" << std::endl;
#endif
        m_connectionHandler->stop();
#ifndef NDEBUG
std::cout << "Connector::stop() after handler stop" << std::endl;
#endif
      }
  });
}
