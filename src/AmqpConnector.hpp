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
    /// Коды завершения коннектора.
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
    ///
    /// Итератор списка приемопередатчиков.
    /// 
    typedef typename TransceiverList::iterator iterator;

    ///
    /// Указатель на функцию обратного вызова при успешном запуске коннектора.
    ///
    typedef std::function<void()> StartedCallback;
    ///
    /// Указатель на функцию обратного вызова при отключении коннектора.
    ///
    /// @param [in] ExitCode Код завершения коннектора.
    ///
    typedef std::function<void(ExitCode)> ExitCallback;

    ///
    /// Конструктор.
    ///
    /// @param [in] service Ссылка на экземпляр службы ввода/вывода.
    /// @param [in] brokerUrl URL брокера, с которым работает коннектор.
    ///
    Connector(boost::asio::io_service& service, std::string brokerUrl);
    ///
    /// Деструктор.
    ///
    ~Connector();

    ///
    /// Копирующий конструктор запрещен.
    ///
    Connector(const Connector&) = delete;

    ///
    /// Готовность коннектора к работе с брокером AMQP.
    ///
    /// @return Готов или нет.
    ///
    inline bool ready() const { return m_connectionHandlerReady; }

    ///
    /// Назначить обратный вызов для завершения коннектора.
    ///
    /// @param [in] callback Указатель на функцию обратного вызова.
    ///
    inline void onExit(ExitCallback callback) { m_exitCb = callback; }
    ///
    /// Получить указатель на текущую функцию обратного вызова.
    ///
    /// @return Указатель на функцию обратного вызова.
    ///
    inline ExitCallback getOnExit() const { return m_exitCb; }
    ///
    /// Извлечь ссылку на службу ввода/вывода, используемую коннектором.
    ///
    /// @return Ссылка на экземпляр службы ввода/вывода.
    ///
    inline boost::asio::io_service& io_service() { return m_service; }
    ///
    /// Получить URL на брокер, с которым работает коннектором.
    ///
    /// @return URL брокера AMQP.
    ///
    /// В URL может отсутствовать номер порта, если он совпадает с портом ФЬЙЗ
    /// по умолчанию.
    ///
    inline std::string url() const { return std::string(m_address); }

    ///
    /// Начало списка приемопередатчиков коннектора.
    ///
    /// @return Итератор начала списка.
    /// 
    inline iterator begin() { return m_transceivers.begin(); }
    ///
    /// Конец списка приемопередатчиков коннектора.
    ///
    /// @return Итератор конца списка.
    /// 
    inline iterator end() { return m_transceivers.end(); }

    ///
    /// Создать приемопередатчик с указанными параметрами.
    ///
    /// @param [in] exchange Имя точки обмена AMQP.
    /// @param [in] queue_ Имя очереди сообщений.
    /// @param [in] route_in Маршрут входящих сообщений.
    /// @param [in] listener Флаг, что экземпляр будет работать на прием.
    /// @return Итератор вновь созданного приемопередатчика.
    ///
    /// Если новый экземпляр работает только на передачу, имя очереди и
    /// маршрут игнорируются.
    ///
    iterator transceiver(const std::string& exchange,
                         const std::string& queue_,
                         const std::string& route_in,
                         bool listener);
    ///
    /// Включить указанный приемопередатчик.
    ///
    /// @param [in] i Итератор приемопередатчика.
    ///
    void open(iterator i);
    ///
    /// Выключить указанный приемопередатчик.
    ///
    /// @param [in] i Итератор приемопередатчика.
    ///
    void close(iterator i);
    ///
    /// Удалить указанный приемопередатчик.
    ///
    /// @param [in] i Итератор приемопередатчика.
    ///
    void remove(iterator i);

    ///
    /// Отправить сообщение через указанный приемопередатчик.
    ///
    /// @param [in] i Итератор приемопередатчика.
    /// @param [in] message Исходящее сообщение.
    /// @param [in] route Маршрут отправки.
    /// @param [in] mandatory Флаг "mandatory" (необязательный, по умолчанию
    ///                       установлен).
    ///
    template<class Message>
    bool send(iterator i, const Message& message,
              const std::string& route, bool mandatory = true)
    {
      // TODO: thread safety
      if (!m_connectionHandlerReady) return false;
      return (*i)->send(message, route, mandatory);
    }

    ///
    /// Инициировать работу с брокером асинхронно.
    ///
    /// @param [in] callback Функция обратного вызова для успешного
    ///                      завершения процедуры (необязательный, по
    ///                      умолчанию отсутствует).
    ///
    /// В случае неудачного подключения запускается функция обратного вызова
    /// на завершение работы конектора с кодом eBrokerConnectError.
    ///
    /// Если ни один обратный вызов не задан, готовность коннектора к работе с
    /// брокером можно определить по значению ready().
    ///
    void async_start(StartedCallback callback = nullptr);
    ///
    /// Инициировать работу с брокером.
    ///
    /// Синхронная версия async_start(). Заверашется запуском одной из функций
    /// обратного вызова: на звершение работы коннектора или на успешное
    /// подключение к брокеру.
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
    /// Соединение с брокером AMQP установлено.
    ///
    void onConnected();
    ///
    /// Соединение с брокером не установлено или разорвано.
    ///
    /// @param [in] message Сообщение о причине разрыва соединения.
    ///
    /// В случае нормального закрытия соединения вызывается m_exitCb с кодом
    /// eNormal, в случае неудачи при соединении -- с кодом
    /// eBrokerConnectError, в случае ошибки во время работы с брокером -- с
    /// кодом eAmqpError. В последнем случае предварительно сбрасываются, но
    /// не удаляются, все имеющиеся приемопередатчики.
    ///
    void onShutdown(const std::string& message);

    AMQP::Address m_address; ///< Адрес брокера AMQP.
    TransceiverList m_transceivers; ///< Контейнер приемопередатчиков.
    boost::asio::io_service& m_service; ///< Ссылка на экземпляр цикла
                                        ///< ввода/вывода boost::asio,
                                        ///< используемого экземпляром
                                        ///< данного класса.
    std::shared_ptr< boost::asio::io_service::work >
      m_sentinel; ///< "Сторож", не дает циклу службы ввода/вывода, заданной в
                  ///< конструторе завершиться до окончания работы в
                  ///< экземпляре данного класса. Работа происходит при
                  ///< подключении (синхронном иили нет) к брокеру и после
                  ///< вызова run(). Завершается в случае удачного/неудачного
                  ///< подключения и перед запуском функции обратного вызова
                  ///< при отключении от брокера. При удачном подключении
                  ///< функция обратного вызова запускается после завершения
                  ///< работы.
    std::shared_ptr< AMQP::Connection > m_amqpConnection; ///< Указатель на
                                                          ///< класс соединения
                                                          ///< с брокером из
                                                          ///< библиотеки
                                                          ///< AMQP-CPP.
    std::shared_ptr< ConnectionHandler > m_connectionHandler; ///< Указатель
                                                              ///< на обработчик
                                                              ///< соединения с
                                                              ///< брокером.
    bool m_exiting, ///< Признак запуска штатной остановки через stop().
         m_connectionHandlerReady; ///< Признак готовности обработчика
                                   ///< соединения с брокером к работе.
    StartedCallback m_startedCb; ///< Обратный вызов после установления
                                 ///< успешного соединения с брокером.
    ExitCallback m_exitCb; ///< Обратный вызов при завершении работы.
};

} // namespace amqp
