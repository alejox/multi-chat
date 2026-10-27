// Datos de prueba de test-chat.cpp.

const char *fixture(int index)
{
	static const char *const fixtures[] = {
		R"js({"id":"x1","content":"hi","sender":{"username":"pepe",
			"identity":{"color":"#FF0000","badges":[{"type":"moderator"},{"type":"og"}]}}})js",
		R"js({"addChatItemAction":{"item":{"liveChatPaidMessageRenderer":{
			"id":"yt1","authorName":{"simpleText":"Ana"},"purchaseAmountText":{"simpleText":"$5.00"},
			"authorBadges":[{"liveChatAuthorBadgeRenderer":{"icon":{"iconType":"MODERATOR"}}}],
			"message":{"runs":[{"text":"hola "},{"emoji":{"emojiId":"😀","shortcuts":[":grin:"]}},
			{"emoji":{"emojiId":"UC/x","isCustomEmoji":true,"shortcuts":[":yt:"],
			 "image":{"thumbnails":[{"url":"https://yt/e.png"}]}}}]}}}}})js",
		R"js({"continuations":[{"timedContinuationData":{"continuation":"abc","timeoutMs":4000}}]})js",
		R"js(<link rel="canonical" href="https://www.youtube.com/watch?v=dQw4w9WgXcQ"> "isLiveNow":true)js",
		R"js(<link rel="canonical" href="https://www.youtube.com/@x">)js",
	};
	return fixtures[index];
}
