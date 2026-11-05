// Starvios: chat público por Supabase Realtime, igual que su web. Solo lectura y anónimo.

#include "platforms.hpp"

#include <QJsonDocument>

namespace chat {
namespace starvios {

namespace {

// Formato de las difusiones binarias según el cliente de Realtime de la página: 1 byte de tipo,
// 3 con las longitudes de topic, evento y metadatos, 1 de codificación y después esos campos.
constexpr char kUserBroadcast = 4;
constexpr char kJsonEncoding = 1;
constexpr qsizetype kBroadcastHeader = 5;

Frame decodeControl(QByteArrayView data)
{
	Frame frame;
	const QJsonDocument doc = QJsonDocument::fromJson(data.toByteArray());
	const QJsonArray fields = doc.array();
	if (fields.size() != 5 || !fields[2].isString() || !fields[3].isString())
		return frame;
	frame.type = Frame::Type::Control;
	frame.ref = fields[1].toVariant().toString();
	frame.topic = fields[2].toString();
	frame.event = fields[3].toString();
	frame.payload = fields[4].toObject();
	return frame;
}

Frame decodeBroadcast(QByteArrayView data)
{
	Frame frame;
	if (data.size() < kBroadcastHeader)
		return frame;
	const auto length = [&](qsizetype index) { return qsizetype(static_cast<unsigned char>(data[index])); };
	const qsizetype topicSize = length(1);
	const qsizetype eventSize = length(2);
	const qsizetype metaSize = length(3);
	const qsizetype payloadStart = kBroadcastHeader + topicSize + eventSize + metaSize;
	if (data.size() <= payloadStart)
		return frame;

	frame.topic = QString::fromUtf8(data.sliced(kBroadcastHeader, topicSize));
	frame.event = QString::fromUtf8(data.sliced(kBroadcastHeader + topicSize, eventSize));
	if (data[4] != kJsonEncoding) {
		frame.type = Frame::Type::Ignored;
		return frame;
	}
	const QJsonDocument doc = QJsonDocument::fromJson(data.sliced(payloadStart).toByteArray());
	if (!doc.isObject())
		return frame;
	frame.type = Frame::Type::Broadcast;
	frame.payload = doc.object();
	return frame;
}

} // namespace

Frame decodeFrame(QByteArrayView data)
{
	if (data.isEmpty())
		return {};
	// Un JSON de Phoenix siempre empieza por "["; una difusión binaria, por su tipo (byte pequeño).
	if (data[0] == '[')
		return decodeControl(data);
	if (data[0] == kUserBroadcast)
		return decodeBroadcast(data);
	if (static_cast<unsigned char>(data[0]) < 0x20) {
		Frame other;
		other.type = Frame::Type::Ignored; // otros tipos binarios del protocolo
		return other;
	}
	return {};
}

} // namespace starvios
} // namespace chat
