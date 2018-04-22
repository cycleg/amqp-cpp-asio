#include "AmqpJsonConverter.hpp"

namespace amqp {

Json::Value ConvertToJson(const AMQP::Envelope& message)
{
  Json::Value value;
  if (message.contentType() == "application/json")
  {
    Json::Reader reader;
    reader.parse(std::string(message.body(), message.bodySize()), value);
  }
  return value;
}

std::shared_ptr< AMQP::Envelope > ConvertFromJson(const Json::Value& json,
                                                  std::string& buffer)
{
  Json::FastWriter writer;
  buffer = writer.write(json);
  std::shared_ptr<AMQP::Envelope> envelope(new AMQP::Envelope(buffer.data(),
                                                              buffer.size()));
  envelope->setContentType("application/json");
  envelope->setContentEncoding("utf-8");
  return envelope;
}

} // namespace amqp
