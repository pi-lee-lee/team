/**
 * 브라우저 종단 시험 — **진짜 크롬**으로 시험 인스턴스에 붙어 확인한다.
 *
 * 이 파일이 존재하는 이유: 시연 경로에서 브라우저→서버→장치 구간만 한 번도 실측된 적이 없었다.
 * 지금까지의 검증은 전부 "코드를 읽어서" 였다. 그건 오늘로 끝낸다.
 *
 * 🔴 안전 규칙 — 이 스크립트는 **운영 서버에 절대 붙지 않는다.**
 *    1) 대상 포트를 인자로 받고 기본값이 없다(운영 9900 을 실수로 집을 수 없다)
 *    2) 9900/9991/5500 을 대상으로 주면 **거부하고 끝낸다**
 *    3) 첫 단계에서 실제로 열린 WebSocket URL 을 CDP 로 확인해 대상과 다르면 즉시 중단
 *
 * 사용: node web/tools/e2e.mjs --port 10000 [--head]
 */
import { launch, evaluate, waitFor, sleep, freePort } from './cdp.mjs';
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';

/* ── 인자 ─────────────────────────────────────────────────────── */
const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const PORT = arg('--port', null);
const HEAD = argv.includes('--head');
/* 🔴 로그 경로는 **포트에서 유도한다**(10500 → 오프셋 +600 → `test+600.log`).
   전에는 `test+100` 이 기본값이었는데, +600 인스턴스에 붙였을 때 **엉뚱한 파일을 조용히 읽어**
   "서버가 R 을 안 내보냈다"는 거짓 실패를 냈다(2026-08-17 실측). 예약은 멀쩡히 성공했고
   화면도 "내 예약"이었는데 로그만 다른 데를 본 것이다.
   ⚠ 이건 socket 이 고친 "기본값이 운영 포트" 사고와 같은 형태다 — **기본값이 딴 데를 가리키면
   도구가 조용히 틀린 답을 낸다.** 그래서 기본값을 없애는 대신 **대상에서 유도**한다. */
const LOG = arg('--log', process.env.HOME + '/parking-logs/parking-server.test+' + (Number(PORT) - 9900) + '.log');
const OUT = new URL('../artifacts/', import.meta.url);

if (!PORT) { console.error('--port 를 반드시 줘라 (기본값 없음: 운영 포트를 실수로 집지 않게 하려는 것)'); process.exit(2); }
const FORBIDDEN = ['9900', '9991', '5500'];
if (FORBIDDEN.includes(String(PORT))) {
  console.error('🔴 ' + PORT + ' 는 운영 포트다. 이 스크립트는 시험 인스턴스에만 붙는다. 중단.');
  process.exit(2);
}
const BASE = 'http://127.0.0.1:' + PORT + '/';
mkdirSync(OUT, { recursive: true });

/* ── 결과 집계 ─────────────────────────────────────────────────── */
const results = [];
let failed = 0;
function check(name, cond, detail) {
  const okv = !!cond;
  if (!okv) failed++;
  results.push({ name, ok: okv, detail: detail === undefined ? '' : String(detail) });
  console.log('  ' + (okv ? '✅' : '❌') + ' ' + name + (detail !== undefined ? '  → ' + detail : ''));
  return okv;
}
function note(text) { results.push({ note: text }); console.log('  · ' + text); }

/* ── 클릭: 진짜 마우스 이벤트 ───────────────────────────────────── */
async function click(client, selector) {
  const box = await evaluate(client, `(() => {
    const e = document.querySelector(${JSON.stringify(selector)});
    if (!e) return null;
    e.scrollIntoView({block:'center'});
    const r = e.getBoundingClientRect();
    return { x: r.left + r.width/2, y: r.top + r.height/2, w: r.width, h: r.height };
  })()`);
  if (!box) throw new Error('선택자를 찾지 못했다: ' + selector);
  if (box.w === 0 || box.h === 0) throw new Error('요소가 화면에 없다(크기 0): ' + selector);
  const p = { x: Math.round(box.x), y: Math.round(box.y), button: 'left', clickCount: 1 };
  await client.send('Input.dispatchMouseEvent', { type: 'mouseMoved', ...p });
  await client.send('Input.dispatchMouseEvent', { type: 'mousePressed', ...p });
  await client.send('Input.dispatchMouseEvent', { type: 'mouseReleased', ...p });
}

/**
 * 이동 후 **DOM 이 실제로 준비될 때까지** 기다린다.
 * ⚠ `Page.loadEventFired` 만 믿으면 안 된다 — 직전 문서의 load 를 잡을 수 있고,
 *   그러면 다음 evaluate 가 아직 없는 노드를 읽어 null 예외로 죽는다(실제로 겪었다).
 */
async function goto(client, url) {
  await client.send('Page.navigate', { url });
  try {
    await waitFor(client,
      `document.readyState === 'complete' && !!document.getElementById('conn-text') && !!document.querySelector('.tile')`,
      { what: '문서 준비 (' + url + ')', timeout: 15000 });
  } catch (e) {
    // 실패했을 때 **무엇이 안 됐는지** 말하게 한다. "false" 하나로는 아무것도 못 고친다.
    const diag = await evaluate(client, `({
      href: location.href, ready: document.readyState,
      conn: !!document.getElementById('conn-text'), tiles: document.querySelectorAll('.tile').length,
      title: document.title, body: (document.body ? document.body.textContent.slice(0, 200) : '(body 없음)')
    })`).catch(err => ({ evalError: err.message }));
    throw new Error(e.message + '\n      진단: ' + JSON.stringify(diag));
  }
}

/** 칸의 상태 문구. 없으면 빈 문자열. */
const stateOf = (slot) => `(() => { const b = document.querySelector('.tile[data-slot="${slot}"]');
  return b ? b.querySelector('.tile__state').textContent : ''; })()`;

async function shot(client, name, { full = false } = {}) {
  try {
    // full=true 는 뷰포트 아래까지 담는다. 장치 패널(신선도 표시)이 접혀 있어 기본 촬영에는 안 잡힌다.
    const { data } = await client.send('Page.captureScreenshot',
      full ? { format: 'png', captureBeyondViewport: true } : { format: 'png' });
    const f = new URL(name + '.png', OUT);
    writeFileSync(f, Buffer.from(data, 'base64'));
    note('스크린샷 web/artifacts/' + name + '.png');
  } catch (e) { note('스크린샷 실패: ' + e.message); }
}

/** 서버 로그의 마지막 N줄 — 우리가 만든 왕복이 실제로 전선에 나갔는지 본다. */
function tailLog(n = 40) {
  try { return readFileSync(LOG, 'utf8').split('\n').slice(-n); }
  catch (e) { return ['(로그를 읽지 못했다: ' + e.message + ')']; }
}

/* ── 본체 ─────────────────────────────────────────────────────── */
const client = await launch({ headless: !HEAD });
const wsFrames = [];      // 받은 WS 프레임 원문
const wsCreated = [];     // 페이지가 실제로 연 WebSocket URL
const consoleErrs = [];

client.on((method, p) => {
  if (method === 'Network.webSocketCreated') wsCreated.push(p.url);
  if (method === 'Network.webSocketFrameReceived') wsFrames.push({ dir: 'rx', t: Date.now(), data: p.response?.payloadData || '' });
  if (method === 'Network.webSocketFrameSent') wsFrames.push({ dir: 'tx', t: Date.now(), data: p.response?.payloadData || '' });
  if (method === 'Runtime.consoleAPICalled' && (p.type === 'error' || p.type === 'warning')) {
    consoleErrs.push(p.args?.map(a => a.value ?? a.description).join(' '));
  }
  if (method === 'Runtime.exceptionThrown') {
    consoleErrs.push('예외: ' + (p.exceptionDetails?.exception?.description || p.exceptionDetails?.text));
  }
});

await client.send('Page.enable');
await client.send('Network.enable');
await client.send('Runtime.enable');
await client.send('Log.enable');

try {
  /* ── 1. 접속 대상 확인 (가장 먼저 — 여기서 틀리면 나머지는 볼 필요도 없다) ── */
  console.log('\n[1] 🔴 관문 — 페이지가 실제로 어느 서버에 붙는가');
  await goto(client, BASE);
  await sleep(1200);   // WS 가 붙고 첫 스냅샷이 올 여유

  const want = 'ws://127.0.0.1:' + PORT + '/ws';
  check('페이지가 연 WebSocket 은 정확히 하나', wsCreated.length === 1, JSON.stringify(wsCreated));
  check('그 대상이 시험 인스턴스다 (' + want + ')', wsCreated[0] === want, wsCreated[0]);
  const leak = wsCreated.filter(u => FORBIDDEN.some(p => u.includes(':' + p)));
  if (!check('🔴 운영 포트로 새지 않았다', leak.length === 0, JSON.stringify(leak))) {
    throw new Error('운영 포트 접촉 감지 — 즉시 중단한다');
  }
  check('푸터가 실제 접속 대상을 말한다',
        (await evaluate(client, `document.getElementById('foot-ws').textContent`)) === want,
        await evaluate(client, `document.getElementById('foot-ws').textContent`));

  /* ── 2. WS 경로로 화면이 살아나는가 ── */
  console.log('\n[2] WS 경로 — 서버 상태가 화면에 그려지는가');
  await waitFor(client, `document.getElementById('conn-text').textContent.includes('WebSocket 연결됨')`,
                { what: 'WS 연결 표시' });
  check('연결 상태가 WebSocket 연결됨', true,
        await evaluate(client, `document.getElementById('conn-text').textContent`));

  const cells = await evaluate(client, `[...document.querySelectorAll('.tile')].map(b => ({
    slot: b.dataset.slot,
    state: b.querySelector('.tile__state').textContent,
    meta: b.querySelector('.tile__meta').textContent
  }))`);
  check('10칸이 그려졌다', cells.length === 10, cells.length);
  check('칸이 "상태 미상"에서 벗어났다 (서버 값이 실제로 반영됐다)',
        cells.every(c => c.state !== '상태 미상'), JSON.stringify(cells.slice(0, 3)));
  const devId = await evaluate(client, `document.getElementById('dev-id').textContent`);
  check('장치 ID 가 표시된다', devId && devId !== '—', devId);
  /* 결손 배너는 **정상 운영에서 침묵해야 한다.** 늘 떠 있는 경고는 아무도 안 읽는다. */
  check('자리 결손 배너가 뜨지 않는다 (서버가 10칸을 다 싣고 있다)',
        await evaluate(client, `document.getElementById('slots-banner').hidden === true`));
  await shot(client, '1-ws-connected');

  /* ── 3. 🔴 예약 왕복 — 브라우저 → 서버 → 장치 → 화면 ── */
  console.log('\n[3] 🔴 예약 왕복 — 이 구간이 오늘의 목적이다');
  const target = (cells.find(c => c.state.includes('빈자리')) || cells[0]).slot;
  note('대상 칸 ' + target);
  const logBefore = tailLog(200).length;
  const user = await evaluate(client, `document.getElementById('user-id').textContent`);
  note('브라우저 사용자 ' + user);

  await click(client, `.tile[data-slot="${target}"]`);
  await waitFor(client, `document.getElementById('confirm-dialog').open === true`, { what: '확인 대화상자' });
  check('확인 대화상자가 열렸다 (키보드 접근 가능한 <dialog>)', true,
        await evaluate(client, `document.getElementById('confirm-title').textContent`));
  await shot(client, '2-confirm-dialog');

  const tSend = Date.now();
  await click(client, '#confirm-dialog button[value="ok"]');

  /* 화면이 먼저 낙관적으로 바뀌고, 그다음 서버 ACK 로 확정된다 (§8).
     ⚠ 여기서 "상태 문구"를 그대로 기다리면 안 된다 — 빈 문자열이 아닌 모든 값이 참이라
     바뀌기 전 값("빈 자리")에 즉시 걸린다. 반드시 **불리언**을 기다려라. */
  await waitFor(client, `/예약/.test(${stateOf(target)})`,
                { what: target + ' 칸이 예약으로 바뀜', timeout: 20000 });
  const dtScreen = Date.now() - tSend;
  const settled = await evaluate(client, stateOf(target));

  const sent = wsFrames.filter(f => f.dir === 'tx').map(f => f.data);
  check('브라우저가 예약 요청을 전선으로 보냈다',
        sent.some(s => s.includes('reserve') && s.includes(target)), JSON.stringify(sent.slice(-2)));

  await sleep(1500);   // 장치 ACK 가 로그에 찍힐 여유
  const tail = tailLog(60).join('\n');
  check('서버가 장치로 R 을 내보냈다 (→ARD R,…,' + target + ')',
        new RegExp('→ARD R,\\d+,' + target).test(tail),
        (tail.match(new RegExp('→ARD R,[^\\n]*')) || ['없음'])[0]);
  check('장치가 A 로 응답했다 (←ARD A)',
        /←ARD A,\d+,/.test(tail), (tail.match(/←ARD A,[^\n]*/g) || ['없음']).slice(-1)[0]);

  const acked = wsFrames.filter(f => f.dir === 'rx' && f.data.includes('"ack"')).map(f => f.data);
  check('서버 ACK 가 브라우저까지 돌아왔다', acked.length > 0, acked.slice(-1)[0]);
  check('🔴 화면이 예약을 반영했다 (' + dtScreen + 'ms)',
        /예약/.test(settled), settled);
  await shot(client, '3-reserved');

  /* ── 4. 취소로 되돌린다 — 시험이 상태를 남기면 다음 사람이 오독한다 ── */
  console.log('\n[4] 취소 — 보드를 원래대로');
  await click(client, `.tile[data-slot="${target}"]`);
  await waitFor(client, `document.getElementById('confirm-dialog').open === true`, { what: '취소 확인 대화상자' });
  const dlgTitle = await evaluate(client, `document.getElementById('confirm-title').textContent`);
  check('같은 칸을 다시 누르면 취소를 묻는다 (예약이 화면에 실제로 잡혀 있다는 뜻)',
        /취소/.test(dlgTitle), dlgTitle);
  await click(client, '#confirm-dialog button[value="ok"]');
  const back = await waitFor(client, `!/예약/.test(${stateOf(target)})`,
    { what: target + ' 예약 해제', timeout: 20000 }).then(() => evaluate(client, stateOf(target)))
    .catch(e => '(실패: ' + e.message + ')');
  check('예약이 취소돼 원래 상태로 돌아왔다', !/실패/.test(back), back);

  /* ── 5. last_frame_ts — REQ-0132 가 들어왔는지 확인한다 ──
     🔴 2026-08-16 23:5x · web-engineer — **극성을 뒤집었다.**
     이 단계는 원래 "키가 **없다**"를 단언했다(그때는 그것이 사실이었고, 결함을 고정해서
     REQ 의 근거로 삼으려던 것이다). socket 이 REQ-0132 로 고친 뒤에는 같은 단언이
     **고쳐졌다는 이유로 빨간불**이 된다 — 하니스가 "회귀했다, 다시 열어라"라고 거짓말을 한다.
     실제로 socket 이 "[5] 가 초록이 되는지 확인해 달라"고 했고, 뒤집지 않고 돌렸다면
     멀쩡한 수정을 되돌리라고 요구할 뻔했다.
     고치기 전의 사실은 REQ-0132 와 LEDGER §2.1 · web/artifacts/7-freshness-unknown.png 에 남아 있다.
     교훈: **결함의 존재를 단언하는 검사는 고쳐지는 순간 거짓 경보가 된다.** 결함을 고정할
     때는 "고쳐지면 초록"이 되게 쓴다([10] 이 그렇게 돼 있어서 그쪽은 손댈 것이 없었다). */
  console.log('\n[5] WS 스냅샷의 last_frame_ts — REQ-0132 (실측)');
  const snaps = wsFrames.filter(f => f.dir === 'rx' && f.data.includes('"snapshot"'));
  check('스냅샷 프레임을 받았다', snaps.length > 0, snaps.length + '개');
  if (snaps.length) {
    const j = JSON.parse(snaps[snaps.length - 1].data);
    const dev = j.device || {};
    const keys = Object.keys(dev);
    note('device 키: ' + JSON.stringify(keys));
    check('device.last_frame_ts 가 WS 스냅샷에 있다 (REQ-0132)',
          'last_frame_ts' in dev, JSON.stringify(keys));
    const lf = dev.last_frame_ts;
    /* 계약은 셋이다: 키 이름 · epoch **밀리초** · 무프레임이면 null(0 이 아니다).
       0 을 받으면 화면이 1970년으로부터의 나이를 그린다 — 그래서 값의 형태까지 검사한다.
       13자리 하한(2001-09-09)만 본다. 상한을 두면 하니스가 미래에 저절로 썩는다. */
    check('값이 null 이거나 epoch ms 다 (0 이 아니다)',
          lf === null || (typeof lf === 'number' && lf > 1e12), JSON.stringify(lf));
    writeFileSync(new URL('ws-snapshot.json', OUT), JSON.stringify(j, null, 2));
    note('스냅샷 원문 web/artifacts/ws-snapshot.json');

    /* 파일 경로의 같은 키와 나란히 놓는다 — **차이가 나는 것이 정상**이다.
       server.cpp:1340 이 last_frame_ts 를 기록 키에서 일부러 뺐다(넣으면 초당 한 번 쓴다).
       즉 파일은 **상태가 바뀔 때만** 다시 써지고, 그 사이 파일의 last_frame_ts 는 얼어 있다.
       WS 는 프레임마다 갱신된다. 같은 키·같은 단위인데 **신선도가 다르다.**
       socket 의 REQ-0132 실측에서 둘이 30.187초 벌어져 있던 것이 이 이유로 설명된다.
       여기서는 단언하지 않고 값을 남긴다 — 간격의 크기는 그 순간 상태 변화 여부에 달렸다. */
    let fileLf = '(못 읽음)';
    try {
      const arr = await (await fetch(BASE.slice(0, -1) + '/data_log.json')).json();
      const last = Array.isArray(arr) ? arr[arr.length - 1] : arr;
      fileLf = last && last.device ? last.device.last_frame_ts : '(device 없음)';
      if (typeof lf === 'number' && typeof fileLf === 'number')
        note('WS 와 파일의 last_frame_ts 간격 = ' + (lf - fileLf) + 'ms  '
             + '(파일은 상태 변화 때만 다시 써진다 — server.cpp:1340. 벌어지는 것이 정상)');
    } catch (e) { fileLf = '(' + e.message + ')'; }
    note('WS last_frame_ts = ' + JSON.stringify(lf) + ' · 파일 = ' + JSON.stringify(fileLf));

    const shown = await evaluate(client, `document.getElementById('dev-frame').textContent`);
    note('화면의 "마지막 프레임" 표시 = ' + JSON.stringify(shown));
    /* 서버가 값을 줬는데 화면이 여전히 "알 수 없음"이면 고장은 이제 **내 쪽**이다.
       값이 null 인 경우(장치를 한 번도 못 본 인스턴스)는 "알 수 없음"이 정답이므로 뺀다. */
    if (typeof lf === 'number')
      check('화면이 실제 시각을 그린다 ("알 수 없음" 이 아니다)',
            !/알 수 없음/.test(shown), JSON.stringify(shown));
    await shot(client, '7-freshness', { full: true });
  }

  /* ── 6. 🔴 살아 있던 WS 가 끊어지는 순간 — 전환 그 자체 ──
     [7] 은 "처음부터 WS 가 안 되는" 경우다. 그건 전환이 아니라 초기 상태다.
     시연에서 실제로 무서운 것은 **잘 되던 화면이 끊기는 순간**이므로 그것을 따로 잰다. */
  console.log('\n[6] 🔴 WS → 폴백 전환 — 잘 되던 소켓이 끊어지면');
  await waitFor(client, `document.getElementById('conn-text').textContent.includes('WebSocket 연결됨')`,
                { what: '전환 시험 전 WS 연결 확인' });
  const tCut = Date.now();
  await evaluate(client, `(() => { transport.ws.close(); return true; })()`);   // 소켓만 끊는다
  await waitFor(client, `/파일 폴백|연결 끊김/.test(document.getElementById('conn-text').textContent)`,
                { what: '끊김 인지', timeout: 15000 });
  check('끊김을 화면이 즉시 말한다 (' + (Date.now() - tCut) + 'ms)', true,
        await evaluate(client, `document.getElementById('conn-text').textContent`));
  check('전환을 알림으로도 말한다',
        (await evaluate(client, `document.getElementById('messages').textContent`)).includes('폴백'),
        await evaluate(client, `document.getElementById('messages').textContent.slice(0, 80)`));
  const keep = await waitFor(client,
    `[...document.querySelectorAll('.tile')].every(b => b.querySelector('.tile__state').textContent !== '상태 미상')`,
    { what: '끊긴 뒤에도 칸이 상태를 유지', timeout: 15000 }).catch(() => false);
  check('🔴 끊긴 뒤에도 화면이 비지 않는다 (폴백이 이어받는다)', keep === true, keep);
  await shot(client, '6-ws-cut');
  // 저절로 다시 붙는가 — 재연결은 §7.3 의 약속이다
  const reconnected = await waitFor(client, `document.getElementById('conn-text').textContent.includes('WebSocket 연결됨')`,
    { what: '자동 재연결', timeout: 30000 }).catch(() => false);
  check('끊긴 소켓이 저절로 다시 붙는다', reconnected === true,
        reconnected === true ? '재연결됨' : '30초 안에 못 붙었다');

  /* ── 7. 폴백 폴링 경로 (처음부터 WS 가 없는 경우) ── */
  console.log('\n[7] 폴백 — WS 가 처음부터 안 될 때 파일 폴링이 화면을 채우는가');
  const dead = await freePort();          // 아무도 안 듣는 포트 (운영 포트가 아님을 보장)
  check('폴백 시험용 죽은 포트가 운영 포트가 아니다', !FORBIDDEN.includes(String(dead)), dead);
  wsCreated.length = 0;
  /* ⚠ 예전에는 `BASE + '?ws=...'` 를 쓰면 서버가 404 를 줬다(아래 **[10]** — [9] 가 아니다.
     REQ-0132 본문에서 내가 이 번호를 잘못 적었고 socket 도 그대로 받아 적었다).
     REQ-0132 로 고쳐졌지만 **운영에 올라간 빌드가 무엇인지는 여기서 알 수 없으므로**
     `/index.html` 을 명시하는 이 형태를 유지한다 — 옛 빌드에서도 통하는 유일한 주소다. */
  await goto(client, BASE + 'index.html?ws=' + dead);
  await waitFor(client, `document.getElementById('conn-text').textContent.includes('파일 폴백')`,
                { what: '파일 폴백 전환', timeout: 20000 });
  check('WS 실패 후 파일 폴백으로 넘어갔다', true,
        await evaluate(client, `document.getElementById('conn-text').textContent`));
  const fcells = await evaluate(client, `[...document.querySelectorAll('.tile')].map(b => b.querySelector('.tile__state').textContent)`);
  check('폴백 경로에서도 10칸이 상태를 말한다', fcells.every(s => s !== '상태 미상'), JSON.stringify(fcells.slice(0, 3)));
  check('포트 불일치 경고가 떴다 (WS 와 폴백이 다른 서버를 가리킬 수 있는 상태)',
        (await evaluate(client, `document.getElementById('messages').textContent`)).includes('받았는데'),
        await evaluate(client, `document.getElementById('messages').textContent.slice(0,120)`));
  const src = await evaluate(client, `document.getElementById('dev-source').textContent`);
  note('출처 표시 = ' + JSON.stringify(src));
  await shot(client, '4-fallback');

  /* ── 7. REQ-0122 가 사람 눈에 남겨 둔 데모 검증 ── */
  console.log('\n[8] 데모 — REQ-0122 가 "사람이 눈으로 볼 것"으로 남긴 버튼');
  await goto(client, BASE + 'index.html?demo=1');
  await sleep(500);
  const btnSel = await evaluate(client, `(() => {
    const b = [...document.querySelectorAll('button')].find(x => /개정 4 폴백/.test(x.textContent));
    if (!b) return null;
    b.id = b.id || 'e2e-demo-btn';
    return '#' + b.id;
  })()`);
  if (check('§9.1 개정 4 폴백+무장 데모 버튼이 있다', !!btnSel, btnSel)) {
    await click(client, btnSel);
    await sleep(400);
    const armed = await evaluate(client, `document.body.classList.contains('is-armed')`);
    const banner = await evaluate(client, `document.getElementById('test-banner').textContent`);
    const flags = await evaluate(client, `[...document.querySelectorAll('.tile')]
      .filter(b => !b.querySelector('.tile__flag').hidden).map(b => b.dataset.slot)`);
    check('무장 표시가 켜졌다', armed || /무장/.test(banner), JSON.stringify(banner.slice(0, 60)));
    check('주입된 칸에 ⚑ 주입값 빗금이 보인다 (A3 기대)', flags.includes('A3'), JSON.stringify(flags));
    await shot(client, '5-demo-armed');
  }

  /* ── 9. 콘솔 오류 ── */
  console.log('\n[9] 콘솔');
  check('페이지 예외·오류 없음', consoleErrs.length === 0, JSON.stringify(consoleErrs.slice(0, 3)));

  /* ── 10. 서버 정적 서빙 — 브라우저 시험이 찾아낸 결함을 고정한다 ── */
  console.log('\n[10] 서버 정적 서빙 — 루트 경로에 쿼리를 붙이면?');
  const code = async (p) => (await fetch(BASE.slice(0, -1) + p)).status;
  const cRoot = await code('/'), cFile = await code('/index.html?demo=1'), cRootQ = await code('/?demo=1');
  note('GET /              → ' + cRoot);
  note('GET /index.html?x  → ' + cFile);
  note('GET /?x            → ' + cRootQ);
  check('GET / 는 200', cRoot === 200, cRoot);
  check('GET /index.html?x 는 200', cFile === 200, cFile);
  /* 이 단언은 **일부러 "고쳐지면 실패"하게 두지 않았다** — 결함이 있는 동안은 빨간불이고,
     socket 이 고치면 저절로 초록이 된다. REQ-0132 로 실제로 그렇게 됐다(server.cpp:1881-1883).
     ⚠ [5] 는 반대로 써 있어서 고쳐지는 순간 거짓 경보가 됐다. 이쪽이 옳은 형태다.
     이제 이 줄은 **회귀 감시**다 — 다시 404 가 되면 여기가 잡는다.
     🔴 200/404 는 **빌드 판별자**이기도 하다: 붙은 서버가 REQ-0132 이전인지 이후인지 이 한 줄로 갈린다. */
  check('GET /?x 도 200 (REQ-0132 이후. 404 면 옛 빌드이거나 회귀다)',
        cRootQ === 200, cRootQ + ' · 404 라면: path=="/" 판정이 쿼리 제거보다 먼저인 옛 serve_file');

} catch (e) {
  failed++;
  console.log('\n💥 중단: ' + e.message);
  results.push({ name: '실행 중 예외', ok: false, detail: e.message });
  await shot(client, 'error');
} finally {
  writeFileSync(new URL('e2e-results.json', OUT), JSON.stringify({ base: BASE, results }, null, 2));
  writeFileSync(new URL('ws-frames.log', OUT),
    wsFrames.map(f => f.dir + ' ' + new Date(f.t).toISOString() + ' ' + f.data).join('\n'));
  await client.close();
}

console.log('\n' + (failed === 0 ? '통과' : '실패') + ' — ' + results.filter(r => r.ok === true).length +
            ' pass / ' + failed + ' fail\n');
process.exit(failed === 0 ? 0 : 1);
