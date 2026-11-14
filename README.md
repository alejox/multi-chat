<div align="center">

# 💬 AC Multichat

**Twitch, YouTube, Kick, TikTok and Starvios chats together in one OBS dock and on your scene. It detects when you multistream and connects only the chats of the platforms where you're live.**

[![Latest release](https://img.shields.io/github/v/release/alejox/multi-chat?label=release&color=6e56cf)](../../releases/latest)
![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-13%2B-000000?logo=apple&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-Ubuntu%2024.04%2B-E95420?logo=ubuntu&logoColor=white)
![OBS](https://img.shields.io/badge/OBS-33%2B-302E31?logo=obsstudio&logoColor=white)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue)](LICENSE)

**English** · [Español](README.es.md)

![AC Multichat on-screen chat](docs/overlay-vertical.png)

[**⬇️ Download**](../../releases/latest) &nbsp;·&nbsp; [Install](#-install) &nbsp;·&nbsp; [Features](#-features) &nbsp;·&nbsp; [FAQ](#-faq)

</div>

---

A native OBS Studio 33+ plugin: no Python, no accounts, no API keys, no browser extensions.

## ✨ Features

| | |
|---|---|
| 🧩 **"Multichat" dock** | Every message together with each platform's logo, badges (MOD, VIP, SUB, HOST), emotes, Super Chats, bits, subscriptions and TikTok gifts. |
| 👁 **Viewers** | How many people are watching right now on each platform and in total, in the dock and in the chats window. Updated every 30 s. |
| 📡 **Multistream detection** | When you press *Start Streaming* it checks which platforms you're live on and connects those chats. The dock shows **"Multistream detected"**. |
| ➕ **Many chats** | Add as many channels as you want, several from the same platform too, from **Tools → Multichat…** (or the dock's gear → *Settings…*). When you type or paste a channel, its profile picture appears so you can check it's the right one; if it doesn't exist, it says so and offers **Add anyway**. If it couldn't be checked (no connection or the platform didn't answer), it says so and still lets you **Add** it. Each chat can be paused without removing it and reordered by dragging (the dock follows that order). To remove one, click the ✕ on its right. |
| 🎬 **"Multichat" source** | The chat on your scene with a transparent background. Vertical (a list of messages) or horizontal (a ticker scrolling right to left). Its Properties are ordered by what you change most (Style, Background, Layout, Date and time) and *Show more options* reveals the rest. |
| ✨ **Presets and typography** | One click changes the look: Clean, Neon, Bubbles, Minimal single line, High contrast and Vertical 1080×1920. Font, weight, text and name color, outline, shadow and line height, plus custom CSS for advanced users. See [How the sources look](#-how-the-sources-look). |
| 🕒 **Date and time in the chat** | The "Multichat" source can show the date and time (with or without seconds, 24 h or 12 h, optional bar): at the end of the ticker when horizontal, or above or below the messages when vertical. To put them anywhere else on the scene, apart from the chat, there's the **Multichat (date and time)** source. |
| 🎨 **Background styles** | A background per message and one behind the whole chat, with color, opacity, rounding and border. Styles: Solid, Glass, Pill, macOS window, Minimal, Line, Synthwave, GTA 6 (Vice City), Neon city, Cyberpunk, Retro and Glitch. Also in the viewers box and the date and time. |
| 👁 **"Multichat (viewers)" source** | A separate box with the logo and viewers of each live channel, plus the total. In a row or a column, with or without the channel name. |
| ⭐ **Featured message** | Click a message in the dock and it shows large on screen with the **"Multichat (featured)"** source: platform logo, name in its color, badges, emotes and donation amount. It animates in and out and hides by itself after a while. Click it again or press Esc to remove it, also with hotkeys. |
| 🔊 **Voice** | Reads the chat aloud in every scene, with a checkbox in the **Voice** tab of **Tools → Multichat…**. It shows in the Audio Mixer as "Multichat (voice)", reaches the stream and you hear it too. It can also play a sound per message (or sound and reading) so you notice without looking at the screen. |
| 🌐 **English and Spanish** | Everything follows the OBS language: menus, connection errors, on-screen sample messages and the notices Multichat builds (TikTok gifts; Starvios raids, redemptions, subscriptions and gifts). Twitch and YouTube notices arrive already written by the platform. |
| 🛡️ **Moderation** | If a moderator deletes a message, or bans or times out someone, their messages disappear from the dock, the on-screen chat and the featured message. See [Moderation and filters](#-moderation-and-filters). |
| 🚫 **Filters** | Hide bots (Nightbot, StreamElements, Streamlabs… are included), commands starting with `!` and blocked words, from the **Filters** tab of **Tools → Multichat…**. |
| 🔇 **Mute a channel** | Click a channel in the dock to mute its messages in Multichat or show them again. It doesn't touch the platform. |
| 🧪 **Test messages** | One switch (in the dock's gear and in Settings) that fills the dock with a test chat and makes the on-screen sources show sample messages and numbers while there's no chat. They never show while you stream. |
| 🔄 **Updates** | OBS tells you when there's a new version and installs it for you after you accept. |

![Horizontal ticker with date and time](docs/overlay-horizontal.png)

## 📦 Install

Requires **OBS Studio 33 or newer** (Windows 10/11, macOS 13+ or Linux x86_64). With an older OBS the plugin doesn't load and OBS says some plugins "failed to load": update OBS first.

Download the file for your system from **[Releases](../../releases/latest)** and **close OBS before installing**.

| | Windows 10/11 | macOS 13+ | Linux x86_64 |
|---|---|---|---|
| **File** | `obs-multichat-<version>-windows-x64.zip` | `obs-multichat-<version>-macos.zip` | `obs-multichat-<version>-linux-x86_64.tar.gz` |
| **You get** | the `obs-multichat` folder | `obs-multichat.plugin` | the `obs-multichat` folder |
| **Copy it to** | `C:\ProgramData\obs-studio\plugins\` (accept the administrator prompt) | `~/Library/Application Support/obs-studio/plugins/` | `~/.config/obs-studio/plugins/` |
| **Then** | Delete old copies, if any (see below) | Run the Terminal command below (once) | — |

<details>
<summary><b>Windows step by step</b></summary>

Since OBS 33, plugins go in **`C:\ProgramData\obs-studio\plugins`**, one folder per plugin (no longer inside `C:\Program Files\obs-studio`).

1. Unzip `obs-multichat-<version>-windows-x64.zip` (right-click → **Extract All**). You get the `obs-multichat` folder.
2. Open `C:\ProgramData\obs-studio\plugins` (type it in the Explorer address bar; `ProgramData` is hidden). Create the `plugins` folder if it doesn't exist.
3. Copy the `obs-multichat` folder there (replace it if it was already there) and accept the administrator prompt. It must look like this:

   ```
   C:\ProgramData\obs-studio\plugins\obs-multichat\
   ├── obs-multichat.dll
   └── data\
       ├── manifest.json
       ├── locale\
       └── overlay\
   ```

4. If you installed an older version in `C:\Program Files\obs-studio`, delete `obs-plugins\64bit\obs-multichat.dll` and the `data\obs-plugins\obs-multichat` folder: OBS 33 marks that layout as *Legacy* and OBS 34 no longer loads it.
5. Open OBS.
</details>

<details>
<summary><b>macOS step by step</b></summary>

1. Unzip `obs-multichat-<version>-macos.zip`: you get `obs-multichat.plugin`.
2. Copy it to `~/Library/Application Support/obs-studio/plugins/` (in Finder: **Go → Go to Folder…**). Replace the old version if there is one.
3. In Terminal, once:

   ```bash
   xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-multichat.plugin
   ```

4. Open OBS.
</details>

<details>
<summary><b>Linux step by step</b></summary>

Built on Ubuntu 24.04 for the OBS from the [official PPA](https://launchpad.net/~obsproject/+archive/ubuntu/obs-studio) (`sudo add-apt-repository ppa:obsproject/obs-studio`); it works on Ubuntu 24.04 or newer and derivatives (Linux Mint 22, Pop!_OS 24.04…). The overlay uses the OBS Browser source: the PPA build has it, the `obs-studio` package from Ubuntu's own repositories doesn't.

```bash
mkdir -p ~/.config/obs-studio/plugins
tar -xzf obs-multichat-*-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins
```

It must look like this:

```
~/.config/obs-studio/plugins/obs-multichat/
├── bin/64bit/obs-multichat.so
└── data/
    ├── manifest.json
    ├── locale/
    └── overlay/
```

The voice uses **espeak-ng**, which Ubuntu desktop usually includes; if it's missing: `sudo apt install espeak-ng`.
</details>


### Automatic updates

When OBS opens, the plugin checks this repository's GitHub Releases for a new version. If there is one, it asks with **Update now**, **Later** or **Skip this version** (the **Multichat** dock reminds you with a button). **Update now** downloads it, checks its SHA-256 checksum, installs it and asks you to restart OBS. Nothing is installed without your click. You can also check by hand with **Check for updates**, in **Tools → Multichat…** or in the dock's gear.

Before installing, the plugin compares the downloaded file with the SHA-256 checksum published with the release: the one GitHub gives for each file and, if that's missing, the `SHA256SUMS` published next to the zips. If it doesn't match or can't be fetched, nothing is installed and it says so; you can still download the version by hand from the page.

- **Windows:** installs into `C:\ProgramData\obs-studio\plugins\obs-multichat`, which belongs to the administrator: Windows asks for permission once to copy the plugin. A copy in the old folder (`C:\Program Files\obs-studio`) is moved to the new one. If the copy takes more than 3 minutes (a slow antivirus, for example), the plugin says it's still running instead of reporting a failure.
- **macOS:** replaces `obs-multichat.plugin` in its folder and clears the "downloaded from the internet" flag, so macOS doesn't ask again.
- **Linux:** replaces `~/.config/obs-studio/plugins/obs-multichat`, with no permission prompt (downloads with `curl`, or `wget` if `curl` is missing).

To see the installed version: **Tools → Plugin Manager** (OBS 33).

## 🎛️ Use it

The first time you open OBS with the plugin and no chats set up, the **first-run assistant** appears: the platform is picked from your streaming service, you add your channel, one button puts the chat and the featured message in the scene you're editing (without duplicating them if they're already there), a checkbox turns on *Read chat aloud*, and finishing opens the dock. You can skip it and open it again with **First-run assistant**, in **Tools → Multichat…** or in the dock's gear.

1. **Tools → Multichat…**. Pick the platform, type the channel and press **Add**. You can also paste the channel URL or a YouTube live URL. A loading bar shows while it looks up the channel; if the name has a format the platform doesn't allow, it says so below, and if the channel doesn't exist the button becomes **Add anyway** (Enter never adds a channel that wasn't found).
   - **Pause**: untick a chat to disconnect it without losing it; tick it again to reconnect.
   - **Reorder**: drag the chats (or select one and press **Alt+↑ / Alt+↓**). The dock's chips follow the same order.
   - **Remove**: the ✕ on the row or the **Delete** key with the chat selected.
2. **Docks → Multichat** to see the chat inside OBS (the assistant opens it when you finish).
3. **Add sources to the scene** (in Settings) puts the chat and the featured message in the scene you're editing at once, only the missing ones. To do it by hand, or for the other sources: **Sources → + → Multichat (on-screen chat)**. For the ticker, choose *Layout → Horizontal*. With *Fit to the canvas automatically* (on by default) it takes the whole canvas width at the height of one line; turn it off to set the size by hand. *Movement* can be continuous or per message (it moves with each message and stops). With *Date and time → Show date and time* the ticker ends with the date and time on the right; when vertical they go above or below the messages (*Position (vertical)*).
4. Viewers on screen: **Sources → + → Multichat (viewers)**.
5. The date and time are a chat option (the **Date and time** group in its Properties). If you want them somewhere else, apart from the chat, use **Sources → + → Multichat (date and time)**.
6. To read the chat aloud: **Tools → Multichat… → Voice → Read chat aloud**. See [Read the chat aloud](#-read-the-chat-aloud).
7. To show one message large on screen: **Sources → + → Multichat (featured)**. See [Feature a message](#-feature-a-message).

### 🧩 The dock

- **Read at your pace**: if you scroll up to read, the list doesn't move when messages arrive. The **↓ N new** pill appears; click it or press **End** to go back to the bottom.
- **Right-click a message**: *Feature on screen* / *Remove from screen*, *Hide message*, *Hide user*, *Copy message* and *Copy name* (plain text).
  - *Hide message* removes it from the dock, the on-screen chat and the featured message, like a moderation delete, but only in Multichat: it doesn't touch the platform. It works with test messages too.
  - *Hide user* adds them to **Filters → Hidden users** and removes their messages from that chat.
- **Search**: **Ctrl+F** (⌘F on macOS) or the magnifier button. Filters by text, emote or name, ignoring case and accents, and says how many match. **Esc** closes the search.
- **Time**: right-click → *Show time* adds the time to each message (off by default; remembered).
- **The gear** (top right): *Settings…*, *Test messages*, *Show time*, *Text size*, *Check again which platforms you are live on*, *First-run assistant* and *Check for updates*.
- **Clear chat**: the trash button clears the dock, the on-screen chat and the featured message. For a few seconds **Chat cleared · Undo** appears; *Undo* brings the messages back to the dock and the on-screen chat (in the horizontal ticker, only the last 3). If you clear twice in a row, *Undo* brings everything back. The voice doesn't read them again.
- **Theme**: icons and colors follow the OBS theme, light or dark.

### ⭐ Feature a message

1. Add **Sources → + → Multichat (featured)** to your scene. With *Test messages* on it shows a sample message while nothing is featured, so you can place it (never while streaming).
2. In the **Multichat** dock, click a message: it appears in the source and is highlighted in the dock. Clicking another message replaces it.
3. While it's on screen, a bar at the top of the dock list shows it with two buttons: **⏸ Pause** keeps it on screen with no time limit until you remove it (**▶ Resume** starts counting again from where it stopped), and **✕ Remove** takes it off.
4. To remove it: **✕ Remove**, click the highlighted message again or press **Esc** with the dock focused.

In its **Properties** you choose the animation (slide in, fade, zoom or none), the position inside the source, the font size, the width and height, the background with the same styles as the chat, and whether the logo and badges show. With *Goes away on its own* (on by default) it stays on screen 8 s plus 60 ms per character, 30 s at most; turn it off to keep it until you remove it. The plugin keeps the time, not the page: when it runs out, the dock stops highlighting the message as it leaves the screen, and *show the last one again* still works. If you have several featured sources with different times, the shortest of those with *Goes away on its own* on wins; if none has it on, the message stays until you remove it.

**Settings → Hotkeys** has three hotkeys with no key assigned: *Multichat: remove featured message*, *Multichat: show the last featured message again* and *Multichat: pause or resume the featured message timer*. Give them any key you like.

If the source's page reloads, the featured message comes back only if its time hasn't run out. Clearing the chat from the dock also removes the featured message.

### 🎨 How the sources look

The Properties of the sources (chat, featured, viewers and date and time) follow the order of what you change most:

1. **Style**: preset, font size, font, weight and text and name colors. The outline color and the shadow color and blur only show if you use them.
2. **Background**: in the chat, the *Background style* (shared by both), *Behind each message* (color, opacity, rounding and border depending on the style, plus each message's padding) and *Behind the whole chat* (the general background, with its color, opacity, rounding and border). In the others, the box background with its style. Each part only shows its controls when its checkbox is on.
3. **Layout**: vertical or horizontal, size or fit to the canvas, stacking and speed (chat); position and size (featured); row or column, total and names (viewers).
4. **Date and time** (chat): the checkbox and, once on, its options.
5. **More options**, folded by default with the **Show more options** checkbox right above: platforms, icons and badges, animation and *Goes away on its own* (featured), disappear after, maximum messages, spacing, margins, line height and custom CSS.

Saved scenes stay the same: no existing option changed, only where it shows.

- **Preset** (at the top, in *Style*): pick one and several options change at once; you can still change everything. If you change something the preset set, the list goes back to *Custom*.

  | Preset | How it looks |
  |---|---|
  | Clean | White text with a soft shadow, no boxes. |
  | Neon | Synthwave boxes, names in the platform color and a magenta glow. |
  | Bubbles | Very rounded dark bubbles, no shadow and more space. |
  | Minimal single line | In the chat, the horizontal ticker fitted to the canvas on a dark strip with the date and time. In the others, a matte black box. |
  | High contrast | Opaque black boxes with a white border, bold text and yellow names. |
  | Vertical 1080×1920 | Big glass bubbles for a vertical canvas (a 1000×1400 chat). |

- **Style**: font (the ones installed on the computer; if it's missing, the system font is used), weight, text color, name color (the user's, the platform's or a fixed one; chat and featured), outline and shadow (*Automatic*, as before: only without a background; *Always*; *Never*) with their color and blur. Line height (chat and featured) is in *More options*.
- **More options → Custom CSS**: CSS added to the page after the plugin's styles (up to 20 KB). Only CSS: no code is ever run. For example `.author { text-transform: uppercase; }`.

### 🛡 Moderation and filters

When a platform's moderators delete a message or ban someone, Multichat removes it from the dock (without moving the list if you're reading further up), from the on-screen chat (with its exit animation) and from the featured message. A delete only affects the channel where it happened: never messages from another chat.

| Platform | Deleted message | Ban or timeout | Chat cleared |
|---|---|---|---|
| Twitch | ✅ | ✅ | ✅ |
| YouTube | ✅ | ✅ (the author's hidden messages) | — |
| Kick | ✅ | ✅ | ✅ |
| Starvios | ✅ | — | — |
| TikTok | — | — | — |

TikTok doesn't send, in what Multichat reads, any delete notice that can be matched to messages already shown. Starvios only reports deleted messages.

In **Tools → Multichat… → Filters** you can set:

- **Hidden users**: one per line. The most common bots are included (Nightbot, StreamElements, Streamlabs, Moobot, Fossabot, Wizebot, Botrix); the *Add known bots* button brings them back if you deleted them.
- **Commands**: messages starting with `!` (on by default). Subscriptions, Super Chats, bits and gifts are always shown.
- **Blocked words**: one word or phrase per line. Whole words are compared, ignoring case and accents: *cat* hides "CAT" but not "category".

Filters are saved right away and apply to messages that arrive from then on; hidden messages aren't read aloud either.

### 🔊 Read the chat aloud

Works on Windows, macOS and Linux with the system voices, with no external services. On Linux it uses **espeak-ng**: the voice is more robotic than on Windows and macOS.

1. Open **Tools → Multichat…**, **Voice** tab, and tick **Read chat aloud** (it's also in the first-run assistant). There you choose the voice, the speed, whether the sender's name is read, which platforms are read and the maximum characters per message. You don't need to add any source: it plays in every scene.
2. Once on, **Multichat (voice)** appears in the **Audio Mixer**. Volume, mute, monitoring and audio filters are set there like any other audio, and they're kept between sessions. The first time it's on *Monitor and Output*: it reaches the stream and the recording and you hear it too. Unticking the checkbox removes it from the mixer.
3. Blocked words, hidden users and commands are the ones in the **Filters** tab: what they hide isn't shown or read.

If you come from an older version: the first time, it takes the settings of the first voice source in your scenes that you had changed (on or off, as it was), and the blocked words of all of them move, without repeats, to the **Filters** tab. Those old sources still load but no longer read anything; you can delete them.

What is read and what isn't:

- Only new messages, never the history.
- Emotes, links and emojis are not read. A message left with no letters or numbers is skipped.
- Chat notices (subscriptions, gifts…) are read. A Twitch `/me` is read as "Ana waves".
- A message hidden by the filters (for example, with a blocked word) isn't read.
- If a message is deleted (platform moderation or *Hide message* in the dock) before it's read, it leaves the queue; if it was being read, it's cut.
- Messages are read one at a time. If the queue reaches 20, the oldest is dropped so the delay doesn't grow.
- **When a message arrives** chooses what the voice does: *Read it aloud* (as always), *Play a notification sound* (a short sound per message, no reading, so you notice without looking at the screen) or *Sound, then read it*. The sound is built in; you can use your own WAV (16-bit PCM, up to 10 s) and limit how many play with *Minimum time between sounds* (1.5 s by default; in the mixed mode it only limits the sound). The chosen platforms and the filters apply to all three modes, and a message deleted before its sound doesn't play (nor do the queued ones from someone banned or timed out in that chat).
- *Skip current message* cuts the one being read. Unticking *Read chat aloud* clears the queue and stops the voice.
- *Test messages* are read too, so you can hear the voice without being live, but never while you stream.

Limits: the first message can take a few seconds while the system loads the voice, and only links starting with `http://`, `https://` or `www.` are removed, so `example.com` would be read.

### When each chat connects

| Mode | Behavior |
|---|---|
| **Automatic** (default) | Checks every minute which channels are live and connects only those chats. When you start streaming it checks every 15 s for 3 minutes. If a channel's state can't be known, it connects while OBS is streaming. |
| **Always connected** | Connects every chat even when you're not live. YouTube, TikTok and Starvios wait for the live to start. |

Paused chats don't connect in either mode.

It works with any way of multistreaming (Restream, *Multiple RTMP outputs*, Aitum Multistream…), because it asks each platform directly. If the OBS streaming service is Restream, Castr or StreamYard, it's also marked as multistream.

### Platform notes

- **Twitch:** anonymous, read-only IRC connection.
- **YouTube:** reads the public live chat, no API key. With `@channel` it looks only for the active live.
- **Kick:** Kick sometimes blocks the request that gives the chat ID. If the channel shows an error, add it with the ID: `mychannel#123456`. To find it, open `https://kick.com/api/v2/channels/mychannel` in the browser and look for `"chatroom":{"id":…}`.
- **TikTok:** type your username (`@mychannel`) or paste the URL. TikTok requires the chat connection to be signed, so the plugin uses [Euler Stream](https://www.eulerstream.com), a free external service with no account that limits connections per day. Over the limit, the chat shows an error and retries after 10 minutes. Shows comments and gifts.
- **Starvios:** type your username (`mychannel`) or paste the URL (`https://starvios.com/live/mychannel`). No account or key: the chat is read anonymously and read-only, the same way its website does, and the plugin doesn't count as a viewer. Messages missed during a reconnection are shown but not read aloud. **This connector is unofficial** and may stop working if Starvios changes.

YouTube, Kick, TikTok and Starvios have no official public API to read the chat without an account: if they change their site, the plugin may need an update.

## ❓ FAQ

<details>
<summary><b>OBS says the plugin "failed to load"</b></summary>

You're on OBS 32 or older. Update OBS to 33 or newer. On Windows, also check the plugin is in `C:\ProgramData\obs-studio\plugins\obs-multichat\` and not in the old `C:\Program Files\obs-studio` folders.
</details>

<details>
<summary><b>A chat stays in error</b></summary>

**Tools → Multichat…** shows the reason under the channel. For Kick, add it with the chat ID (see [Platform notes](#platform-notes)). For TikTok, the free signing service may have reached its daily limit: it retries by itself after 10 minutes. The OBS log (**Help → Log Files**) has the `[obs-multichat]` lines.
</details>

<details>
<summary><b>The chat doesn't connect when I'm not live</b></summary>

That's the **Automatic** mode: it connects only the channels that are live. Switch to **Always connected** in **Tools → Multichat…**, or use *Test messages* to place the overlay.
</details>

<details>
<summary><b>macOS says the plugin is damaged or blocks it</b></summary>

Run the `xattr -cr …` command from the install steps. The plugin isn't signed with a paid Apple developer account.
</details>

## 🔒 Privacy

- No accounts, no logins, no telemetry. Everything runs inside OBS on your computer; there's no server of ours.
- To read the chats and viewer counts, the plugin talks directly and anonymously to each platform you add: Twitch (IRC and its public GraphQL), YouTube, Kick, TikTok and Starvios (its public Supabase endpoint). Emotes and profile pictures are loaded from the platforms' own image servers.
- **TikTok only:** the room ID of the TikTok channel you add is sent to [Euler Stream](https://www.eulerstream.com) to sign the connection.
- **Updates:** the plugin asks the GitHub API for this repository's latest release when OBS opens and, only if you accept, downloads it from GitHub.
- The channels you add and your settings are saved in the OBS configuration folder on your computer.

## 🛠️ For developers

<details>
<summary><b>Build, tests and project layout</b></summary>

The plugin uses the [official OBS plugin template](https://github.com/obsproject/obs-plugintemplate) updated to **OBS 33.0.0**: obs-deps and Qt 6.11 from `2026-10-23`, macOS 13+ universal, Windows x64. Configuring downloads the dependencies by itself. On Linux it uses the system OBS, Qt 6, libcurl and espeak-ng.

```bash
# macOS (Xcode 26.5+: OBS 33 requires the macOS 26.5 SDK)
cmake --preset macos && cmake --build --preset macos

# Windows (Visual Studio 2022, SDK 10.0.26100)
cmake --preset windows-x64
cmake --build --preset windows-x64

# Ubuntu 24.04 (with the OBS from the official PPA)
sudo apt install obs-studio qt6-base-dev qt6-base-private-dev libcurl4-openssl-dev libespeak-ng-dev libsimde-dev ninja-build
cmake --preset ubuntu-x86_64 && cmake --build --preset ubuntu-x86_64
```

Tests (no OBS or network):

```bash
cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
```

Every push builds on GitHub Actions for Windows, macOS and Linux (`.github/workflows/build.yml`). To publish a version, bump `version` in `buildspec.json` and in `data/manifest.json` (CI checks they match) and push it to `main`: the *Publicar versión* workflow creates the `v<version>` tag and the Release with the packages for the three systems. You can also push the tag by hand or run the workflow from Actions.

```
src/plugin-main.cpp        plugin load, dock, menu and streaming events; connects the manager to OBS
src/chat-manager.*         live / multistream detection and chat lifecycle (no OBS: gets config, translator and log)
src/chat-history.*         recent history, deletes and "undo clear"
src/featured-state.*       featured message: current, last, undo copy and time on screen
src/config-store.*         config.json (with "version") and atomic saving of the plugin's JSON files
src/i18n.*                 translated strings (i18n::tr) from data/locale
src/platforms-registry.hpp platforms: id, name, color and order (data/overlay/icons.js has a copy checked by the tests)
src/overlay/               "Multichat", "Multichat (viewers)", "Multichat (featured)" and "Multichat (date and time)" sources (private browser sources)
  source.cpp               OBS callbacks and source registration
  properties.cpp           properties window
  config.cpp               settings → configuration sent to each page
  viewers-layout.cpp       box sizes (always measured on the UI thread) and fitting the ticker to the canvas
  box-measure.*            pure measuring of the viewers and clock boxes with QFont (no OBS, tested in tests/test-box-measure.cpp)
  wiring.cpp               shared state and routing of the manager's events to the pages
src/overlay-presets.*      defaults, presets and sanitizing of CSS/colors/fonts (no OBS, tested in tests/test-overlay.cpp)
src/tts/                   voice: global settings and migration (voice-settings), plugin-owned "Multichat (voice)" output on a free output channel (voice-source; v0 and v2 sources saved in scenes load silently), text filter, queue, real-time output and voice (AVSpeechSynthesizer on macOS, SAPI on Windows, espeak-ng on Linux)
src/chat/                  connectors and parsers: twitch, youtube, kick, tiktok, starvios, demo
src/net/                   system networking: WinHTTP (Windows), NSURLSession (macOS) and libcurl (Linux)
src/ui/                    dock (chat-dock, chat-view, dock-prefs), settings window (settings-dialog, channel-input, chat-list), first-run assistant, emote and profile caches
data/icons/                monochrome SVG icons for the dock (tinted with the theme color)
src/updater.*              automatic updates from GitHub Releases: lookup, download and SHA-256 check
src/updater/               per-system installers (install-win/-mac/-linux.cpp, only the system's one is built), copy-tree.cpp and the PowerShell script (install-script-win.hpp)
data/overlay/              chat (index.html), featured (featured.html), viewers (viewers.html) and date and time (clock.html) pages; icons.js and overlay.js are the shared code
data/locale/               English and Spanish strings (also chat notices and connection errors, following the OBS language)
tests/                     tests without OBS: parsers (test-chat), voice (test-tts), overlay (test-overlay), box measuring (test-box-measure, headless QtGui), manager (test-manager) and the pages' JS (overlay-js.test.js, with node if installed)
```

Why native networking and not QtNetwork? OBS 33 doesn't ship Qt's TLS plugins, so HTTPS through QtNetwork doesn't work inside OBS. WinHTTP and NSURLSession handle HTTPS and WebSocket with no extra dependencies. On Linux, libcurl (which OBS already uses) does HTTPS and the WebSocket TLS connection; the WebSocket protocol lives in `src/net/net-linux.cpp`.
</details>

## 📄 License

GPL-2.0 or later, like every plugin that links against libobs — see [LICENSE](LICENSE).

AC Multichat is an independent project, not affiliated with or endorsed by the OBS Project, Twitch, YouTube, Kick, TikTok or Starvios.
