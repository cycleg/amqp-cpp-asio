#include <iostream>
#include <memory>
#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>
#include "../src/AutoReconnect.hpp"

const char* amqpUrl = "amqp://127.0.0.1:5672/";
const char* amqpExchange = "test.client";
const char* amqpRoute = "incoming";

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cout << "Usage: " << argv[0] << " <content>" << std::endl;
    return EXIT_FAILURE;
  }
  std::string content(argv[1]);
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
                                                      "", false));
  m_amqpClient->connector()->onExit([](amqp::Connector<>::ExitCode code) {
    std::cout << "AMQP client finished with code " << code << std::endl;
  });
  m_amqpClient->start([m_amqpClient, trn, &content, &stopSignals]() {
    m_amqpClient->connector()->run();
    std::cout << "AMQP client started" << std::endl;
    if (trn->send(content, amqpRoute, false))
      std::cout << "Raw message send." << std::endl;
      else std::cout << "Message not send." << std::endl;
    rapidjson::Document msg;
    if (!msg.Parse(content.c_str()).HasParseError())
      {
        if (trn->send(msg, amqpRoute, false))
          std::cout << "JSON send." << std::endl;
          else std::cout << "JSON not send." << std::endl;
      }
      else std::cout << "Content isn't JSON." << std::endl;
    m_amqpClient->stop();
    stopSignals.cancel();
  });
  io_service.run();
  return EXIT_SUCCESS;
}
