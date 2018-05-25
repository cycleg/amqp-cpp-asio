#pragma once

#include <functional>
#include <memory>
#include <list>
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

    ///
    /// Указатель на экземпляр приемопередатчика.
    ///
    typedef std::shared_ptr< TransceiverImpl > TransceiverPtr;
    ///
    /// Тип списка приемопередатчиков.
    ///
    typedef std::list<TransceiverPtr> TransceiverList;
    typedef typename TransceiverList::iterator iterator;

    typedef std::function<void()> StartedCallback;
    typedef std::function<void(ExitCode)> ExitCallback;

    Connector(boost::asio::io_service& service, std::string brokerUrl);
    ~Connector();

    Connector(const Connector&) = delete;

    inline bool ready() const { return m_connectionHandlerReady; }

    inline void onExit(ExitCallback callback) { m_exitCb = callback; }
    inline ExitCallback getOnExit() const { return m_exitCb; }
    inline boost::asio::io_service& io_service() { return m_service; }
    inline std::string url() const { return std::string(m_address); }

    inline iterator begin() { return m_transceivers.begin(); }
    inline iterator end() { return m_transceivers.end(); }

    iterator transceiver(const std::string& exchange,
                         const std::string& queue_,
                         const std::string& route_in,
                         bool listener);
    void open(iterator i);
    void close(iterator i);
    void remove(iterator i);

    template<class Message>
    bool send(iterator i, const Message& message,
              const std::string& route, bool mandatory = true)
    {
      // TODO: thread safety
      if (!m_connectionHandlerReady) return false;
      return (*i)->send(message, route, mandatory);
    }

    void async_start(StartedCallback callback = nullptr);
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
    AMQP::Address m_address; ///< Адрес брокера AMQP.
    TransceiverList m_transceivers; ///< Контейнер приемопередатчиков.
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
