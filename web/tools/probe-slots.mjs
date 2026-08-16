/**
 * `slots[]` 결손을 화면이 어떻게 말하는지 **측정한다.**
 *
 * 왜 따로 있나: "데이터가 없다"와 "빈 자리다"가 화면에서 갈리는지 확인하라는 숙제인데,
 * 이건 서버가 실제로 만들어 줄 수 없는 상태다(서버는 항상 10칸을 싣는다).
 * 그래서 **수신 처리 함수에 프레임을 직접 넣는다** — 전송 계층만 건너뛰고
 * `handleServerMessage → applySnapshot → normalizeSnapshot → render` 는 진짜 경로를 탄다.
 *
 * ⚠ 여기서 보는 것은 "브라우저가 그린 DOM"이다. 다만 프레임은 **내가 주입한 것**이고
 *   서버가 보낸 것이 아니다 — 보고할 때 이 구분을 지워서는 안 된다.
 *
 * 서버에 트래픽을 만들지 않는다(`?demo=1` 은 WS 도 폴링도 열지 않는다).
 *
 * 사용: node web/tools/probe-slots.mjs --port 10000
 */
import { launch, evaluate, sleep } from './cdp.mjs';
import { writeFileSync, mkdirSync } from 'node:fs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const PORT = arg('--port', null);
if (!PORT) { console.error('--port 를 줘라'); process.exit(2); }
if (['9900', '9991', '5500'].includes(String(PORT))) { console.error('🔴 운영 포트 거부'); process.exit(2); }
const BASE = 'http://127.0.0.1:' + PORT + '/index.html?demo=1';
const OUT = new URL('../artifacts/', import.meta.url);
mkdirSync(OUT, { recursive: true });

const ALL = ['A1','A2','A3','A4','A5','B1','B2','B3','B4','B5'];
const full = ALL.map((id, i) => ({ id, occupied: i % 3 === 0 ? 1 : 0, reserved: 0, user_id: null, reserved_at: null, overridden: 0 }));

const CASES = [
  { key: 'control', title: '대조군 — 10칸 전부 실림', slots: full },
  { key: 'missing', title: 'slots 키 자체가 없다', omit: true },
  { key: 'empty',   title: 'slots: [] (빈 배열)', slots: [] },
  { key: 'partial', title: '10칸 중 3칸만 실림 (A1·A2·A3)', slots: full.slice(0, 3) },
  { key: 'bits',    title: '비트열은 있는데 slots 가 비었다', slots: [],
    extra: { occupied: '1001001001', reserved: '0000000000' } },
];

const client = await launch({ headless: true });
await client.send('Page.enable');
await client.send('Runtime.enable');
await client.send('Page.navigate', { url: BASE });
for (let i = 0; i < 100; i++) {
  const ready = await evaluate(client, `document.readyState === 'complete' && !!document.querySelector('.tile')`).catch(() => false);
  if (ready) break;
  await sleep(100);
}

const report = [];
for (const c of CASES) {
  const frame = { type: 'snapshot', ts: Date.now(), device: { online: true, device_id: 'P1', uptime: 100, seq: 100 }, test_mode: { armed: false, override_count: 0 } };
  if (!c.omit) frame.slots = c.slots;
  Object.assign(frame, c.extra || {});

  // 전송 계층만 건너뛴다. 수신 처리부터는 진짜 경로다.
  await evaluate(client, `(() => { state.link = 'reconnecting';
    handleServerMessage(${JSON.stringify(frame)}); return true; })()`);
  await sleep(250);

  /* ⚠ `b.disabled` 를 보면 안 된다. 이 화면은 일부러 `aria-disabled` 를 쓴다 —
     진짜 disabled 로 만들면 키보드·스크린리더 사용자가 그 칸에 도달조차 못 해서
     "왜 못 누르는지"를 들을 수 없다(1801-1806행). 처음에 이걸 잘못 읽어
     "disabled 가 안 먹는다"고 오판할 뻔했다. */
  const tiles = await evaluate(client, `[...document.querySelectorAll('.tile')].map(b => ({
    slot: b.dataset.slot, view: b.dataset.view,
    ariaDisabled: b.getAttribute('aria-disabled'), reason: b.dataset.reason,
    state: b.querySelector('.tile__state').textContent,
    meta: b.querySelector('.tile__meta').textContent }))`);
  const banners = await evaluate(client, `({
    offline: document.getElementById('offline-banner').hidden ? '' : document.getElementById('offline-banner').textContent.trim(),
    stale: document.getElementById('stale-banner').hidden ? '' : document.getElementById('stale-banner').textContent.trim(),
    slots: document.getElementById('slots-banner').hidden ? '' : document.getElementById('slots-banner').textContent.trim(),
    conn: document.getElementById('conn-text').textContent
  })`);

  const unknown = tiles.filter(t => t.view === 'unknown').map(t => t.slot);
  const free = tiles.filter(t => /빈 자리/.test(t.state)).map(t => t.slot);
  report.push({ case: c.key, title: c.title, unknown, free, banners, tiles });

  console.log('\n■ ' + c.title);
  console.log('   상태 미상 칸: ' + (unknown.length ? unknown.join(',') : '없음') + ' (' + unknown.length + '개)');
  console.log('   빈 자리 칸  : ' + (free.length ? free.join(',') : '없음') + ' (' + free.length + '개)');
  console.log('   첫 칸 표시  : ' + JSON.stringify(tiles[0].state + ' / ' + tiles[0].meta) + '  aria-disabled=' + tiles[0].ariaDisabled);
  console.log('   결손 배너   : ' + JSON.stringify(banners.slots || '(없음)'));
  await client.send('Page.captureScreenshot', { format: 'png' })
    .then(({ data }) => writeFileSync(new URL('slots-' + c.key + '.png', OUT), Buffer.from(data, 'base64')));
}

writeFileSync(new URL('probe-slots.json', OUT), JSON.stringify(report, null, 2));
console.log('\n원문 web/artifacts/probe-slots.json · 스크린샷 web/artifacts/slots-*.png\n');
await client.close();
