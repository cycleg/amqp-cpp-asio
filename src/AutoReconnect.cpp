#include <chrono>
#include <functional>
#include <iostream>
#if 0
#include "SyslogStreambuf.hpp"
#endif
#include "AutoReconnect.hpp"

AutoReconnect::AutoReconnect(const std::shared_ptr< amqp::Connector<> >& connector):
  m_connector(connector),
  m_started(false),
  m_needStop(false),
  m_rerun(false),
  m_backupExitCallback(nullptr),
  m_startedCallback(nullptr),
  m_timer(m_connector->io_service())
{
}

AutoReconnect::~AutoReconnect()
{
}

std::shared_ptr<AutoReconnect> AutoReconnect::Factory(
  const std::shared_ptr< amqp::Connector<> >& connector
)
{
  return std::shared_ptr<AutoReconnect>(new AutoReconnect(connector));
}

void AutoReconnect::start(amqp::Connector<>::StartedCallback callback)
{
  if (m_started) return;
  m_needStop = false;
  m_rerun = false;
  m_backupExitCallback = m_connector->getOnExit();
  m_startedCallback = callback;
  m_connector->onExit(std::bind(&AutoReconnect::restart, this, std::placeholders::_1));
  m_connector->async_start([this]() {
    this->started();
  });
  m_started = true;
}

void AutoReconnect::stop()
{
  if (!m_started) return;
  m_timer.cancel();
  if (m_connector->ready())
    m_connector->stop();
    else
    {
      // коннектор в процессе подключения
      m_needStop = true;
    }
  m_started = false;
}

void AutoReconnect::started()
{
#if 0
  std::clog << SyslogStreambuf::kLogInfo
            << m_connector->url() << " connection established" << std::endl;
#endif
  if (m_needStop)
  {
    m_needStop = false;
    m_connector->stop();
    return;
  }
  // вызывается ровно один раз: при первом установлении соединения
  if (m_startedCallback) m_startedCallback();
  m_startedCallback = nullptr;
  if (m_rerun)
  {
    m_rerun = false;
    m_connector->run();
  }
}

void AutoReconnect::restart(amqp::Connector<>::ExitCode code)
{
  if (!m_started)
    {
      m_connector->onExit(m_backupExitCallback);
      if (m_backupExitCallback) m_backupExitCallback(code);
      m_backupExitCallback = nullptr;
    }
    else
    {
      switch (code)
      {
        case amqp::Connector<>::eNormal:
          // коннектор остановили снаружи
          m_started = false;
          m_connector->onExit(m_backupExitCallback);
          if (m_backupExitCallback) m_backupExitCallback(code);
          m_backupExitCallback = nullptr;
          break;
        case amqp::Connector<>::eBrokerConnectError:
#if 0
          std::clog << SyslogStreambuf::kLogWarning
                    << m_connector->url() << " not connected, trying again..."
                    << std::endl;
#endif
          m_rerun = true;
          m_timer.expires_from_now(std::chrono::seconds(1));
          m_timer.async_wait([this](const boost::system::error_code& error) {
            if (error == boost::asio::error::operation_aborted) return;
            m_connector->async_start([this]() {
              started();
            });
          });
          break;
        case amqp::Connector<>::eAmqpError:
#if 0
          std::clog << SyslogStreambuf::kLogWarning
                    << m_connector->url() << " connection lost, restore..."
                    << std::endl;
#endif
          m_rerun = true;
          m_timer.expires_from_now(std::chrono::seconds(1));
          m_timer.async_wait([this](const boost::system::error_code& error) {
            // timer cancelled
            if (error == boost::asio::error::operation_aborted) return;
            m_connector->async_start([this]() {
              started();
            });
          });
          break;
        default:
          break;
      }
    }
}
