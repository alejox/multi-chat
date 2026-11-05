# Feature: starvios-chat

## Objective
Add Starvios as a chat platform: read live chat anonymously and show viewer counts.

## Problem and why
The user streams or follows channels on Starvios, which the plugin does not support. Starvios has no documented API, so the connector relies on the same interfaces its public web page uses.

## Evidence (inspection on 2026-11-05 against `eddi24k`, anonymous)
- `GET https://starvios.com/api/public/discovery?kind=channel&username=<name>` returns `channel.id`, `stream.id`, `stream.status`, `started_at`. Stream ID changes per broadcast.
- Supabase Realtime: `wss://naiijcvngtkkocligwvk.supabase.co/realtime/v1/websocket?apikey=<publishable>&vsn=2.0.0`, topic `realtime:stream:<stream.id>`, public broadcast, join accepted anonymously, Phoenix heartbeats answered.
- User broadcasts arrive as binary frames. From the page bundle only: byte 0 kind (4), bytes 1-4 topic, event and metadata lengths plus encoding (1 = JSON), then those fields and the payload. Event `msg`, payload `{op, msg}`.
- `GET /rest/v1/live_chat_messages` with only the `apikey` header returned history rows.
- `POST /rest/v1/rpc/live_viewer_counts` in the browser returned labeled `viewers`.
- Not determined: the exact live `msg` payload, including whether it holds the author profile.

## Scope
- Connector, entry parsing, settings hint, viewer count, voice platform checkbox, tests, README.

## Out of scope
- Writing to chat or any login. Voice quality changes.

## Constraints
- Read only and anonymous. Never send chat or use the user's session.
- History is never shown or spoken on connect. Reconnect recovery may show messages but must not read them aloud.

## Delivery
- Forecast: about 700 authored changed lines.
- Strategy: `single-pr` on `feat/tts-chat-reader` together with the voice feature, by explicit user decision. Push and PR remain user decisions.
- RDD: off (decided by global).

## Tasks
- [x] S1 Binary WebSocket support and Realtime frame decoder with tests. Route: delegated (writer trigger, 2+ non-trivial files).
  - Accept: binary frames reach connectors on macOS, Windows and Linux network backends; decoder handles valid and malformed frames.
  - Check: test suite and `cmake --build --preset macos`.
- [ ] S2 Starvios connector and platform registration with tests. Route: delegated.
  - Accept: `https://starvios.com/live/<user>` and bare usernames are accepted; offline and new broadcasts are handled through discovery; heartbeats sent; author, text, color, badges and Starvies mapped; missing profile handled; viewers polled; recovered messages are flagged and not spoken.
  - Check: test suite, build, and a bounded read-only live connection.
- [ ] S3 UI hints, voice checkbox, locale strings and README. Route: delegated.
  - Accept: Starvios appears in settings and the voice properties in both locales; README lists its unofficial status.
  - Check: every new key resolved in both locale files; build.

## Progress
- S1 done. `src/net/` did not change: all three backends already deliver binary messages unmodified through `net::WebSocket::receive` (`net-mac.mm` copies `msg.data`, `net-win.cpp` and `net-linux.cpp` append the payload for both message types), but none of them tells text from binary, so `decodeFrame` decides by content.

## Verification evidence
- S1 RED: with `decodeFrame` as a stub, `ctest` failed `starviosBroadcastFrame` and `starviosControlFrame` (14 passed, 2 failed). `starviosMalformedFrames` passed against the stub, because a stub rejects everything.
- S1 GREEN: `ctest --test-dir build_tests`: 100% tests passed out of 2, against the QtTest stand-in because `.deps` Qt has no QtTest.
- S1 build: `cmake --preset macos && cmake --build --preset macos`: `** BUILD SUCCEEDED **`, plugin has x86_64 and arm64.
- Frame fixtures are built from `_decodeUserBroadcast` in the page bundle; they are not captured frames.

## Engram mirror
Pending: `mem_save` was blocked by the auto mode hook check earlier in this session. Topic: `odd/starvios-chat/tasks`.

## Next step
S2.
