#pragma once

#include <functional>
#include <queue>
#include <memory>
#include <string>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <amqpcpp.h>

namespace amqp {

///
/// Класс, реализующий интерфейс ввода-вывода для AMQP-CPP.

/// Библиотека AMQP-CPP не содержит средств ввода-вывода, а полагается на
/// предоставляемую извне реализацию класса AMQP::ConnectionHandler. Данный
/// класс является такой реализацией на базе библиотеки boost::asio.
///
/// ConnectionHandler устанавливает и сопровождает TCP-соединение с брокером
/// AMQP. Все операции ввода-вывода производятся асинхронно. Класс не является
/// потокобезопасным.
///
/// Входящие данные накапливаются в общем буфере, откуда в методе onRead()
/// передаются на обработку AMQP-CPP. Исходящие данные, напротив, помещаются в
/// очередь в виде отдельных буферов. Очередной буфер на отправку добавляется
/// каждым вызовом onData(). При закрытии соединения все буфера сбрасываются.
///
/// Экземпляры класса пригодны для повторного использования, т.е. пара методов
/// start()/stop() может вызываться для одного экземпляра много раз.
///
/// ConnectionHandler реализован как конечный автомат.
///
/// @startuml
/// [*] -> eNotConnected
/// eNotConnected -> eResolving : ConnectionHandler::start()
/// eResolving -> eConnecting: Success
/// eResolving -> eNotConnected : Fail
/// eConnecting --> eReceiverInit : Success
/// eConnecting -> eNotConnected : Fail
/// eReceiverInit --> eReady : First data in
/// eReceiverInit --> eShutdown : Data read error
/// eReady --> eShutdown : ConnectionHandler::stop()
/// eReady --> eShutdown : ConnectionHandler::onError()
/// eReady --> eShutdown : ConnectionHandler::onClosed()
/// eReady --> eShutdown : Data write error
/// eShutdown --> [*]
/// @enduml
///
/// @author cycleg
///
class ConnectionHandler: public AMQP::ConnectionHandler
{
  public:
    typedef std::function<void()>
      ConnectedCallback; ///< Указатель на функцию, вызываемую после
                         ///< установления TCP-соединения с брокером.

    typedef std::function<void(const std::string& message)>
      ShutdownCallback; ///< Указатель на функцию, вызываемую после завершения
                        ///< работы конечного автомата.

    ///
    /// Конструктор.
    ///
    /// @param [in] service
    /// @param [in] host Имя или адрес хоста брокера AMQP.
    /// @param [in] port TCP-порт брокера AMQP.
    /// @param [in] shutdownCb Обратный вызов для закрытия соединения.
    ///
    /// Обратный вызов выполняется после закрытия соединения с брокером.
    ///
    ConnectionHandler(boost::asio::io_service& service,
                      const std::string& host, const std::string& port,
                      ShutdownCallback shutdownCb);
    ///
    /// Деструктор.
    ///
    ~ConnectionHandler();

    ///
    /// Копирующий конструктор запрещен.
    ///
    ConnectionHandler(const ConnectionHandler&) = delete;

    ///
    /// Готовность соединения к работе.
    ///
    /// @return Готово или нет.
    ///
    inline bool ready() const
    { return (m_state == eReceiverInit) || (m_state == eReady); }
    ///
    /// Соединение остановлено.
    ///
    /// @return Остановлено или нет.
    ///
    /// Соединение не остановлено, если находится в любом состоянии, кроме
    /// eNotConnected. Соединение запускается вызовом start(),
    /// останавливается -- stop(), или в случае какой-либо ошибки.
    /// 
    inline bool stopped() const { return m_state == eNotConnected; }
    ///
    /// Ошибка AMQP-CPP.
    ///
    /// @return Возвращает true, если имела место ошибка.
    ///
    /// Ошибка AMQP-CPP фиксируется, если был вызван метод onError().
    ///
    inline bool amqp_error() const { return m_amqpError; }

    ///
    /// Запустить (открыть) соединение с брокером AMQP.
    ///
    /// @param [in] callback Обратный вызов при установлении TCP-соединения.
    ///
    /// Если соединение не остановлено, не делает ничего. Соединение
    /// устанавливается асинхронно.
    ///
    void start(ConnectedCallback callback);
    ///
    /// Остановить (закрыть) соединение с брокером.
    ///
    /// Если соединение уже разорвано, не делает ничего. Соединение
    /// разрывается синхронно.
    ///
    void stop();

#if 0
    uint16_t onNegotiate(AMQP::Connection* connection, uint16_t interval);
#endif

    ///
    /// Вызывается для отправки данных из AMQP-CPP.
    ///
    /// @param [in] connection Указатель на класс соединения с брокером AMQP.
    /// @param [in] buffer Буфер данных для отправки.
    /// @param [in] size Размер буфера данных для отправки.
    ///
    /// Реализация AMQP::ConnectionHandler::onData().
    ///
    void onData(AMQP::Connection* connection, const char* buffer, size_t size);
    ///
    /// Вызывается при возникновении ошибки из AMQP-CPP.
    ///
    /// @param [in] connection Указатель на класс соединения с брокером AMQP.
    /// @param [in] message Текст сообщения об ошибке.
    ///
    /// Для дальнейшего использования текст копируется в m_lastError.
    ///
    /// Реализация AMQP::ConnectionHandler::onError().
    ///
    void onError(AMQP::Connection* connection, const char* message);
    ///
    /// Вызывается при закрытии соединения в AMQP-CPP.
    ///
    /// @param [in] connection Указатель на класс соединения с брокером AMQP.
    ///
    /// Реализация AMQP::ConnectionHandler::onClosed().
    ///
    void onClosed(AMQP::Connection* connection);

  private:
    ///
    /// Состояния конечного автомата.
    ///
    enum State
    {
      eNotConnected, ///< Соединение отсутствует.
      eResolving, ///< Разрешается DNS-имя брокера AMQP.
      eConnecting, ///< Устанавливается TCP-соединение с брокером.
      eReceiverInit, ///< Ожидается начальный отклик брокера (запрос посылает
                     ///< AMQP-CPP).
      eReady, ///< Соединение готово к работе.
      eShutdown ///< Соединение закрывается.
    };

    ///
    /// Установлено TCP-соединение с брокером AMQP или нет.
    ///
    /// @return Вовзращает true, если ожидается начальный отклик или соединение
    ///         готово к работе.
    ///
    inline bool connected() const { return m_state > eConnecting; }

    ///
    /// Реализация конечного автомата соединения.
    ///
    /// Вызывается после каждой смены состояния, в том числе, рекурсивно.
    ///
    void StateMachine();

    ///
    /// Обратный вызов при завершении очередного асинхронного приема.
    ///
    /// @param [in] ec Код завершения асинхронной операции.
    /// @param [in] bytes Число полученных байтов.
    ///
    /// Вызывается из boost::asio.
    ///
    void onRead(const boost::system::error_code& ec, std::size_t bytes);
    ///
    /// Обратный вызов при завершении очередной асинхронной передачи.
    ///
    /// @param [in] ec Код завершения асинхронной операции.
    /// @param [in] bytes Число переданных байтов.
    ///
    /// Вызывается из boost::asio.
    ///
    void onWrite(const boost::system::error_code& ec, std::size_t bytes);

    boost::asio::io_service& m_service; ///< Экземпляр службы ввода/вывода
                                        ///< ASIO, через который проходят
                                        ///< данные.
    boost::asio::ip::tcp::socket m_socket; ///< Сокет соединения с брокером AMQP.
    std::shared_ptr<boost::asio::io_service::work>
      m_sentinel; ///< "Сторож", не дает циклу службы ввода/вывода, заданной в
                  ///< конструкторе, завершиться раньше времени.
    std::shared_ptr<boost::asio::streambuf> m_inBuf; ///< Буфер входящих данных.
    std::queue< std::shared_ptr<boost::asio::streambuf> >
      m_outBufs; ///< Очередь буферов исходящих данных.
    std::string m_host, ///< Имя или адрес хоста брокера.
                m_port, ///< TCP-порт брокера.
                m_lastError; ///< Описание последней ошибки, возникшей в
                             ///< работе соединения с брокером.
    AMQP::Connection* m_connection; ///< Соединение с брокером на стороне AMQP-CPP.
    State m_state; ///< Текущее состояние соединения.
    boost::asio::ip::tcp::resolver m_resolver;
    boost::asio::ip::tcp::resolver::iterator m_rIterator;
    ConnectedCallback m_connectedCb; ///< Обратный вызов после установления
                                     ///< TCP-соединения с брокером.
    ShutdownCallback m_shutdownCb; ///< Обратный вызов после закрытия соединения.
    bool m_amqpError, ///< Признак, что AMQP-CPP был вызан обработчик onError().
         m_readReq, ///< Признак, что запущена асинхронная операция приема из сокета.
         m_writeReq; ///< Признак, что запущена асинхронная операция отправки в сокет.
};

} // namespace amqp
