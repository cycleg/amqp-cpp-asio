#pragma once

#include <memory>
#include <string>
#include <amqpcpp.h>
#include <rapidjson/document.h>

namespace amqp {

///
/// Извлекает из входящего AMQP-сообщения JSON.
///
/// @param [in] message Сообщение AMQP.
/// @param [out] json Результат преобразования содержимого сообщения в JSON.
///
/// Сообщение преобразуется, только если его заголовок "Content Type" равен
/// "application/json". Иначе, или если преобразование завершилось неудачно,
/// возвращается пустой объект.
///
/// @author cycleg
///
void ConvertToJson(const AMQP::Envelope& message, rapidjson::Document& json);

///
/// Помещает JSON в "конверт" для публикации средствами AMQP.
///
/// @param [in] json Объект для публикации.
/// @param [in,out] buffer Буфер для содержимого сообщения.
/// @return Созданный "конверт".
/// 
/// Класс AMQP::Envelope не управляет памятью, выделенной под содержимое
/// публикации, поэтому до завершения отправки необходим внешний буфер.
///
/// @author cycleg
///
std::shared_ptr<AMQP::Envelope> ConvertFromJson(const rapidjson::Document& json,
                                                std::string& buffer);

} // namespace amqp
