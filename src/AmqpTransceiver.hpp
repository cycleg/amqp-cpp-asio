#pragma once

#include <functional>
#include <memory>
#include <string>
#include <amqpcpp.h>
#include <json/json.h>

namespace amqp {

///
/// Класс, абстрагирующий работу с точкой обмена AMQP.

/// Каждый экземпляр класса предназначен для работы с одной точкой обмена
/// AMQP. Через нее можно как передавать, так и принимать сообщения.
///
/// При создании приемопередатчику задаются точка обмена, имя очереди для
/// входящих сообщений и маршрут (routing key) для них же, а также роль:
/// будет ли этот экземпляр работать только на передачу или еще и принимать
/// сообщения. В случае, если прием не предполагается, имя очереди и маршрут
/// игнорируются.
///
/// Создаваемая точка обмена имеет тип "topic" и установленные флаги
/// "autodelete" и "durable", т.е брокер удалит ее, когда не останется ни
/// одной связи (binding) с очередями, но точка сохранится в случае
/// перезапуска самого брокера.
///
/// Отправка сообщения производится через точку обмена по маршруту,
/// указываемому для каждого сообщения отдельно. При отправке сообщению можно
/// указать флаг "mandatory". Если он установлен (по умолчанию), то в случае
/// невозможности опубликовать сообщение брокер AMQP его возвращает. При этом
/// будет вызвана функция, назначенная методом onBounce(). Сигнатура ее должна
/// совпадать с BounceCallback. Вся логика, связанная с обработкой данной
/// ситуации должна быть реализована в указанной функции. Если функция для
/// данного приемопередатчика задана не была, то не делается ничего.
///
/// Прием сообщений происходит через указанную очередь. Она подключается к
/// точке обмена с заданным маршрутом. Имя очереди может быть пустым, тогда
/// имя очереди будет составлено брокером AMQP. При этом приемопередатчик
/// автоматически назначит очереди флаг "exclusive", означающий, что очередь
/// просуществует только до закрытия соединения с брокером. Если имя очереди
/// задано, то приемопередатчик проверит, существует в брокере такая очередь
/// или нет. Если очередь существует, то она будет открыта, если нет, то вновь
/// создана. В случае возникновения ошибок работы с брокером, приемопередатчик
/// попытается удалить созданную им очередь, а уже существовавшую оставит без
/// изменений.
///
/// При поступлении входящего сообщения будет вызвана функция, назначенная
/// методом onMessage(). Сигнатура функции должна совпадать с MessageCallback.
/// Вся обработка сообщения, включая подтверждение его обработки брокеру,
/// должна выполняться этим обратным вызовом. Если он не задан, вызывается
/// обработчик сообщений по умолчанию OnMessage(). В данном классе метод не
/// делает ничего, в том числе, не подтверждает сообщение. Такое поведение
/// может быть изменено в классах-потомках.
///
/// При завершении работы приемопередатчика вызывается функция, заданная
/// методом onExit(). Ее сигнатура должна совпадать с сигнатурой ExitCallback.
/// Если функция для данного приемопередатчика задана не была, то не делается
/// ничего.
///
/// Автономное использование экземпляров Transceiver возможно, но настоятельно
/// не рекомендуется. Вместо этого используйте средства класса Connector,
/// которые предназначены для манипуляций приемопередатчиками.
///
/// Transceiver нейтрален в отношении ввода/вывода. Все операции производятся
/// исключительно посредством экземпляра класса AMQP::Connection, указатель на
/// который нужно передать при запуске приемопередатчика, см. метод start().
///
/// Transceiver реализован как конечный автомат.
///
/// @startuml
/// [*] -> eCreateChannel
///
/// eCreateChannel --> eCheckQueue : Success (channel ready), listener
/// eCreateChannel --> eCreateExchange : Success, not listener
/// eCreateChannel --> eEnd : Fail
/// eCreateChannel --> eEnd : No AMQP connection
///
/// eCheckQueue --> eCreateExchange : No queue name
/// eCheckQueue --> eCreateExchange : Success (queue exists)
/// eCheckQueue --> eRecreateChannel : Queue not exists
///
/// eRecreateChannel --> eCreateExchange : Success
/// eRecreateChannel --> eEnd : Fail
///
/// eCreateExchange --> eCreateQueue : Success, listener
/// eCreateExchange --> eReady : Success, not listener
/// eCreateExchange --> eCloseChannel : Fail
///
/// eCreateQueue --> eBindQueue : Queue exists
/// eCreateQueue --> eBindQueue : Success, queue created
/// eCreateQueue --> eCloseChannel : Fail
///
/// eBindQueue --> eCreateConsumer : Success
/// eBindQueue --> eRemoveQueue : Fail
///
/// eCreateConsumer --> eReady : Success
/// eCreateConsumer --> eUnbindQueue : Fail
///
/// eReady --> eShutdown : Transceiver::stop()
///
/// eShutdown --> eUnbindQueue : If no consumer tag (stop() before tag received)
/// eShutdown --> eUnbindQueue : Success (consumer cancelled)
/// eShutdown --> eUnbindQueue : Fail
/// eShutdown --> eCloseChannel : Not listener 
///
/// eUnbindQueue --> eCloseChannel : No received queue name (impossible?)
/// eUnbindQueue --> eCloseChannel : Success (channel restored and queue unbind)
/// eUnbindQueue --> eEnd : Channel restored, but unbind failed
/// eUnbindQueue --> eEnd : Channel not restored
///
/// eRemoveQueue --> eCloseChannel : Queue exists, but not created
/// eRemoveQueue --> eCloseChannel : Success (channel restored and queue removed)
/// eRemoveQueue --> eEnd : Channel restored, but queue not removed
/// eRemoveQueue --> eEnd : Channel not restored
///
/// eCloseChannel --> eEnd : Success
/// eCloseChannel --> eEnd : Fail
///
/// eEnd --> [*]
/// @enduml
///
/// После завершения конечного автомата вызовами методов stop() или drop(), он
/// может быть запущен заново вызовом start().
///
/// @author cycleg
///
class Transceiver
{
  public:
    ///
    /// Коды завершения конечного автомата.
    ///
    enum ExitCode
    {
      eNoError, ///< Нормальное завершение.
      eCreateChannelError, ///< Ошибка открытия канала.
      eCreateExchangeError, ///< Ошибка создания очки обмена.
      eCreateQueueError, ///< Ошибка создания очереди.
      eBindQueueError, ///< Ошибка подсоединения очереди.
      eCreateConsumerError, ///< Ошибка регистрации подписчика.
      eConsumerCancelError, ///< Ошибка отмены регистрации подписчика.
      eUnbindQueueError, ///< Ошибка отсоединения очереди.
      eRemoveQueueError, ///< Ошибка удаления очереди.
      eCloseChannelError, ///< Ошибка закрытия канала.
      eDrop ///< Принудительный сброс приемопередатчика.
    };

    ///
    /// Указатель на функцию обратного вызова для обработки возврата брокером
    /// сообщения.
    ///
    typedef std::function<void(
      const AMQP::Message &message,
      int16_t code,
      const std::string &description
    )> BounceCallback;

    ///
    /// Указатель на функцию обратного вызова для обработки входящего сообщения.
    ///
    typedef std::function<void(
      AMQP::Channel* channel,
      const AMQP::Message& message,
      uint64_t deliveryTag,
      bool redelivered
    )> MessageCallback;

    ///
    /// Указатель на функцию обратного вызова при завершении конечного автомата
    /// приемопередатчика.
    ///
    typedef std::function<void(
      const ExitCode& ec
    )> ExitCallback;

    ///
    /// Конструктор.
    ///
    /// @param [in] exchange Имя точки обмена AMQP.
    /// @param [in] queue_ Имя очереди для входящих сообщений AMQP.
    /// @param [in] route_in Маршрут входящих сообщений AMQP.
    /// @param [in] listener Признак, будет ли данный экземпляр использоваться
    ///                      для приема сообщений.
    ///
    Transceiver(const std::string& exchange, const std::string& queue_,
                const std::string& route_in, bool listener);
    ///
    /// Деструктор.
    ///
    virtual ~Transceiver();

    ///
    /// Копирующий конструктор запрещен.
    ///
    Transceiver(const Transceiver&) = delete;

    inline std::string exchange_point() const { return m_exchange; }
    inline std::string route_in() const { return m_route_in; }

    ///
    /// Готовность приемопередатчика к работе.
    ///
    /// @return Готов или нет.
    ///
    inline bool ready() const { return m_state == eReady; }
    ///
    /// Конечный автомат приемопередатчика работает.
    ///
    /// @return Запущен или нет приемопередатчик.
    ///
    /// Запуск осуществялется методом start(). Автомат работает до остановки
    /// методом stop() или до возникновения ошибки. Кроме того автомат может
    /// быть принудительно сброшен в начальное состояние вызовом drop(). Как
    /// правило, сброс производится в случае неработоспособности соединения
    /// с брокером AMQP.
    ///
    inline bool is_running() const { return m_state != eEnd; }
    ///
    /// Извлечь текст с описанием ошибки завершения.
    ///
    /// @return Текст ошибки.
    ///
    /// Если конечный автома данного экземпляра завершился без ошибок,
    /// возвращает пустую строку.
    ///
    inline std::string error() const { return m_error; }

    ///
    /// Назначить обратный вызов для сообщений, которые брокер вернул.
    ///
    /// @param [in] callback Указатель на функцию.
    ///
    void onBounce(BounceCallback callback);
    ///
    /// Назначить обратный вызов для обработки входящих сообщений.
    ///
    /// @param [in] callback Указатель на функцию.
    ///
    void onMessage(MessageCallback callback);
    ///
    /// Назначить обратный вызов для завершения приемопередатчика.
    ///
    /// @param [in] callback Указатель на функцию.
    ///
    void onExit(ExitCallback callback);

    ///
    /// Запустить приемопередатчик.
    ///
    /// @param [in] connection Указатель на класс соединения с брокером AMQP.
    ///
    /// Запуск производится асинхронно. Ход и результаты запуска проверяются
    /// методами is_running() и ready(). Пока процедура продолжается,
    /// is_running() возвращает истину. Если запуск прошел успешно, то оба
    /// метода возвращают истину. Если оба метода возвращают ложь, то запуск
    /// завершился неудачно. При этом запускается соответствующая функция
    /// обратного вызова, если она была задана.
    ///
    /// Запуск возможен, если приемопередатчик не был запущен ранее. В
    /// противном случае метод не делает ничего.
    ///
    void start(AMQP::Connection* connection);
    ///
    /// Остановить приемопередатчик.
    ///
    /// Остановка также производится асинхронно. Признак завершения работы --
    /// метод is_running() возвращает ложь. По завершению запускается
    /// соответствующая функция обратного вызова, если она была задана.
    ///
    /// Если автомат не запущен, метод не делает ничего.
    ///
    void stop();
    ///
    /// Сбросить приемопередатчик.
    ///
    /// Немедленное выключение, при котором не производятся никакие действия:
    /// не отменяется подписка, не отсоединяется очередь и т.д. Как правило,
    /// метод используется в том случае, если заданное при запуске
    /// приемопередатчика соединение с брокером AMQP неработоспособно.
    ///
    void drop();

    ///
    /// Опубликовать сообщение в формате JSON.
    ///
    /// @param [in] message Сообщение для отправки.
    /// @param [in] route Маршрут публикуемого сообщения.
    /// @param [in] mandatory Делать обратный вызов, если для сообщения нет
    ///                       получателей (необязательный).
    /// @return Успешно или нет отправлено сообщение.
    ///
    /// Успех здесь означает, что приемопередатчик инициировал передачу
    /// сообщения брокеру средствами AMQP-CPP.
    ///
    /// Сообщение публикуется с заголовками "content type", равным
    /// "application/json", и "content encoding", равным "utf-8".
    /// 
    /// По умолчанию mandatory равен истине, т.е. если функция обратного
    /// вызова определена, то она будет запущена.
    ///
    bool send(const Json::Value& message, const std::string& route,
              bool mandatory = true);
    ///
    /// Опубликовать текстовое сообщение.
    ///
    /// @param [in] message Сообщение для отправки.
    /// @param [in] route Маршрут публикуемого сообщения.
    /// @param [in] mandatory Делать обратный вызов, если для сообщения нет
    ///                       получателей (необязательный).
    /// @return Успешно или нет отправлено сообщение.
    ///
    /// Успех здесь означает, что приемопередатчик инициировал передачу
    /// сообщения брокеру средствами AMQP-CPP.
    ///
    /// Сообщение публикуется с заголовками "content type", равным
    /// "text/plain", и "content encoding", равным "utf-8".
    ///
    /// По умолчанию mandatory равен истине, т.е. если функция обратного
    /// вызова определена, то она будет запущена.
    ///
    bool send(const std::string& message, const std::string& route,
              bool mandatory = true);

  protected:
    ///
    /// Обработчик входящих сообщений по умолчанию.
    ///
    /// @param [in] channel Указатель на объект соединение с брокером AMQP.
    /// @param [in] message Входящее сообщение.
    /// @param [in] deliveryTag Метка сообщения.
    /// @param [in] redelivered
    ///
    /// Обработчик по умолчанию не делает ничего, в том числе, не подтверждает
    /// сообщение.
    ///
    virtual void OnMessage(AMQP::Channel* channel,
                           const AMQP::Message& message, uint64_t deliveryTag,
                           bool redelivered);

  private:
    ///
    /// Состояния конечного автомата.
    ///
    enum State
    {
      eCreateChannel, ///< Заводится канал к брокеру AMQP.
      eCheckQueue, ///< Проверяется наличие в брокере очереди для входящих
                   ///< сообщений.
      eRecreateChannel, ///< Канал к брокеру AMQP заводится заново.
      eCreateExchange, ///< Создается или открывается точка обмена.
      eCreateQueue, ///< Создается или открывается очередь для входящих
                    ///< сообщений.
      eBindQueue, ///< Очередь для входящих сообщений соединяется с точкой
                  ///< обмена.
      eCreateConsumer, ///< В брокере заказана подписка на входящие сообщения.
      eReady, ///< Готовность к приему/отправке сообщений.
      eShutdown, ///< Начинается процедура остановки автомата.
      eUnbindQueue, ///< Отменяется соединение очереди с точкой обмена.
      eRemoveQueue, ///< Удаляется ранее созданная очередь.
      // Exchange point, если к нему нет других очередей, всегда удаляется
      // автоматически при закрытии канала, поэтому отдельного шага для его
      // удаления не делаем.
      eCloseChannel, ///< Закрывается канал AMQP.
      eEnd, ///< Автомат завершился, это же начальное состояние.
      eMax
    };
    static const int ExchangeCreationFlags; ///< Флаги, с которыми создается
                                            ///< (открывается) точка обмена.

    friend inline std::ostream& operator<<(std::ostream& out, const State& s)
    {
      static const char* str[eMax + 1] = {
        "eCreateChannel",
        "eCheckQueue",
        "eRecreateChannel",
        "eCreateExchange",
        "eCreateQueue",
        "eBindQueue",
        "eCreateConsumer",
        "eReady",
        "eShutdown",
        "eUnbindQueue",
        "eRemoveQueue",
        "eCloseChannel",
        "eEnd",
        "eMax"
      };
      out << str[s];
      return out;
    }

    ///
    /// Конечный автомат приемопередатчика.
    ///
    void StateMachine();

    State m_state; ///< Текущее состояние конечного автомата.
    AMQP::Connection* m_connection; ///< Соединение с брокером AMQP.
    std::string m_exchange, ///< Точка обмена.
                m_queue, ///< Имя очереди для входящих сообщений.
                m_recvQueue, ///< Имя очереди, полученное в процессе ее
                             ///< создания/открытия.
                m_route_in, ///< Маршрут входящих сообщений.
                m_consumerTag; ///< Тэг подписчика в брокере AMQP.
    bool m_listener; ///< Признак, работает ли экземпляр на прием.
    bool m_queueExist; ///< Очередь с таким именем в брокере есть.
    int m_qFlags; ///< Флаги создания очереди в брокере.
    BounceCallback m_onBounceMessage; ///< Указатель на функцию, вызываемую
                                      ///< при возврате брокером сообщения.
    MessageCallback m_onMessage; ///< Указатель на функцию, вызываемую
                                 ///< при появлении входящего сообщения.
    ExitCallback m_onExit; ///< Указатель на функцию, вызываемую при завершении
                           ///< основного цикла приемопередатчика.
    std::shared_ptr<AMQP::Channel> m_channel; ///< Канал связи с брокером AMQP.
    std::string m_error; ///< Текст последней ошибки.
    ExitCode m_ec; ///< Код ошибки, с которым завершился автомат.
};

} // namespace amqp
