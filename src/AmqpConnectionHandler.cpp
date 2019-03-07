#include <iostream> // debug output
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "AmqpConnectionHandler.hpp"

#define UNUSED(x) (void)x;

using namespace amqp;

ConnectionHandler::ConnectionHandler(boost::asio::io_service& service,
                                     const std::string& host,
                                     const std::string& port,
                                     ShutdownCallback shutdownCb):
  m_service(service),
  m_socket(m_service),
  m_inBuf(std::make_shared<boost::asio::streambuf>()),
  m_host(host),
  m_port(port),
  m_connection(nullptr),
  m_state(eNotConnected),
  m_resolver(m_service),
  m_connectedCb(nullptr),
  m_shutdownCb(shutdownCb),
  m_amqpError(false),
  m_readReq(false),
  m_writeReq(false)
{
}

ConnectionHandler::~ConnectionHandler()
{
  stop();
}

void ConnectionHandler::start(ConnectedCallback connected)
{
  if (m_state != eNotConnected) return;
  m_connectedCb = connected;
  m_amqpError = false;
  m_state = eResolving;
  StateMachine();
}

void ConnectionHandler::stop()
{
  if (m_state == eNotConnected) return;
#ifndef NDEBUG
std::clog << "ConnectionHandler::stop()" << std::endl;
#endif
  m_lastError.clear();
  m_state = eShutdown;
  StateMachine();
}

#if 0
uint16_t ConnectionHandler::onNegotiate(AMQP::Connection* connection,
                                            uint16_t interval)
{
  if (!m_connection) m_connection = connection;
  // default implementation, suggested heartbeat is ok
  return interval;
}
#endif

void ConnectionHandler::onData(AMQP::Connection* connection,
                               const char* buffer, size_t size)
{
  if (!connected()) return;
  if (!m_connection) m_connection = connection;
  if (m_state == eReceiverInit)
  {
    m_state = eReady;
    StateMachine();
  }
  auto outBuf(std::make_shared< boost::asio::streambuf >());
  std::ostream os(outBuf.get());
  os.write(buffer, size);
#ifndef NDEBUG
std::clog << "ConnectionHandler::onData() " << outBuf->size() << "!" << uint64_t(outBuf.get()) << std::endl;
#endif
  m_outBufs.push(outBuf);
  if (!m_writeReq)
  {
#ifndef NDEBUG
std::clog << "ConnectionHandler::onData() wait for write " << m_outBufs.size() << std::endl;
#endif
    m_writeReq = true;
    boost::asio::async_write(
      m_socket, *m_outBufs.front().get(),
      boost::bind(&ConnectionHandler::onWrite, this,
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred)
    );
  }
}

void ConnectionHandler::onError(AMQP::Connection* connection,
                                const char* message)
{
  if (!connected()) return;
  if (!m_connection) m_connection = connection;
  m_lastError = "AMQP-CPP error: ";
  m_lastError.append(message);
  m_amqpError = true;
#ifndef NDEBUG
std::clog << "ConnectionHandler::onError() " << m_lastError << std::endl;
#endif
  m_state = eShutdown;
  StateMachine();
}

void ConnectionHandler::onClosed(AMQP::Connection* connection)
{
  if (!connected()) return;
  if (!m_connection) m_connection = connection;
#ifndef NDEBUG
std::clog << "ConnectionHandler::onClosed()" << std::endl;
#endif
  m_lastError.clear();
  m_state = eShutdown;
  StateMachine();
}

void ConnectionHandler::StateMachine()
{
  switch (m_state)
  {
    case eResolving:
#ifndef NDEBUG
std::clog << "ConnectionHandler::StateMachine() before async_resolve()" << std::endl;
#endif
      m_resolver.async_resolve(boost::asio::ip::tcp::resolver::query(m_host, m_port),
                               [this](const boost::system::error_code& ec,
                                      boost::asio::ip::tcp::resolver::iterator i) {
        // something happened...
        if (m_state != eResolving) return;
        if (ec) {
          m_lastError = "failed to resolve: ";
          m_lastError.append(ec.message());
          m_state = eNotConnected;
          StateMachine();
          return;
        }
        m_rIterator = i;
        m_state = eConnecting;
        StateMachine();
      });
#ifndef NDEBUG
std::clog << "ConnectionHandler::StateMachine() after async_resolve()" << std::endl;
#endif
      break;
    case eConnecting:
#ifndef NDEBUG
std::clog << "ConnectionHandler::StateMachine() before async_connect()" << std::endl;
#endif
      boost::asio::async_connect(m_socket, m_rIterator,
                                 [this](const boost::system::error_code& ec,
                                        boost::asio::ip::tcp::resolver::iterator i) {
        if (m_state != eConnecting) return;
        if (ec || (i == boost::asio::ip::tcp::resolver::iterator()))
        {
          m_lastError = "failed to connect: ";
          m_lastError.append(ec.message());
          m_state = eNotConnected;
          StateMachine();
          return;
        }
        m_state = eReceiverInit;
        StateMachine();
      });
#ifndef NDEBUG
std::clog << "ConnectionHandler::StateMachine() after async_connect() " << m_socket.is_open() << std::endl;
#endif
      break;
    case eReceiverInit:
      if (m_connectedCb) m_connectedCb();
      break;
    case eReady:
      {
        // initialize i/o buffers and set read callback
#ifndef NDEBUG
std::clog << "ConnectionHandler::StateMachine() read buffer size = " << m_connection->maxFrame() << std::endl;
#endif
        m_readReq = true;
        m_socket.async_read_some(
          m_inBuf->prepare(m_connection->maxFrame()),
          boost::bind(&ConnectionHandler::onRead, this,
                      boost::asio::placeholders::error,
                      boost::asio::placeholders::bytes_transferred)
        );
#ifndef NDEBUG
std::clog << "ConnectionHandler::StateMachine() read callback installed, bytes in input " << m_inBuf->size() << std::endl;
#endif
      }
      break;
    case eShutdown:
      {
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint endpoint = m_socket.remote_endpoint(ec);
        UNUSED(endpoint)
        try
        {
          // if connection close by other side, the underlying descriptor
          // already closed
          if (!ec)
            m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        }
        catch (boost::system::system_error& e)
        {
          // socket already dead
        }
        // waiting for asynchronous handlers complete
        while (m_readReq || m_writeReq) m_service.run_one();
        try
        {
          if (!ec) m_socket.close();
        }
        catch (boost::system::system_error& e)
        {
          // ignore error
        }
        // clear i/o buffers
        auto buf(std::make_shared<boost::asio::streambuf>());
        m_inBuf.swap(buf);
      }
      while (!m_outBufs.empty()) m_outBufs.pop();
      m_connection = nullptr;
      m_state = eNotConnected;
      StateMachine();
      break;
    case eNotConnected:
      if (m_shutdownCb) m_shutdownCb(m_lastError);
      m_lastError.clear();
      break;
    default:
      break;
  }
}

void ConnectionHandler::onRead(const boost::system::error_code& ec,
                               std::size_t bytes)
{
  m_readReq = false;
  if (!connected()) return;
  if (ec)
  {
    if (m_state != eShutdown)
    {
      m_lastError = "read error: ";
      m_lastError.append(ec.message());
      m_state = eShutdown;
      StateMachine();
    }
    return;
  }
#ifndef NDEBUG
std::clog << "ConnectionHandler::onRead() received " << bytes << std::endl;
#endif
  m_inBuf->commit(bytes);
  // Advanced Message Queuing Protocol Specification v0-9-1:
  // "The client opens a TCP/IP connection to the server and sends a protocol
  // header."
  // Thus, onData() called firstly, and m_connection will be filled.
  if (m_inBuf->size() >= m_connection->expected())
  {
    uint64_t parsed = 0;
    do
    {
      std::string buf;
      buf.assign(boost::asio::buffers_begin(m_inBuf->data()),
                 boost::asio::buffers_begin(m_inBuf->data()) + m_inBuf->size());
#ifndef NDEBUG
std::clog << "ConnectionHandler::onRead() in buf " << buf.size() << std::endl;
#endif
      parsed = m_connection->parse(buf.data(), buf.size());
#ifndef NDEBUG
std::clog << "ConnectionHandler::onRead() parsed " << parsed << std::endl;
#endif
      // If broker unexpectedly close connection, this object already cleared.
      if (!connected()) return;
      m_inBuf->consume(parsed);
#ifndef NDEBUG
std::clog << "ConnectionHandler::onRead() remain " << m_inBuf->size() << std::endl;
#endif
    } while ((m_inBuf->size() >= m_connection->expected()) && parsed);
  }
  m_readReq = true;
  m_socket.async_read_some(
    m_inBuf->prepare(m_connection->maxFrame()),
    boost::bind(&ConnectionHandler::onRead, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred)
  );
}

void ConnectionHandler::onWrite(const boost::system::error_code& ec,
                                std::size_t bytes)
{
  UNUSED(bytes)
  m_writeReq = false;
  if (!connected()) return;
#ifndef NDEBUG
std::clog << "ConnectionHandler::onWrite() send " << bytes << std::endl;
std::clog << "ConnectionHandler::onWrite() remain " << m_outBufs.front()->size() << "!" << uint64_t(m_outBufs.front().get()) << std::endl;
#endif
  if (ec)
  {
    if (m_state != eShutdown)
    {
      m_lastError = "write error: ";
      m_lastError.append(ec.message());
      m_state = eShutdown;
      StateMachine();
    }
    return;
  }
  if (!m_outBufs.front()->size())
  {
#ifndef NDEBUG
std::clog << "ConnectionHandler::onWrite() drop " << uint64_t(m_outBufs.front().get()) << std::endl;
#endif
    m_outBufs.pop();
  }
  if (!m_outBufs.empty())
  {
#ifndef NDEBUG
std::clog << "ConnectionHandler::onWrite() wait for write " << m_outBufs.size() << std::endl;
std::clog << "ConnectionHandler::onWrite() " << m_outBufs.front()->size() << "!" << uint64_t(m_outBufs.front().get()) << std::endl;
#endif
    m_writeReq = true;
    boost::asio::async_write(
      m_socket, *m_outBufs.front().get(),
      boost::bind(&ConnectionHandler::onWrite, this,
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred)
    );
  }
}
