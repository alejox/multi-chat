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
		R"js("viewCount":{"videoViewCountRenderer":{"viewCount":{"runs":[{"text":"1.234"},{"text":" watching now"}]},"isLive":true,"originalViewCount":"1234"}})js",
		R"js("videoViewCountRenderer":{"viewCount":{"runs":[{"text":"1,234"},{"text":" personas viendo"}]},"isLive":true})js",
		R"js("videoViewCountRenderer":{"viewCount":{"simpleText":"98.765 visualizaciones"},"originalViewCount":"98765"})js",
		R"js(<meta property="og:title" content="Alejo &amp; Tech"><meta property="og:image" content="https://yt3.googleusercontent.com/abc123=s900-c-k-c0x00ffffff-no-rj">)js",
		R"js("videoOwnerRenderer":{"thumbnail":{"thumbnails":[{"url":"https://yt3.ggpht.com/xyz=s48-c-k-c0x00ffffff-no-rj","width":48}]},"title":{"runs":[{"text":"Canal"}]}} "ownerChannelName":"Mi Canal")js",
	};
	return fixtures[index];
}
