# Feature: properties-layout

## Objective
The properties window of every Multichat source (chat, featured, viewers) is ordered by what users change most, and the background controls are easy to find. Only rarely used options stay collapsed.

## Problem and why
- In the chat source (`src/overlay/properties.cpp:315-397`) the order is: channels button, preset, Layout, Platforms & badges (7 checkboxes), Date & time (7 options), then the "Show advanced options" switch, which hides Text, Background, Animation and Fine tuning (`showAdvancedGroups`, lines 130-138). Font size and background opacity sit behind ~18 controls and a switch nobody associates with "background".
- Two backgrounds with unrelated locations: "Background" (behind each message, group_background) and "Panel" (behind the whole chat, inside Fine tuning, lines 392-393). The panel is what users see as "the chat background".

## Decision (user, 2026-11-08)
New order, same for the three sources where it applies:
1. Channels button + **Style**: preset, font size, font family/weight, text and name colors; outline/shadow details only appear when used (current behavior).
2. **Background**: one group with two parts, "Behind each message" (`background` + style list + color/opacity, radius/border only when the style uses them, message padding) and "Behind the whole chat" (`panel` + its color/opacity/radius/border). Each part's controls only show when its checkbox is on.
3. **Layout**: vertical/horizontal, size/fit canvas, stacking, speed (current visibility rules).
4. **Date & time** (chat only): checkbox; options only when checked (current behavior).
5. **More options**, collapsed by default at the end (reuse the existing `advanced` key, relabelled "Show more options"): platforms filter, icons and badges, animation/auto-hide, disappear after, max messages, spacing, margins, line height, custom CSS.

## Constraints
- Saved keys never change (scenes keep loading identically); only placement, group names, labels and visibility change. Presets unaffected.
- Preserve existing visibility rules (horizontal vs vertical, style-dependent radius/border/color, clock, auto-hide) and the `on_modified` preset → Custom behavior.
- Locales en-US/es-ES updated; unused keys removed (test-chat locale test fails on unused keys). README source-properties section updated.
- Tests: `cmake -S tests -B build_tests_brew -DCMAKE_PREFIX_PATH=$(brew --prefix qt) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build_tests_brew -j8 && ctest --test-dir build_tests_brew --output-on-failure` (base 6/6: test-chat 71, test-tts 67, test-overlay 20, test-box-measure 9, test-manager 35, overlay-js 6). macOS build `cmake --preset macos && cmake --build --preset macos`, 0 warnings. No push.
- RED→GREEN where pure logic exists (e.g. a pure function deciding group membership/visibility if extracted). The OBS properties window itself has no automated test: structural check + manual checklist.

## Delivery
- Branch `feat/properties-layout`, stacked on `feat/voice-settings` (28d5dbc).

## Tasks
- [x] T1 Chat source: new order, unified Background group, More options. Route: delegated (writer). Commit b6816be.
  - Order: Channels button; Style (preset, font size, font, weight, text color, name mode/color, outline, shadow); Background (shared background style list, "Behind each message" + color/opacity/radius/border + message padding, "Behind the whole chat" + panel color/opacity/radius/border); Layout; Date and time; "Show more options"; More options (platforms, icons, badges, disappear after, max messages, spacing, margins, line height, custom CSS).
  - Background is ONE flat group (no nested groups). The style list sits first because it styles both the message box and the panel; it shows when either checkbox is on (existing rule).
  - Line height moved out of `addTextStyle` into `addLineHeight` (More options). `obs_properties_get` searches nested groups recursively (`.deps/obs-studio-33.0.0/libobs/obs-properties.c:296-321`), so the visibility callbacks keep finding moved properties.
  - Locales: Group.Style added; Source.Advanced "Show more options", Group.Advanced "More options", Source.Background "Behind each message", Source.Panel "Behind the whole chat", Panel.* "Chat background ...".
- [x] T2 Featured and viewers sources: same structure where it applies; locales, README. Route: delegated (writer). Commit 8c0e74c.
  - Featured: Style; Background; Layout (vertical/horizontal position, width, height); Show more options; More options (icons, badges, animation, auto-hide + times, padding, line height, custom CSS).
  - Viewers: Channels button; Style; Background; Layout (row/column, total, names); Show more options; More options (platforms, padding, custom CSS).
  - Legacy clock source (not in the decision, aligned for consistency since it shares `showAdvancedGroups`): Style; Background; Date and time; Show more options; More options (padding, custom CSS).
  - `showAdvancedGroups` only toggles `group_advanced`. Removed unused Group.Text, Group.Platforms, Group.PlatformsOnly, Group.Animation (both locales). README "Aspecto de las fuentes" rewritten for the new order.
- [x] T3 Tests, build; install by parent (OBS closed). Route: delegated (writer) + parent spot check. Install pending (parent).
  - tests: 6/6 passed (test-chat 71, test-tts 67, test-overlay 20, test-box-measure 9, test-manager 35, overlay-js 6).
  - `cmake --preset macos && cmake --build --preset macos`: BUILD SUCCEEDED, 0 warnings.
  - Saved keys unchanged (only placement, group names and labels); `advanced` default false still asserted (tests/test-overlay.cpp:97).
  - No automated test for the OBS properties window: manual checklist pending (open each source, toggle backgrounds, layout, clock, Show more options, preset to Custom).

## Next step
Parent: install with OBS closed, manual OBS check of the three sources.
