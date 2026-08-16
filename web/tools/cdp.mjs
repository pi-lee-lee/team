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
  client.close = async () => {
    try { await client.send('Browser.close'); } catch { /* 이미 죽었으면 됐다 */ }
    try { client.ws.close(); } catch { }
    proc.kill('SIGTERM');
    await sleep(200);
    try { rmSync(profile, { recursive: true, force: true }); } catch { }
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
