#include <functional>
#include <iostream>
#include <memory>
#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>
#include <rapidjson/prettywriter.h>
#include <rapidjson/ostreamwrapper.h>
#include "../src/AutoReconnect.hpp"
#include "../src/AmqpJsonConverter.hpp"

#define UNUSED(x) (void)x;

const char* amqpUrl = "amqp://127.0.0.1:5672/";
const char* amqpExchange = "test.client";
const char* amqpRoute = "incoming";

/*
Send message with rabbitmq CLI:

rabbitmqadmin publish routing_key=incoming exchange=test.client \
  'properties={"content_type":"application/json"}' \
  'payload=<JSON>'
*/

void onAmqpMessage(AMQP::Channel* channel, const AMQP::Message& message,
                   uint64_t deliveryTag, bool redelivered)
{
  UNUSED(redelivered)

  channel->ack(deliveryTag);
  rapidjson::Document msg;
  amqp::ConvertToJson(message, msg);
  rapidjson::OStreamWrapper osw(std::cout);
  rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
  writer.SetIndent(' ', 2);
  std::cout << "AMQP raw message content:" << std::endl
            << std::string(message.body(), message.bodySize()) << std::endl
            << "JSON:" << std::endl;
  msg.Accept(writer);
  std::cout << std::endl;
}

int main()
{
  boost::asio::io_service io_service;
  std::shared_ptr<amqp::AutoReconnect> m_amqpClient = amqp::AutoReconnect::Factory(
    std::make_shared< amqp::Connector<> >(io_service, amqpUrl)
  );
  boost::asio::signal_set stopSignals(io_service, SIGINT, SIGTERM, SIGQUIT);
  stopSignals.async_wait([m_amqpClient](const boost::system::error_code& error, int signal) {
    // ignore signal handling cacellation
    if (error == boost::asio::error::operation_aborted) return;
    std::cout << "Signal " << signal << " received" << std::endl;
    m_amqpClient->stop();
  });
  auto trn = *(m_amqpClient->connector()->transceiver(amqpExchange, "",
                                                      amqpRoute, true));
  trn->onMessage(std::bind(&onAmqpMessage, std::placeholders::_1,
                           std::placeholders::_2, std::placeholders::_3,
                           std::placeholders::_4));
  m_amqpClient->connector()->onExit([](amqp::Connector<>::ExitCode code) {
    std::cout << "AMQP client finished with code " << code << std::endl;
  });
  auto conn = m_amqpClient->connector();
  m_amqpClient->start([conn]() {
    conn->run();
    std::cout << "AMQP client started" << std::endl;
  });
  io_service.run();
  return EXIT_SUCCESS;
}
