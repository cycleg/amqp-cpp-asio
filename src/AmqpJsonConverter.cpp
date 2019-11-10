#ifndef NDEBUG
#include <iostream>
#endif
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/error/en.h>
#include "AmqpJsonConverter.hpp"

namespace amqp {

void ConvertToJson(const AMQP::Envelope& message, rapidjson::Document& json)
{
#ifndef NDEBUG
  std::clog << "ConvertToJson(): content type " << message.contentType()
            << std::endl;
#endif
  if (message.contentType() == "application/json")
  {
    if (json.Parse(message.body(), message.bodySize()).HasParseError())
    {
#ifndef NDEBUG
      std::clog << "ConvertToJson(): "
                << GetParseError_En(json.GetParseError()) << std::endl;
#endif
      json = rapidjson::Document(rapidjson::kNullType);
    }
  }
}

std::shared_ptr<AMQP::Envelope> ConvertFromJson(const rapidjson::Document& json,
                                                std::string& buffer)
{
  rapidjson::StringBuffer buff;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buff);
  json.Accept(writer);
  buffer.assign(buff.GetString(), buff.GetSize());
  std::shared_ptr<AMQP::Envelope> envelope(new AMQP::Envelope(buffer.data(),
                                                              buffer.size()));
  envelope->setContentType("application/json");
  envelope->setContentEncoding("utf-8");
  return envelope;
}

} // namespace amqp
