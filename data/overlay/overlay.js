// Helpers shared by the overlay pages (index.html, featured.html, viewers.html, clock.html).
// Load after icons.js: authorColor and platformIcon read its PLATFORMS and ICONS tables.

// Custom CSS from the source properties, in its own <style> after every other style. Only
// textContent: it is CSS text, never HTML or script.
function applyCustomCss(css) {
  let style = document.getElementById('user-css');
  if (!style) {
    style = document.createElement('style');
    style.id = 'user-css';
    document.head.appendChild(style);
  }
  const text = typeof css === 'string' ? css.slice(0, 20480) : '';
  if (style.textContent !== text) style.textContent = text;
}

// Typography from the plugin (font, weight, colors, outline, shadow, line height). The values
// are CSS strings built by the plugin; setProperty ignores anything that is not valid CSS.
// defaultWeight: the text weight of the page when the plugin sends none.
function applyTextStyle(c, defaultWeight) {
  const root = document.documentElement.style;
  const set = (name, value, fallback) => root.setProperty(name, typeof value === 'string' && value ? value : fallback);
  set('--font', c.font, '"Segoe UI", system-ui, -apple-system, "Helvetica Neue", Roboto, sans-serif');
  root.setProperty('--fw', String(Number(c.textWeight) || defaultWeight));
  set('--fg', c.textColor, '#fff');
  root.setProperty('--ow', (Number(c.outlineWidth) || 0) + 'px');
  set('--oc', c.outlineColor, '#000');
  set('--sc', c.shadowColor, 'rgba(0, 0, 0, 1)');
  root.setProperty('--sb', (Number(c.shadowBlur ?? 6) || 0) + 'px');
  if (Number(c.lineHeight) > 0) root.setProperty('--lh', String(Number(c.lineHeight) / 100));
  const body = document.body.classList;
  body.toggle('outline', Number(c.outlineWidth) > 0);
  body.toggle('ts-on', c.shadowMode === 'on');
  body.toggle('ts-off', c.shadowMode === 'off');
}

// True if k is a string key of o itself (not inherited): guards lookups with names from messages.
const own = (o, k) => typeof k === 'string' && Object.prototype.hasOwnProperty.call(o, k);

// Name color by the chosen mode: the user's (lightened if too dark), the platform's or a fixed one.
function authorColor(c, m) {
  if (c.nameMode === 'platform') return Object.prototype.hasOwnProperty.call(PLATFORMS, m.platform) ? PLATFORMS[m.platform] : '#888';
  if (c.nameMode === 'fixed' && typeof c.nameColor === 'string') return c.nameColor;
  return nameColor(m.color, m.author);
}

const BADGES = { owner: 'HOST', moderator: 'MOD', vip: 'VIP', subscriber: 'SUB', member: 'MEMBER', verified: '✓', staff: 'STAFF' };
// The plugin sends the member label in OBS's language (c is the page's config).
function badgeLabel(b, c) {
  return b === 'member' && typeof c.memberBadge === 'string' && c.memberBadge ? c.memberBadge : BADGES[b];
}
const FALLBACK = ['#FF6B6B', '#4FC3F7', '#FFD54F', '#81C784', '#BA68C8', '#FF8A65', '#4DD0E1', '#F06292', '#AED581', '#9575CD'];

function nameColor(hex, name) {
  if (!hex || !/^#?[0-9a-f]{6}$/i.test(hex)) {
    let h = 0;
    for (const c of String(name || '')) h = (h * 31 + c.codePointAt(0)) | 0;
    return FALLBACK[Math.abs(h) % FALLBACK.length];
  }
  hex = hex.replace('#', '');
  let [r, g, b] = [0, 2, 4].map(i => parseInt(hex.slice(i, i + 2), 16));
  const lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  if (lum < 0.45) {
    const k = (0.45 - lum) / 0.55 + 0.25;
    [r, g, b] = [r, g, b].map(v => Math.round(v + (255 - v) * k));
  }
  return `rgb(${r},${g},${b})`;
}

// Solo imágenes por http o https.
function safeUrl(u) {
  if (typeof u !== 'string' || !u) return null;
  try {
    const url = new URL(u);
    return url.protocol === 'https:' || url.protocol === 'http:' ? url.href : null;
  } catch (e) {
    return null;
  }
}

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = String(text);
  return e;
}

function platformIcon(platform) {
  const icon = el('span', 'pf');
  // ICONS son SVG fijos de icons.js; la plataforma solo elige cuál.
  if (own(ICONS, platform)) icon.innerHTML = ICONS[platform];
  return icon;
}

// Node (tests/overlay-js.test.js) loads this file as a module; the pages ignore this.
if (typeof module !== 'undefined')
  module.exports = { applyCustomCss, applyTextStyle, own, authorColor, BADGES, badgeLabel, FALLBACK, nameColor, safeUrl, el, platformIcon };
