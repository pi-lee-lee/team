/**
 * 최소 CDP 클라이언트 — 진짜 크롬을 띄우고 붙는다. **의존성 없음**(node 22 내장만).
 *
 * 왜 puppeteer 를 안 쓰나: 이 프로젝트의 기본값은 "의존성 없는 정적 웹"이고,
 * 검증 도구 하나 때문에 node_modules 를 들이는 것은 그 기준을 깨는 거래다.
 * node 22 는 전역 `WebSocket`·`fetch` 를 주므로 CDP 를 직접 말하는 편이 더 작다.
 *
 * 쓰는 쪽은 web/tools/e2e.mjs 다.
 */
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createServer } from 'node:net';

const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

/** 빈 포트를 커널에게 받아 온다 — 상수를 박으면 다른 세션과 부딪친다. */
export function freePort() {
  return new Promise((resolve, reject) => {
    const s = createServer();
    s.on('error', reject);
    s.listen(0, '127.0.0.1', () => {
      const p = s.address().port;
      s.close(() => resolve(p));
    });
  });
}

export const sleep = (ms) => new Promise(r => setTimeout(r, ms));

/**
 * 크롬을 띄우고 CDP 로 붙는다.
 *
 * ⚠ `--user-data-dir` 는 **매번 새 임시 디렉터리**다. 사용자 프로필을 재사용하면
 *   잠금 때문에 싸우고, 그 프로필이 기동 때 하는 일(확장·복원 탭)까지 딸려 온다.
 */
export async function launch({ headless = true } = {}) {
  const port = await freePort();
  const profile = mkdtempSync(join(tmpdir(), 'parking-e2e-'));
  const args = [
    '--remote-debugging-port=' + port,
    '--user-data-dir=' + profile,
    '--no-first-run', '--no-default-browser-check',
    '--disable-background-networking', '--disable-sync',
    '--disable-features=Translate,MediaRouter',
    'about:blank',
  ];
  if (headless) args.unshift('--headless=new');

  const proc = spawn(CHROME, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  let stderr = '';
  proc.stderr.on('data', d => { stderr += d.toString(); });

  // /json/version 이 응답할 때까지 기다린다 — 고정 sleep 은 느린 기계에서 깨진다.
  let wsUrl = null;
  for (let i = 0; i < 100; i++) {
    if (proc.exitCode !== null) throw new Error('크롬이 즉시 종료됐다 (code ' + proc.exitCode + ')\n' + stderr);
    try {
      const r = await fetch('http://127.0.0.1:' + port + '/json/version');
      const j = await r.json();
      if (j.webSocketDebuggerUrl) { wsUrl = j.webSocketDebuggerUrl; break; }
    } catch { /* 아직 안 떴다 */ }
    await sleep(100);
  }
  if (!wsUrl) throw new Error('CDP 엔드포인트가 10초 안에 안 떴다\n' + stderr);

  const client = await connect(wsUrl);

  /* 🔴🔴 **부르는 쪽이 잊어도 남지 않게 한다.**
     실측(2026-08-26 · cpp 가 잡았다): `parking-e2e-*` 프로파일 **28개** · 프로세스 **196개** 가
     넷 시간대에 걸쳐 쌓여 있었고, 실기 서버 9900 에 **localhost WS 약 30개**를 물고 있었다.
     → 서버 소크 줄의 `WS접속(9900) N` 이 **사람이 아니라 좀비**를 세고 있었다.
       그리고 연결 하나마다 `push_snapshot()` 이 나간다(config.h:235: "화면이 N 개면 N 배다").
     🔑 **원인은 `close()` 를 안 부르는 것이 아니라 `process.exit()` 이 `finally` 를 건너뛰는 것**이다 —
        도구 대부분에 `finally { close() }` 가 있었는데도 쌓였다. 임시 probe 처럼 `process.exit(0)` 으로
        끝나는 코드에서는 그 `finally` 가 **아예 안 돈다**.
     ✅ 그래서 정리를 **여기(만든 자리)** 에 건다. 부르는 쪽 열다섯을 고치는 것보다 확실하다.
     ⚠ `exit` 훅에서는 비동기를 못 쓴다 → `SIGKILL` + `rmSync` 로 **동기적으로** 끝낸다. */
  let cleaned = false;
  const reap = () => {
    if (cleaned) return;
    cleaned = true;
    try { proc.kill('SIGKILL'); } catch { }
    try { rmSync(profile, { recursive: true, force: true }); } catch { }
  };
  process.once('exit', reap);
  for (const sig of ['SIGINT', 'SIGTERM', 'SIGHUP']) {
    process.once(sig, () => { reap(); process.exit(130); });
  }

  client.close = async () => {
    try { await client.send('Browser.close'); } catch { /* 이미 죽었으면 됐다 */ }
    try { client.ws.close(); } catch { }
    proc.kill('SIGTERM');
    await sleep(200);
    reap();                                  /* 남았으면 확실히 죽이고 프로필도 지운다 */
  };
  client.debugPort = port;
  client.pid = proc.pid;
  return client;
}

/** 브라우저 타깃에 붙고, 페이지 타깃 하나를 잡아 세션을 연다. */
async function connect(wsUrl) {
  const ws = new WebSocket(wsUrl);
  await new Promise((res, rej) => {
    ws.addEventListener('open', res, { once: true });
    ws.addEventListener('error', () => rej(new Error('CDP 소켓 연결 실패')), { once: true });
  });

  let nextId = 1;
  const pending = new Map();
  const listeners = [];
  let sessionId = null;

  ws.addEventListener('message', (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) reject(new Error(msg.error.message + ' (' + JSON.stringify(msg.error) + ')'));
      else resolve(msg.result);
      return;
    }
    if (msg.method) for (const fn of listeners.slice()) fn(msg.method, msg.params || {}, msg.sessionId);
  });

  function raw(method, params = {}, sid = undefined) {
    const id = nextId++;
    const payload = { id, method, params };
    if (sid) payload.sessionId = sid;
    ws.send(JSON.stringify(payload));
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject });
      setTimeout(() => {
        if (pending.has(id)) { pending.delete(id); reject(new Error('CDP 응답 없음: ' + method)); }
      }, 30000);
    });
  }

  // 페이지 타깃을 잡는다 (about:blank 하나가 이미 있다).
  const { targetInfos } = await raw('Target.getTargets');
  let page = targetInfos.find(t => t.type === 'page');
  if (!page) {
    const c = await raw('Target.createTarget', { url: 'about:blank' });
    page = { targetId: c.targetId };
  }
  ({ sessionId } = await raw('Target.attachToTarget', { targetId: page.targetId, flatten: true }));

  const client = {
    ws,
    /** 페이지 세션으로 명령을 보낸다. */
    send: (method, params) => raw(method, params, sessionId),
    /** 이벤트 구독. 해제 함수를 돌려준다. */
    on(fn) { listeners.push(fn); return () => { const i = listeners.indexOf(fn); if (i >= 0) listeners.splice(i, 1); }; },
    /** 특정 이벤트를 기다린다. */
    once(method, { timeout = 10000, where = () => true } = {}) {
      return new Promise((resolve, reject) => {
        const off = client.on((m, p) => { if (m === method && where(p)) { off(); clearTimeout(t); resolve(p); } });
        const t = setTimeout(() => { off(); reject(new Error('이벤트를 기다리다 시간초과: ' + method)); }, timeout);
      });
    },
  };
  return client;
}

/** 페이지에서 식을 평가해 값을 돌려받는다. */
export async function evaluate(client, expression) {
  const r = await client.send('Runtime.evaluate', {
    expression, returnByValue: true, awaitPromise: true,
  });
  if (r.exceptionDetails) {
    throw new Error('페이지 예외: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
  }
  return r.result.value;
}

/**
 * 조건이 참이 될 때까지 폴링한다.
 * ⚠ 고정 sleep 대신 이걸 써라 — 1Hz 프레임과 3초 폴링이 도는 화면에서
 *   고정 대기는 "느려서 실패"와 "틀려서 실패"를 구분 못 하게 만든다.
 */
export async function waitFor(client, expression, { timeout = 15000, every = 200, what = expression } = {}) {
  const t0 = Date.now();
  let last;
  while (Date.now() - t0 < timeout) {
    last = await evaluate(client, expression);
    if (last) return last;
    await sleep(every);
  }
  throw new Error('조건이 ' + timeout + 'ms 안에 참이 되지 않았다: ' + what + ' (마지막 값 ' + JSON.stringify(last) + ')');
}

/**
 * 🔴 **현지 시각 도장** — `HH:MM:SS.mmm` (로컬). (2026-08-19)
 *
 * ~~`new Date().toISOString().slice(11,23)`~~ 을 쓰다가 **내 기록이 UTC** 로 찍혔다.
 * 서버 로그는 **현지(KST)** 다. 그래서 같은 사건이 내 기록 `20:19:17.092` ·
 * 서버 로그 `05:19:17` 로 남았고 — **분·초는 같은데 시(hour)만 어긋났다.**
 * socket 이 대조하다 잡았다.
 *
 * 🔑 **분·초가 맞아떨어지니 "같은 사건"임은 알 수 있었지만, 그건 운이다** — 사건이 1분 이상
 * 떨어져 있으면 **다른 사건으로 보이거나 순서가 뒤집혀 보인다.**
 * ⚠ 우리 원장의 *"로그에 날짜가 없으면 시:분:초를 오늘로 읽어 반드시 오독한다"* 의 이웃이다.
 * **여기서는 날짜가 아니라 시간대가 어긋났고, 결과는 같다.**
 *
 * 🔴 **정의를 한 곳에만 둔다.** 도구마다 각자 포맷하면 그게 곧 두 번째 판정자다 —
 * 하나는 UTC, 하나는 로컬로 갈리는 것이 정확히 그렇게 생긴다.
 */
export function localStamp(d) {
  const t = d || new Date();
  const p = (n, w) => String(n).padStart(w || 2, '0');
  return p(t.getHours()) + ':' + p(t.getMinutes()) + ':' + p(t.getSeconds()) + '.' + p(t.getMilliseconds(), 3);
}


/**
 * 🔴 **격자 자리를 하나씩 밟는 헬퍼** — 하니스 안에서 손으로 순회하지 말고 이것을 써라.
 *
 * 막는 함정: **칸을 클릭하면 격자가 통째로 다시 만들어진다.** 그래서
 * `for (const c of document.querySelectorAll('.zone'))` 처럼 **NodeList 를 들고 순회하면**
 * 두 번째 원소부터 **떨어진 노드**가 되고, 떨어진 노드의 `click()` 은 아무 일도 안 한다 →
 * 🔴 **패널이 앞 자리 값을 그대로 들고 있어 다음 자리 값으로 읽힌다**(거짓 빨강).
 *
 * ⚠ 나는 그 함정을 `zone-nodes.mjs` 주석에 적어 두고 **새 블록에서 다시 밟았다**(2026-08-22).
 *   🔑 **주석은 새로 쓰는 코드를 못 막는다.** 그래서 헬퍼로 옮겼다 —
 *   §"조건을 확인하는 것보다 조건이 성립할 수밖에 없게 만들어라".
 *
 * 사용(하니스에서 페이지가 뜬 뒤 한 번):
 *   await evaluate(client, ZONE_HELPERS);
 *   const out = await evaluate(client, `__eachZone((cell, panel) => ({ … }))`);
 *   → `{ 자리id: 콜백이 돌려준 것 }`
 */
export const ZONE_HELPERS = `(() => {
  const grid = () => document.getElementById('zone-grid');
  window.__eachZone = function (cb) {
    const ids = [...grid().querySelectorAll('.zone')].map((c) => c.dataset.zone);
    const out = {};
    for (const id of ids) {
      /* 🔴 매번 **다시 찾는다** — 앞의 click() 이 이 원소를 떨어뜨렸을 수 있다 */
      const cell = [...grid().querySelectorAll('.zone')].find((x) => x.dataset.zone === id);
      if (!cell) continue;
      cell.click();
      const fresh = [...grid().querySelectorAll('.zone')].find((x) => x.dataset.zone === id) || cell;
      out[id] = cb(fresh, document.getElementById('zone-detail'));
    }
    return out;
  };
  return true;
})()`;
