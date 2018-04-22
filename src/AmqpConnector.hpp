#pragma once

#include <functional>
#include <memory>
#include <map>
#include <string>
#include <boost/asio/io_service.hpp>
#include <amqpcpp.h>
#include <json/json.h>
#include "AmqpTransceiver.hpp"

namespace amqp {

class ConnectionHandler;

template<class TransceiverImpl = Transceiver>
class Connector
{
  public:
    ///
    /// Коды завершения.
    ///
    enum ExitCode
    {
      eNormal, ///< Ошибок нет.
      eBrokerConnectError, ///< Не удалось соединиться с брокером AMQP.
      eAmqpError ///< Библиотека AMQP-CPP сообщила об ошибке.
    };

    typedef unsigned int TransceiverId;
    typedef std::function<void()> StartedCallback;
    typedef std::function<void(ExitCode)> ExitCallback;

    Connector(boost::asio::io_service& service, std::string brokerUrl);
    ~Connector();

    Connector(const Connector&) = delete;

    inline bool ready() const { return m_connectionHandlerReady; }

    inline void onExit(ExitCallback callback) { m_exitCb = callback; }

    inline bool transceiverExists(TransceiverId id) const
    {
      // TODO: thread safety
      return m_transceivers.find(id) != m_transceivers.end();
    }

    TransceiverId transceiver(const std::string& exchange,
                              const std::string& queue_,
                              const std::string& route_in,
                              bool listener);
    bool transceiverReady(TransceiverId id) const;
    bool transceiverRunning(TransceiverId id) const;
    void onBounceMessage(TransceiverId id, typename TransceiverImpl::BounceCallback callback);
    void onMessage(TransceiverId id, typename TransceiverImpl::MessageCallback callback);
    void onTransceiverExit(TransceiverId id, typename TransceiverImpl::ExitCallback callback);
    void open(TransceiverId id);
    void close(TransceiverId id);
    void remove(TransceiverId id);

    template<class Message>
    bool send(TransceiverId id, const Message& message,
              const std::string& route, bool mandatory = true)
    {
      // TODO: thread safety
      if (!m_connectionHandlerReady) return false;
      auto i = m_transceivers.find(id);
      if (i == m_transceivers.end()) return false;
      return i->second->send(message, route, mandatory);
    }

    void async_start(StartedCallback callback);
    ///
    /// Инициировать работу с брокером.
    ///
    /// Синхронная версия async_start().
    ///
    void start();
    ///
    /// Запустить цикл работы с брокером.
    ///
    /// Метод синхронный, запускает все созданные и не запущенные на момент
    /// вызова приемопередатчики. После успешного инициирования работы с
    /// брокером и до ее завершения может вызываться многократно. Это полезно,
    /// если нужно создать и запустить сразу несколько приемопередатчиков.
    ///
    /// Если работа с брокером не была ранее успешно инициирована, то данный
    /// метод не делает ничего.
    ///
    void run();
    ///
    /// Завершить работу с брокером.
    ///
    /// Отключение от брокера происходит асинхронно, по завершении отключения
    /// производится обратный вызов, ранее заданный через onExit(). Если вызов
    /// не установлен, то проверить завершенность можно с помощью ready().
    /// Если метод возвращает false, то все приемопередатчики уже остановлены,
    /// а обработчик соединения, по крайней мере, отключается.
    ///
    /// Если ранее экземпляр не был ранее успешно инициирован вызовом start()
    /// или async_start(), то данный метод не делает ничего.
    ///
    void stop();

  private:
    ///
    /// Указатель на экземпляр приемопередатчика.
    ///
    typedef std::shared_ptr< TransceiverImpl > TransceiverPtr;

    AMQP::Address m_address; ///< Адрес брокера AMQP.
    TransceiverId m_nextTransceiverId; ///< Следующий свободный идентификатор
                                       ///< приемопередатчика.
    std::map<TransceiverId, TransceiverPtr>
      m_transceivers; ///< Карта-контейнер приемопередатчиков; ключ --
                      ///< идентификатор, значение -- премопередачик.
    boost::asio::io_service& m_service; ///< Ссылка на экземпляр цикла
                                        ///< ввода/вывода boost::asio,
                                        ///< используемого экземпляром
                                        ///< данного класса.
    std::shared_ptr< boost::asio::io_service::work >
      m_sentinel; ///< "Сторож", не дает циклу ввода/вывода boost::asio
                  ///< завершиться до завершения работы экземпляра данного класса.
    std::shared_ptr< AMQP::Connection > m_amqpConnection;
    std::shared_ptr< ConnectionHandler > m_connectionHandler;
    bool m_exiting, ///< Признак запуска штатной остановки через stop().
         m_connectionHandlerReady; ///< Признак готовности обработчика
                                   ///< соединения с брокером к работе.
    StartedCallback m_startedCb; ///< Обратный вызов после установления
                                 ///< соединения с брокером.
    ExitCallback m_exitCb; ///< Обратный вызов при завершении работы.
};

} // namespace amqp
