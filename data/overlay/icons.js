// Colores y logos de cada plataforma, compartidos por index.html (chat) y viewers.html.
const PLATFORMS = {
  twitch: '#9146FF', youtube: '#FF0033', kick: '#53FC18', tiktok: '#FE2C55',
};
const ICONS = {
  twitch: '<svg viewBox="0 0 24 24"><path fill="#9146FF" d="M4.3 1 2.5 5.6V20h5v3h2.8l3-3h4.1L23 14.4V1H4.3Z"/><path fill="#fff" d="M5.2 3.2h15.5v11H5.2z"/><path fill="#9146FF" d="M15.8 6.6h2v5.8h-2zm-5.4 0h2v5.8h-2z"/></svg>',
  youtube: '<svg viewBox="0 0 24 24"><rect x="1" y="4.5" width="22" height="15" rx="4.5" fill="#FF0033"/><path fill="#fff" d="m10 8.6 5.8 3.4-5.8 3.4z"/></svg>',
  kick: '<svg viewBox="0 0 24 24"><rect x="1" y="1" width="22" height="22" rx="5" fill="#53FC18"/><path fill="#0b0b0b" d="M6.5 5.5h3.6v3.6h1.8V7.3h1.8V5.5h3.6V11h-1.8v2h1.8v5.5h-3.6v-1.8h-1.8v-1.8h-1.8v3.6H6.5z"/></svg>',
  tiktok: '<svg viewBox="0 0 24 24"><defs><path id="tt" d="M12.2 5h2.2c.4 2.3 2.2 3.6 4.2 3.7V11c-1.6 0-3.1-.6-4.2-1.6v6.1a4.3 4.3 0 1 1-4.3-4.3v2.2a2.1 2.1 0 1 0 2.1 2.1z"/></defs><rect x="1" y="1" width="22" height="22" rx="5" fill="#0b0b0b"/><use href="#tt" fill="#25F4EE" x="-.8" y="-.8"/><use href="#tt" fill="#FE2C55" x=".8" y=".8"/><use href="#tt" fill="#fff"/></svg>',
};

// "#1a2b3c" → "26, 43, 60", para usarlo en rgba(var(--…), alfa).
function rgbTriplet(hex) {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex || '');
  return m ? m.slice(1).map(h => parseInt(h, 16)).join(', ') : '0, 0, 0';
}

// Fondo de un recuadro según la configuración: <prefix>Color, Opacity, Radius, BorderWidth y
// BorderColor → variables --<prefix>-rgb, -alpha, -radius, -bw y -brgb de la página.
function applyBoxStyle(c, prefix) {
  const root = document.documentElement.style;
  root.setProperty(`--${prefix}-rgb`, rgbTriplet(c[prefix + 'Color']));
  root.setProperty(`--${prefix}-alpha`, String((c[prefix + 'Opacity'] ?? 60) / 100));
  root.setProperty(`--${prefix}-radius`, (c[prefix + 'Radius'] ?? 0) + 'px');
  root.setProperty(`--${prefix}-bw`, (c[prefix + 'BorderWidth'] ?? 0) + 'px');
  root.setProperty(`--${prefix}-brgb`, rgbTriplet(c[prefix + 'BorderColor'] || '#ffffff'));
}

// Fecha y hora "28 OCT 2026 04:20" en el idioma del sistema. o: {date, time, seconds, hour24}.
// La usan el reloj de la cinta (index.html) y la fuente de fecha y hora (clock.html).
function clockLabel(now, o) {
  const parts = [];
  if (o.date !== false)
    parts.push(now.toLocaleDateString(undefined, { day: '2-digit', month: 'short', year: 'numeric' })
      .replace(/\./g, '').replace(/\sde\s/g, ' ').toUpperCase());
  if (o.time !== false)
    parts.push(now.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit',
      second: o.seconds ? '2-digit' : undefined, hour12: o.hour24 === false }).toUpperCase());
  return parts.join(' ');
}
