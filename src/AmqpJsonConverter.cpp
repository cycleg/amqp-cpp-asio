#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "AmqpJsonConverter.hpp"

namespace amqp {

void ConvertToJson(const AMQP::Envelope& message, rapidjson::Document& json)
{
  json.RemoveAllMembers();
  if (message.contentType() == "application/json")
  {
    if (json.Parse(message.body(), message.bodySize()).HasParseError())
      json.RemoveAllMembers();
  }
}

std::shared_ptr<AMQP::Envelope> ConvertFromJson(const rapidjson::Document& json,
                                                std::string& buffer)
{
  StringBuffer buff;
  Writer<StringBuffer> writer(buff);
  json.Accept(writer);
  buffer.assign(buff.GetString(), buff.GetSize());
  std::shared_ptr<AMQP::Envelope> envelope(new AMQP::Envelope(buffer.data(),
                                                              buffer.size()));
  envelope->setContentType("application/json");
  envelope->setContentEncoding("utf-8");
  return envelope;
}

} // namespace amqp
