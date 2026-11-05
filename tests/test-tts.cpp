// Pruebas de la lectura del chat en voz alta (sin OBS ni sintetizador de voz).

#include "../src/chat/message.hpp"
#include "../src/tts/bounded-queue.hpp"
#include "../src/tts/speech-text.hpp"

#include <QtTest/QtTest>

using namespace tts;

namespace {

SpeechOptions baseOptions()
{
	SpeechOptions o;
	o.includeAuthor = false;
	o.authorFormat = QStringLiteral("%1 dice: %2");
	o.allowedPlatforms = {"twitch", "youtube", "kick", "tiktok"};
	o.maxLength = 0;
	return o;
}

QJsonObject message(const QString &text, const QString &author = QStringLiteral("Ana"),
		    const QString &platform = QStringLiteral("twitch"), const chat::MessageOptions &options = {})
{
	QJsonArray parts;
	if (!text.isEmpty())
		parts.append(chat::textPart(text));
	return chat::makeMessage(platform, "canal", author, parts, options);
}

} // namespace

class TtsTest : public QObject {
	Q_OBJECT

private slots:
	void plainTextIsSpoken() { QCOMPARE(speechTextForMessage(message("hola a todos"), baseOptions()), QString("hola a todos")); }

	void authorPrefixIsOptional()
	{
		SpeechOptions o = baseOptions();
		QCOMPARE(speechTextForMessage(message("hola"), o), QString("hola"));
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("hola"), o), QString("Ana dice: hola"));
	}

	// Un autor o un texto con "%2" o "%1" no se puede colar en el formato.
	void formatMarkersInUserTextAreLiteral()
	{
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("el 50%1 gana", "Eva%2"), o), QString("Eva%2 dice: el 50%1 gana"));
	}

	void emoteAndTextPartsAreJoined()
	{
		QJsonArray parts;
		parts.append(chat::textPart("hola"));
		parts.append(chat::emotePart("https://x/y.png", "Kappa"));
		parts.append(chat::textPart("mundo"));
		QCOMPARE(speechTextForMessage(chat::makeMessage("twitch", "c", "Ana", parts), baseOptions()), QString("hola mundo"));
	}

	void onlyEmotesIsNotSpoken()
	{
		QJsonArray parts;
		parts.append(chat::emotePart("https://x/y.png", "Kappa"));
		QVERIFY(speechTextForMessage(chat::makeMessage("twitch", "c", "Ana", parts), baseOptions()).isEmpty());
	}

	void linksAreRemoved()
	{
		QCOMPARE(cleanSpeechText("mira https://a.com/x?y=1 y HTTP://b.org ya www.c.net/z fin"), QString("mira y ya fin"));
		QCOMPARE(cleanSpeechText("awww.que lindo"), QString("awww.que lindo"));
		QVERIFY(speechTextForMessage(message("https://a.com"), baseOptions()).isEmpty());
	}

	void emojisAreRemovedButAccentsStay()
	{
		QCOMPARE(cleanSpeechText(QString::fromUtf8("hola😀mundo")), QString("hola mundo"));
		QCOMPARE(cleanSpeechText(QString::fromUtf8("🔥 ¡qué señal, niño! ❤️ ⭐")), QString::fromUtf8("¡qué señal, niño!"));
		QCOMPARE(cleanSpeechText(QString::fromUtf8("👨‍👩‍👧 familia 🇨🇴")), QString("familia"));
		QCOMPARE(cleanSpeechText(QString::fromUtf8("日本語 Привет")), QString::fromUtf8("日本語 Привет"));
		QVERIFY(speechTextForMessage(message(QString::fromUtf8("😀😀 🔥")), baseOptions()).isEmpty());
	}

	void whitespaceIsCollapsed() { QCOMPARE(cleanSpeechText("  a \n\t b   c  "), QString("a b c")); }

	void withoutLettersOrDigitsIsNotSpoken()
	{
		QVERIFY(speechTextForMessage(message("?!... ---"), baseOptions()).isEmpty());
		QVERIFY(speechTextForMessage(message("   "), baseOptions()).isEmpty());
		QCOMPARE(speechTextForMessage(message("7"), baseOptions()), QString("7"));
		// El autor por sí solo tampoco es motivo para hablar.
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QVERIFY(speechTextForMessage(message("!!!"), o).isEmpty());
	}

	void longTextIsCutAtAWordBoundary()
	{
		SpeechOptions o = baseOptions();
		o.maxLength = 10;
		QCOMPARE(speechTextForMessage(message("uno dos tres cuatro"), o), QString("uno dos"));
		QCOMPARE(speechTextForMessage(message("uno dos tr"), o), QString("uno dos tr"));
		// El espacio justo en el límite también es un corte limpio.
		QCOMPARE(speechTextForMessage(message("uno dos tr es"), o), QString("uno dos tr"));
		// Una sola palabra enorme se corta donde toque.
		QCOMPARE(speechTextForMessage(message("abcdefghijklmnop"), o), QString("abcdefghij"));
	}

	void cutNeverSplitsASurrogatePair()
	{
		SpeechOptions o = baseOptions();
		o.maxLength = 4;
		const QString cut = speechTextForMessage(message(QString::fromUtf8("abc𝒳𝒳𝒳")), o);
		QCOMPARE(cut, QString("abc"));
	}

	void authorDoesNotCountTowardsMaxLength()
	{
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		o.maxLength = 4;
		QCOMPARE(speechTextForMessage(message("hola que tal"), o), QString("Ana dice: hola"));
	}

	void blockedWordsRejectTheWholeMessage()
	{
		SpeechOptions o = baseOptions();
		o.blockedWords = {"Tonto", "muy feo"};
		QVERIFY(speechTextForMessage(message("eres TONTO, de verdad"), o).isEmpty());
		QVERIFY(speechTextForMessage(message("es muy  feo eso"), o).isEmpty());
		// Palabra completa: que aparezca dentro de otra no cuenta.
		QCOMPARE(speechTextForMessage(message("tontos atontado"), o), QString("tontos atontado"));
		// Mayúsculas acentuadas y letras que no son ASCII como frontera de palabra.
		o.blockedWords = {"canción"};
		QVERIFY(speechTextForMessage(message("otra CANCIÓN"), o).isEmpty());
		o.blockedWords = {"can"};
		QCOMPARE(speechTextForMessage(message("cañcan"), o), QString::fromUtf8("cañcan"));
	}

	void blockedWordsApplyBeyondTheCutAndToAuthor()
	{
		SpeechOptions o = baseOptions();
		o.blockedWords = {"secreto"};
		o.maxLength = 5;
		QVERIFY(speechTextForMessage(message("hola que tal secreto"), o).isEmpty());
		o.maxLength = 0;
		// El nombre solo importa si se va a leer.
		QCOMPARE(speechTextForMessage(message("hola", "secreto"), o), QString("hola"));
		o.includeAuthor = true;
		QVERIFY(speechTextForMessage(message("hola", "secreto"), o).isEmpty());
	}

	void blockedWordsWithRegexCharactersAreLiteral()
	{
		SpeechOptions o = baseOptions();
		o.blockedWords = {"a.b", "c++"};
		QCOMPARE(speechTextForMessage(message("axb"), o), QString("axb"));
		QVERIFY(speechTextForMessage(message("uso c++ siempre"), o).isEmpty());
	}

	void parseBlockedWordsSplitsAndTrims()
	{
		QCOMPARE(parseBlockedWords(" uno, dos tres\n\ncuatro ,, "), QStringList({"uno", "dos tres", "cuatro"}));
		QVERIFY(parseBlockedWords("  ").isEmpty());
	}

	void platformFilter()
	{
		SpeechOptions o = baseOptions();
		o.allowedPlatforms = {"kick"};
		QVERIFY(speechTextForMessage(message("hola", "Ana", "twitch"), o).isEmpty());
		QCOMPARE(speechTextForMessage(message("hola", "Ana", "kick"), o), QString("hola"));
		o.allowedPlatforms.clear();
		QVERIFY(speechTextForMessage(message("hola", "Ana", "kick"), o).isEmpty());
	}

	void actionReadsAuthorAsSubject()
	{
		chat::MessageOptions action;
		action.action = true;
		SpeechOptions o = baseOptions();
		QCOMPARE(speechTextForMessage(message("saluda", "Ana", "twitch", action), o), QString("saluda"));
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("saluda", "Ana", "twitch", action), o), QString("Ana saluda"));
	}

	void systemTextIsSpokenWithoutDoublingTheAuthor()
	{
		chat::MessageOptions sys;
		sys.kind = "highlight";
		sys.system = "Ana se suscribió por 6 meses";
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("", "Ana", "twitch", sys), o), QString("Ana se suscribió por 6 meses"));
		QCOMPARE(speechTextForMessage(message("gracias", "Ana", "twitch", sys), o),
			 QString("Ana se suscribió por 6 meses. gracias"));

		sys.system = "Miembro hace 6 meses!";
		QCOMPARE(speechTextForMessage(message("gracias", "Ana", "youtube", sys), o),
			 QString("Ana, Miembro hace 6 meses! gracias"));
		o.includeAuthor = false;
		QCOMPARE(speechTextForMessage(message("", "Ana", "youtube", sys), o), QString("Miembro hace 6 meses!"));
	}

	void systemTextIsFilteredToo()
	{
		chat::MessageOptions sys;
		sys.system = "Ana envió secreto x3";
		SpeechOptions o = baseOptions();
		o.blockedWords = {"secreto"};
		QVERIFY(speechTextForMessage(message("", "Ana", "tiktok", sys), o).isEmpty());
	}

	void authorWithOnlyEmojisHasNoPrefix()
	{
		SpeechOptions o = baseOptions();
		o.includeAuthor = true;
		QCOMPARE(speechTextForMessage(message("hola", QString::fromUtf8("😀😀")), o), QString("hola"));
		QCOMPARE(speechTextForMessage(message("hola", QString::fromUtf8("Ana 😀")), o), QString("Ana dice: hola"));
	}

	void queueIsFifo()
	{
		BoundedQueue<int> q(3);
		QVERIFY(q.empty());
		QCOMPARE(q.push(1), std::size_t(0));
		QCOMPARE(q.push(2), std::size_t(0));
		int v = 0;
		QVERIFY(q.pop(v));
		QCOMPARE(v, 1);
		QVERIFY(q.pop(v));
		QCOMPARE(v, 2);
		QVERIFY(!q.pop(v));
	}

	void queueDropsTheOldestWhenFull()
	{
		BoundedQueue<int> q(2);
		q.push(1);
		q.push(2);
		QCOMPARE(q.push(3), std::size_t(1));
		QCOMPARE(q.push(4), std::size_t(1));
		QCOMPARE(q.droppedTotal(), std::size_t(2));
		QCOMPARE(q.size(), std::size_t(2));
		int v = 0;
		QVERIFY(q.pop(v));
		QCOMPARE(v, 3);
	}

	void queueCapacityChangesAndClear()
	{
		BoundedQueue<int> q(4);
		for (int i = 0; i < 4; ++i)
			q.push(i);
		QCOMPARE(q.setCapacity(1), std::size_t(3));
		int v = 0;
		QVERIFY(q.pop(v));
		QCOMPARE(v, 3);
		q.setCapacity(0); // un límite inválido se queda en uno
		QCOMPARE(q.capacity(), std::size_t(1));
		q.push(1);
		q.clear();
		QVERIFY(q.empty());
	}
};

QTEST_GUILESS_MAIN(TtsTest)
#include "test-tts.moc"
