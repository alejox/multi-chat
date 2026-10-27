// Código compartido por el dock y el overlay.
const PLATFORMS = {
  twitch: { name: 'Twitch', color: '#9146FF' },
  youtube: { name: 'YouTube', color: '#FF0033' },
  kick: { name: 'Kick', color: '#53FC18' },
  demo: { name: 'Demo', color: '#888888' },
};

const ICONS = {
  twitch: '<svg viewBox="0 0 24 24"><path fill="#9146FF" d="M4.3 1 2.5 5.6V20h5v3h2.8l3-3h4.1L23 14.4V1H4.3Zm16.6 12.4-3.3 3.3h-5l-3 3v-3H5.5V3h15.4v10.4Z"/><path fill="#9146FF" d="M15.8 6.6h2v5.8h-2zm-5.4 0h2v5.8h-2z"/></svg>',
  youtube: '<svg viewBox="0 0 24 24"><rect x="1" y="4.5" width="22" height="15" rx="4.5" fill="#FF0033"/><path fill="#fff" d="m10 8.8 5.6 3.2-5.6 3.2z"/></svg>',
  kick: '<svg viewBox="0 0 24 24"><rect x="1" y="1" width="22" height="22" rx="5" fill="#53FC18"/><path fill="#0b0b0b" d="M6.5 5.5h3.6v3.6h1.8V7.3h1.8V5.5h3.6V11h-1.8v2h1.8v5.5h-3.6v-1.8h-1.8v-1.8h-1.8v3.6H6.5z"/></svg>',
  demo: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10" fill="#888"/><path fill="#fff" d="M9 7h3.4a5 5 0 0 1 0 10H9zm2 2v6h1.4a3 3 0 0 0 0-6z"/></svg>',
};

const BADGES = {
  owner: { label: 'HOST', cls: 'b-owner' },
  moderator: { label: 'MOD', cls: 'b-mod' },
  vip: { label: 'VIP', cls: 'b-vip' },
  subscriber: { label: 'SUB', cls: 'b-sub' },
  member: { label: 'MIEMBRO', cls: 'b-sub' },
  verified: { label: '✓', cls: 'b-verified' },
  staff: { label: 'STAFF', cls: 'b-owner' },
};

const FALLBACK_COLORS = ['#FF6B6B', '#4FC3F7', '#FFD54F', '#81C784', '#BA68C8', '#FF8A65', '#4DD0E1', '#F06292', '#AED581', '#9575CD'];

function hashColor(name) {
  let h = 0;
  for (const c of name) h = (h * 31 + c.codePointAt(0)) | 0;
  return FALLBACK_COLORS[Math.abs(h) % FALLBACK_COLORS.length];
}

// Aclara colores muy oscuros para que se lean sobre fondo oscuro.
function readableColor(hex, name) {
  if (!hex || !/^#?[0-9a-f]{6}$/i.test(hex)) return hashColor(name || '');
  hex = hex.replace('#', '');
  let [r, g, b] = [0, 2, 4].map(i => parseInt(hex.slice(i, i + 2), 16));
  const lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  if (lum < 0.45) {
    const k = (0.45 - lum) / 0.55 + 0.25;
    [r, g, b] = [r, g, b].map(v => Math.round(v + (255 - v) * k));
  }
  return `rgb(${r},${g},${b})`;
}

function iconEl(platform, cls = 'pf') {
  const s = document.createElement('span');
  s.className = cls;
  s.title = (PLATFORMS[platform] || {}).name || platform;
  s.innerHTML = ICONS[platform] || ICONS.demo;
  return s;
}

function renderMessage(m, opts = {}) {
  const row = document.createElement('div');
  row.className = 'msg' + (m.kind === 'highlight' ? ' hl' : '') + (m.action ? ' action' : '');
  row.dataset.platform = m.platform;
  row.dataset.channel = m.channel;
  row.dataset.source = m.source || '';
  row.dataset.id = m.id;
  row.style.setProperty('--pc', (PLATFORMS[m.platform] || PLATFORMS.demo).color);

  if (m.system) {
    const sys = document.createElement('div');
    sys.className = 'sys';
    sys.textContent = m.system;
    row.appendChild(sys);
  }

  const line = document.createElement('div');
  line.className = 'line';
  if (opts.icons !== false) line.appendChild(iconEl(m.platform));
  if (opts.time) {
    const t = document.createElement('span');
    t.className = 'time';
    t.textContent = new Date(m.ts * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    line.appendChild(t);
  }
  if (opts.badges !== false) {
    for (const b of m.badges || []) {
      const def = BADGES[b];
      if (!def) continue;
      const el = document.createElement('span');
      el.className = 'badge ' + def.cls;
      el.textContent = def.label;
      line.appendChild(el);
    }
  }
  if (m.amount) {
    const a = document.createElement('span');
    a.className = 'amount';
    a.textContent = m.amount;
    line.appendChild(a);
  }
  const author = document.createElement('span');
  author.className = 'author';
  author.textContent = m.author;
  author.style.color = readableColor(m.color, m.author);
  line.appendChild(author);

  if (m.parts && m.parts.length) {
    const sep = document.createElement('span');
    sep.className = 'sep';
    sep.textContent = m.action ? ' ' : ': ';
    line.appendChild(sep);
    const body = document.createElement('span');
    body.className = 'body';
    if (m.action) body.style.color = author.style.color;
    for (const p of m.parts) {
      if (p.type === 'emote') {
        const img = document.createElement('img');
        img.className = 'emote';
        img.src = p.url;
        img.alt = img.title = p.name;
        img.onerror = () => img.replaceWith(document.createTextNode(p.name));
        body.appendChild(img);
      } else {
        body.appendChild(document.createTextNode(p.text));
      }
    }
    line.appendChild(body);
  }
  row.appendChild(line);
  return row;
}

// Conexión SSE con el servidor local. EventSource reconecta solo.
function connectEvents({ history = true, onState, onHistory, onMessage, onClear, onStatus }) {
  const es = new EventSource('/events' + (history ? '' : '?history=0'));
  es.addEventListener('state', e => onState && onState(JSON.parse(e.data)));
  es.addEventListener('history', e => onHistory && onHistory(JSON.parse(e.data)));
  es.addEventListener('message', e => onMessage && onMessage(JSON.parse(e.data)));
  es.addEventListener('clear', () => onClear && onClear());
  es.onopen = () => onStatus && onStatus(true);
  es.onerror = () => onStatus && onStatus(false);
  return es;
}
