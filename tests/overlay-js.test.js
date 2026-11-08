// Tests for the pure helpers shared by the overlay pages (data/overlay/overlay.js), with Node's
// built-in runner and no dependencies:  node --test tests/overlay-js.test.js
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const path = require('node:path');

const dir = path.join(__dirname, '..', 'data', 'overlay');
// In OBS the pages load icons.js first: its tables are globals that overlay.js reads.
const icons = require(path.join(dir, 'icons.js'));
globalThis.PLATFORMS = icons.PLATFORMS;
globalThis.ICONS = icons.ICONS;
const overlay = require(path.join(dir, 'overlay.js'));

test('safeUrl only lets http and https through', () => {
  assert.strictEqual(overlay.safeUrl('https://cdn.example/e.png'), 'https://cdn.example/e.png');
  assert.strictEqual(overlay.safeUrl('http://x.test/a'), 'http://x.test/a');
  assert.strictEqual(overlay.safeUrl('javascript:alert(1)'), null);
  assert.strictEqual(overlay.safeUrl('data:image/png;base64,AAAA'), null);
  assert.strictEqual(overlay.safeUrl('not a url'), null);
  assert.strictEqual(overlay.safeUrl(''), null);
  assert.strictEqual(overlay.safeUrl(42), null);
});

test('nameColor keeps light colors and lightens dark ones', () => {
  assert.strictEqual(overlay.nameColor('#FF7F50', 'a'), 'rgb(255,127,80)');
  assert.strictEqual(overlay.nameColor('#203040', 'a'), 'rgb(199,203,207)');
  // Current behavior: very dark colors overshoot 255 (CSS clamps it to white). See "Found bugs" in
  // odd/tasks/internal-cleanup.md.
  assert.strictEqual(overlay.nameColor('000000', 'a'), 'rgb(272,272,272)');
});

test('nameColor picks a stable fallback from the name', () => {
  const a = overlay.nameColor('', 'LunaGamer');
  assert.strictEqual(a, overlay.nameColor(null, 'LunaGamer'));
  assert.ok(overlay.FALLBACK.includes(a));
  assert.ok(overlay.FALLBACK.includes(overlay.nameColor('bad', undefined)));
});

test('authorColor follows the name mode', () => {
  const m = { platform: 'kick', color: '#FF7F50', author: 'x' };
  assert.strictEqual(overlay.authorColor({ nameMode: 'platform' }, m), '#53FC18');
  assert.strictEqual(overlay.authorColor({ nameMode: 'platform' }, { platform: 'constructor' }), '#888');
  assert.strictEqual(overlay.authorColor({ nameMode: 'fixed', nameColor: '#123456' }, m), '#123456');
  assert.strictEqual(overlay.authorColor({ nameMode: 'user' }, m), 'rgb(255,127,80)');
});

test('badgeLabel uses the translated member label when there is one', () => {
  assert.strictEqual(overlay.badgeLabel('moderator', {}), 'MOD');
  assert.strictEqual(overlay.badgeLabel('member', {}), 'MEMBER');
  assert.strictEqual(overlay.badgeLabel('member', { memberBadge: 'MIEMBRO' }), 'MIEMBRO');
  assert.strictEqual(overlay.badgeLabel('unknown', {}), undefined);
});

test('own only accepts own string keys', () => {
  assert.ok(overlay.own({ a: 1 }, 'a'));
  assert.ok(!overlay.own({}, 'toString'));
  assert.ok(!overlay.own({ 1: 1 }, 1));
});
