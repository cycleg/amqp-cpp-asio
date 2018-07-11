#include <iostream> // debug output
#include <boost/asio/deadline_timer.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include "AmqpConnectionHandler.hpp"
#include "AmqpConnector.hpp"

using namespace amqp;

template class Connector<Transceiver>;

template <class TransceiverImpl>
Connector<TransceiverImpl>::Connector(boost::asio::io_service& service,
                                      std::string brokerUrl):
  m_address(brokerUrl),
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
typename Connector<TransceiverImpl>::iterator
Connector<TransceiverImpl>::transceiver(const std::string& exchange,
                                        const std::string& queue_,
                                        const std::string& route_in,
                                        bool listener)
{
  TransceiverPtr transceiver = std::make_shared<Transceiver>(
    exchange, queue_, route_in, listener
  );
  // TODO: thread safety
  m_transceivers.push_front(transceiver);
  return m_transceivers.begin();
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::open(Connector<TransceiverImpl>::iterator i)
{
  // TODO: thread safety
  TransceiverPtr t(*i);
  if (!t->is_running() && ready())
  {
    t->start(m_amqpConnection.get());
    while (t->is_running() && !t->ready()) m_service.run_one();
  }
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::close(Connector<TransceiverImpl>::iterator i)
{
  // TODO: thread safety
  TransceiverPtr t(*i);
  if (t->is_running())
  {
    t->stop();
    while (t->is_running()) m_service.run_one();
  }
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::remove(Connector<TransceiverImpl>::iterator i)
{
  // TODO: thread safety
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
    m_service,
    m_address.hostname(),
    boost::lexical_cast<std::string>(m_address.port()),
    boost::bind(&Connector<TransceiverImpl>::onShutdown, this, _1)
  );
  m_connectionHandler.swap(connectionHandler);
  m_connectionHandler->start(
    boost::bind(&Connector<TransceiverImpl>::onConnected, this)
  );
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::start()
{
  // TODO: thread safety
  if (m_connectionHandlerReady) return;
  async_start();
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
std::clog << "Connector::run()" << std::endl;
#endif
  {
    auto work = std::make_shared< boost::asio::io_service::work >(m_service);
    m_sentinel.swap(work);
  }
  for (auto& i: m_transceivers)
    if (!i->is_running())
    {
#ifndef NDEBUG
std::clog << "Connector::run() " << i->route_in() << "@" << i->exchange_point() << std::endl;
#endif
      i->start(m_amqpConnection.get());
    }
  for (auto& i: m_transceivers)
  {
    while (i->is_running() && !i->ready()) m_service.run_one();
  }
#ifndef NDEBUG
std::clog << "Connector::run() m_connectionHandlerReady = " << m_connectionHandlerReady << std::endl;
#endif
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::stop()
{
#ifndef NDEBUG
std::clog << "Connector::stop() " << m_connectionHandlerReady << std::endl;
#endif
  // TODO: thread safety
  if (!m_connectionHandlerReady) return;
  for (auto& i: m_transceivers)
  {
    i->stop();
  }
  m_service.post([this]() {
    for (auto& i: m_transceivers)
    {
      while (i->is_running()) m_service.run_one();
    }
    if (m_connectionHandler->stopped())
      {
        if (m_connectionHandlerReady)
        {
          // handler's shutdown callback will not called
          m_connectionHandlerReady = false;
          m_amqpConnection.reset();
          m_sentinel.reset();
          if (m_exitCb) m_exitCb(eNormal);
        }
      }
      else
      {
        // prevent infinite loop in handler's shutdown callback
        m_exiting = true;
#ifndef NDEBUG
std::clog << "Connector::stop() before handler stop" << std::endl;
#endif
        m_connectionHandler->stop();
#ifndef NDEBUG
std::clog << "Connector::stop() after handler stop" << std::endl;
#endif
      }
  });
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::onConnected()
{
  m_connectionHandlerReady = true;
#ifndef NDEBUG
std::clog << "connection handler m_connectionHandlerReady = " << m_connectionHandlerReady << std::endl;
#endif
  auto connection(std::make_shared<AMQP::Connection>(
    m_connectionHandler.get(), m_address.login(), m_address.vhost()
  ));
  m_amqpConnection.swap(connection);
  m_sentinel.reset();
#ifndef NDEBUG
std::clog << "Connector::async_start() after m_sentinel.reset()" << std::endl;
#endif
  if (m_startedCb) m_startedCb();
#ifndef NDEBUG
std::clog << "Connector::async_start() after callback" << std::endl;
#endif
}

template <class TransceiverImpl>
void Connector<TransceiverImpl>::onShutdown(const std::string& message)
{
#ifndef NDEBUG
std::clog << "connection handler shutdown";
#endif
  if (!message.empty())
  {
#ifndef NDEBUG
std::clog << ": " << message;
#endif
  }
#ifndef NDEBUG
std::clog << std::endl;
#endif
  if (m_exiting)
  {
    // regular stop
    m_connectionHandlerReady = false;
    m_amqpConnection.reset();
    m_sentinel.reset();
    if (m_exitCb) m_exitCb(eNormal);
    return;
  }
  if (!m_connectionHandlerReady)
  {
    // can't open connection to broker
    m_amqpConnection.reset();
    m_sentinel.reset();
    if (m_exitCb) m_exitCb(eBrokerConnectError);
    return;
  }
  if (m_connectionHandler->amqp_error())
    {
      for (auto& i: m_transceivers)
      {
        i->drop();
#ifndef NDEBUG
std::clog << i->route_in() << "@" << i->exchange_point() << ": " << i->error() << std::endl;
#endif
      }
      m_connectionHandlerReady = false;
      m_amqpConnection.reset();
      m_sentinel.reset();
      if (m_exitCb) m_exitCb(eAmqpError);
    }
    else stop();
}
