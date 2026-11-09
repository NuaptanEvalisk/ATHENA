'use strict';

const el = id => document.getElementById(id);
const buttons = ['first','previous','next','last','black','laser','timerPause','timerReset','start','stop'];
let connected = false;
let approved = false;
let lastRevision = '';
let state = {active:false,page:0,count:0,finished:false,black:false,timerPaused:false,elapsedSeconds:0};
let laserEnabled = false;
let lastPointerSent = 0;
function clearPicture(which) {
  const image = el(which+'Image');
  image.removeAttribute('src');
  image.classList.remove('visible');
  el(which+'Placeholder').classList.remove('hidden');
}

function statusText(message, kind = '') {
  const item = el('connection');
  // The controller's normal state requires no explanatory badge. Show a
  // status only when connecting, awaiting approval or encountering a problem.
  const display = message && message !== 'Connected';
  item.textContent = display ? message : '';
  item.hidden = !display;
  item.className = `connection ${kind}`;
}
function note(message) { el('notice').textContent = message; }

async function send(command, fields = {}) {
  if (!approved) return;
  try {
    const response = await fetch('/api/action', {
      method:'POST', credentials:'same-origin', cache:'no-store',
      headers:{'Content-Type':'application/json','X-Athena-Presenter':'1'},
      body:JSON.stringify({command,...fields})
    });
    if (response.status === 401 || response.status === 403) {
      approved = false;
      connected = false;
      note('Presenter permission is no longer valid.');
    } else if (!response.ok && response.status !== 429) {
      note('ATHENA rejected the command.');
    } else note('');
  } catch (_) { statusText('Disconnected','error'); }
}

async function connect() {
  try {
    const response = await fetch('/api/connect', {
      method:'POST', credentials:'same-origin', cache:'no-store',
      headers:{'Content-Type':'application/json','X-Athena-Presenter':'1'},
      body:JSON.stringify({device:navigator.userAgentData?.platform || navigator.platform || 'Browser'})
    });
    const result = await response.json();
    connected = response.ok;
    if (result.state === 'pending') {
      statusText('Awaiting ATHENA approval');
      note('A confirmation request is open on the computer running ATHENA.');
    } else if (result.state === 'denied') {
      statusText('Access denied','error');
      note('This device was not approved. Re-enable the controller in Preferences to request again.');
    } else if (result.state === 'approved') {
      approved = true;
      statusText('Connected','ready'); note('');
    } else {
      statusText('Unavailable','error');
      note(result.error || 'Unable to connect.');
    }
  } catch (_) {
    connected = false;
    approved = false;
    statusText('Disconnected','error');
    note('The presenter service is unreachable. Check its listening address and network.');
  }
}

function showClock(seconds) {
  const value = Math.max(0,Math.floor(Number(seconds) || 0));
  const h = Math.floor(value/3600);
  const m = Math.floor((value%3600)/60);
  const s = value%60;
  return (h ? `${String(h).padStart(2,'0')}:` : '') +
    `${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
}

async function loadPicture(which, revision, allowed) {
  if (!allowed) return;
  try {
    // Load the protected image directly from our own origin. It carries the
    // admitted HttpOnly cookie automatically and satisfies img-src 'self'.
    // Avoid blob: URLs entirely: the old ATHENA CSP omitted blob: and Chrome
    // would fetch valid PNGs yet refuse to display them.
    const picture = new Image();
    picture.id = `${which}Image`;
    picture.alt = el(picture.id).alt;
    picture.draggable = false;
    picture.src = `/api/${which==='current'?'current':'next'}.png?version=${encodeURIComponent(revision)}`;
    // Decode off-DOM, then swap the already-decoded element in one operation.
    // Keep the previous raster intact if the request fails or becomes stale.
    await picture.decode();
    if (lastRevision !== revision) return;
    picture.classList.add('visible');
    el(picture.id).replaceWith(picture);
    el(which+'Placeholder').classList.add('hidden');
  } catch (_) { /* Preserve the previous raster until the next one arrives. */ }
}

function updateControls() {
  const active = approved && state.active;
  buttons.forEach(id => {el(id).disabled = !approved || (!state.active && id !== 'start');});
  el('start').disabled = !approved || state.active;
  el('first').disabled = !active || state.page <= 1;
  el('previous').disabled = !active || state.page <= 1;
  el('last').disabled = !active || state.page >= state.count;
  el('jumpPage').disabled = !active;
  el('seek').disabled = !active;
  el('seek').min = '1';
  el('seek').max = String(Math.max(1,state.count));
  el('seek').value = String(Math.max(1,state.page));
  el('fraction').textContent = active ? `${state.page} / ${state.count}` : '— / —';
  el('currentNumber').textContent = active ? `${state.page} / ${state.count}` : '—';
  el('nextNumber').textContent = active && state.page < state.count ? `${state.page+1}` : '—';
  el('slideLabel').textContent = !active ? 'Waiting for slideshow' :
    state.finished ? 'End of slideshow' : `Slide ${state.page} of ${state.count}`;
  el('elapsed').textContent = showClock(state.elapsedSeconds);
  el('timerState').textContent = state.timerPaused ? 'Timer paused' : 'Elapsed';
  el('timerPause').textContent = state.timerPaused ? '▶ Resume timer' : '⏸ Pause timer';
  el('black').textContent = state.black ? '□ Show slide' : '⬛ Black screen';
  el('black').classList.toggle('active',Boolean(state.black));
  el('laser').classList.toggle('active',laserEnabled);
  el('pointerSurface').classList.toggle('laser-mode',laserEnabled && active);
}

async function refresh() {
  try {
    if (!connected) await connect();
    if (!connected) { updateControls(); return; }
    const response = await fetch('/api/status', {cache:'no-store',credentials:'same-origin'});
    const result = await response.json();
    if (response.ok && (result.state === 'pending' || result.state === 'denied')) {
      approved = false;
      statusText(result.state === 'pending' ? 'Awaiting ATHENA approval' : 'Access denied',
                 result.state === 'pending' ? '' : 'error');
      note(result.state === 'pending' ?
        'A confirmation request is open on the computer running ATHENA.' :
        'ATHENA denied this device access.');
      updateControls();
      return;
    }
    if (!response.ok) {
      approved = false;
      if (response.status === 401) connected = false;
      statusText(result.state === 'pending' ? 'Awaiting ATHENA approval' :
                 result.state === 'denied' ? 'Access denied' : 'Disconnected',
                 result.state === 'pending' ? '' : 'error');
      updateControls(); return;
    }
    approved = true;
    statusText('Connected','ready');
    note('');
    state = result;
    updateControls();
    if (result.revision !== lastRevision) {
      lastRevision = result.revision;
      if (!result.active || !result.hasCurrent) clearPicture('current');
      if (!result.active || !result.hasNext) clearPicture('next');
      if (result.active && result.hasCurrent)
        loadPicture('current',lastRevision,true);
      if (result.active && result.hasNext)
        loadPicture('next',lastRevision,true);
    }
  } catch (_) {
    connected = false;
    approved = false;
    statusText('Disconnected','error');
    note('Connection lost — reconnecting automatically.');
    updateControls();
  }
}

for (const id of ['first','previous','next','last'])
  el(id).addEventListener('click', () => send(id));
el('black').addEventListener('click', () => send('black'));
el('start').addEventListener('click', () => send('start'));
el('stop').addEventListener('click', () => send('end'));
el('timerPause').addEventListener('click', () => send('timer-pause'));
el('timerReset').addEventListener('click', () => send('timer-reset'));
el('seek').addEventListener('change', () => send('jump',{page:Number(el('seek').value)}));
el('jumpForm').addEventListener('submit', event => {
  event.preventDefault();
  const page = Number(el('jumpPage').value);
  if (Number.isInteger(page) && page>=1 && page<=state.count) {
    send('jump',{page}); el('jumpPage').blur();
  }
});
el('laser').addEventListener('click', () => {
  laserEnabled = !laserEnabled;
  if (!laserEnabled) {
    el('laserCursor').hidden = true;
    send('pointer-hide');
  }
  updateControls();
});

function pointLaser(event) {
  if (!laserEnabled || !approved || !state.active) return;
  const rect = el('pointerSurface').getBoundingClientRect();
  let x = (event.clientX-rect.left)/rect.width;
  let y = (event.clientY-rect.top)/rect.height;
  x = Math.min(1,Math.max(0,x)); y = Math.min(1,Math.max(0,y));
  const cursor = el('laserCursor');
  cursor.hidden = false;
  cursor.style.left = `${100*x}%`; cursor.style.top = `${100*y}%`;
  const now = performance.now();
  if (now-lastPointerSent>65) {
    lastPointerSent=now;
    send('pointer',{x,y});
  }
}
el('pointerSurface').addEventListener('pointerdown', event => {
  if (laserEnabled) {
    event.preventDefault();
    el('pointerSurface').setPointerCapture(event.pointerId);
    pointLaser(event);
  }
});
el('pointerSurface').addEventListener('pointermove', pointLaser);
el('pointerSurface').addEventListener('pointerup', () => {
  if (laserEnabled) {el('laserCursor').hidden = true; send('pointer-hide');}
});
el('pointerSurface').addEventListener('pointercancel', () => {
  if (laserEnabled) {el('laserCursor').hidden = true; send('pointer-hide');}
});

document.addEventListener('keydown', event => {
  if (['INPUT','TEXTAREA'].includes(document.activeElement?.tagName)) return;
  const map = {
    ArrowRight:'next',ArrowDown:'next',PageDown:'next',
    ArrowLeft:'previous',ArrowUp:'previous',PageUp:'previous',
    Home:'first',End:'last',' ':'next',Enter:'next'
  };
  if (event.key==='Escape') {event.preventDefault();send('end');}
  else if (map[event.key]) {event.preventDefault();send(map[event.key]);}
  else if (event.key.toLowerCase()==='b') {event.preventDefault();send('black');}
});

if ('serviceWorker' in navigator && window.isSecureContext)
  navigator.serviceWorker.register('/service-worker.js').catch(()=>{});
if (!window.isSecureContext)
  el('installHint').textContent = 'For installable PWA support on a phone, serve over HTTPS with a certificate trusted by that phone.';
setInterval(refresh,900);
refresh();
