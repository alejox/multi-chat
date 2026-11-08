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
		// 10: fila de chat de Starvios con los campos de la API REST, con datos inventados.
		R"js({"id":"m1","user_id":"u1","body":"hola :abc_d: y https://x.io/:abc_d: fin :nada:","coins":0,"kind":"chat","meta":{},
			"badges":["vip","donor","moderator","member","owner","staff","partner"],"pinned_until":null,"is_deleted":false,
			"created_at":"2026-01-02T03:04:05.123456+00:00","profiles":{"username":"ana_test","display_name":"Ana Test",
			"chat_color":"#12ab9F","is_partner":false,"is_verified":false,"is_team":false}})js",
		// 11: respuesta de /api/public/discovery con datos inventados.
		R"js({"channel":{"id":"c1","username":"ana_test","display_name":"Ana Test","avatar_url":"c1/foto.png"},
			"stream":{"id":"s1","status":"live","channel_id":"c1","started_at":"2026-01-02T03:04:05+00:00"}})js",
		// 12: catálogo de /api/public/emotes con datos inventados, incluidos valores que no se aceptan.
		R"js({"rows":[{"id":"e1","channel_id":"c1","code":"Abc_D","storage_path":"c1/abc d.png","audience":"follower","is_animated":false},
			{"id":"e2","code":"bad code","storage_path":"c1/x.png"},{"id":"e3","code":"salir","storage_path":"../secreto.png"},
			{"id":"e4","code":"externo","storage_path":"https://evil.example/x.png"},{"id":"e5","code":"nulo"}]})js",
		// 13: fragmento del HTML de la portada con los módulos de la página.
		R"js(<link rel="modulepreload" href="/assets/index-AAA.js"/><script type="module" src="/assets/client-D8x_9-q.js" async></script>)js",
		// 14: fragmento del bundle con las constantes de Supabase. La clave es inventada.
		R"js(TSS_SERVER_FN_BASE:`/_serverFn/`,VITE_SUPABASE_PROJECT_ID:`abc`,VITE_SUPABASE_PUBLISHABLE_KEY:`sb_publishable_Nueva-Clave_123`,VITE_SUPABASE_URL:`https://abcdefgh.supabase.co`})js",
		// 15: Kick chat message with a numeric sender id (made-up data).
		R"js({"id":"k-msg-1","content":"hola","sender":{"id":777,"username":"Pepe","slug":"pepe","identity":{"badges":[]}}})js",
		// 16: Kick moderation events from chatrooms.<id>.v2 (made-up data): MessageDeletedEvent,
		// UserBannedEvent (with "data" as a JSON string, as Pusher sends it) and ChatroomClearEvent.
		R"js({"event":"App\\Events\\MessageDeletedEvent","data":{"id":"evt-1","message":{"id":"k-msg-1"},"aiModerated":false}})js",
		R"js({"event":"App\\Events\\UserBannedEvent","data":"{\"id\":\"evt-2\",\"user\":{\"id\":777,\"username\":\"Pepe\",\"slug\":\"pepe\"},\"banned_by\":{\"id\":1,\"username\":\"mod\"},\"permanent\":false,\"duration\":5}"})js",
		// 18: YouTube text message with its author channel id (made-up data).
		R"js({"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"yt-msg-1","authorName":{"simpleText":"Ana"},
			"authorExternalChannelId":"UCabc","message":{"runs":[{"text":"hola"}]}}}}})js",
		// 19: YouTube moderation actions (made-up data).
		R"js([{"markChatItemAsDeletedAction":{"deletedStateMessage":{"runs":[{"text":"[mensaje eliminado]"}]},"targetItemId":"yt-msg-1"}},
			{"markChatItemsByAuthorAsDeletedAction":{"deletedStateMessage":{"runs":[]},"externalChannelId":"UCabc"}},
			{"removeChatItemAction":{"targetItemId":"yt-msg-1"}},
			{"removeChatItemByAuthorAction":{"externalChannelId":"UCabc"}},
			{"addChatItemAction":{"item":{}}}])js",
	};
	return fixtures[index];
}
