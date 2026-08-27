/**
 * 🔴 **이용자 화면(8080 · 주차위치 확인)** 을 잰다 — `서머리/server/user8080.html`.
 *
 * ⚠ **주입이다.** `file://` 로 열고 WS 대신 봉투를 직접 넣는다(서버 0 · 트래픽 0).
 *   🔑 이 화면은 **상행이 하나도 없다** — 받은 `state` 를 스스로 뒤진다. 그래서 주입으로 거의 다 잰다.
 *
 * ★ 이 도구가 있는 이유 셋:
 * ```
 * ① **관제 어휘 누출**은 눈으로 못 잡는다 — `title` 속성·툴팁까지 봐야 한다(루트 지시)
 * ② 화면 여섯(대기·입력·기다림·찾음·못찾음·번호없는점유·오류)을 사람이 매번 손으로 밟을 수 없다
 * ③ 🔴 **못 찾은 이유가 다섯 갈래**다. 넷으로 접으면 `many`(서버 결함)에 "입력을 확인하세요" 가 붙는다
 * ```
 * 사용: node web/tools/user8080.mjs            (--head 로 창을 본다)
 *       node web/tools/user8080.mjs --file <다른 html>
 *       node web/tools/user8080.mjs --live 8080   ← 🔴 실기(서버가 그 포트를 열었을 때) · 주입 0 · 클릭 0
 */
import { launch, evaluate, sleep } from './cdp.mjs';

/* 페이지 로드 **전**에 심는 감시. 🔴 늦게 걸면 첫 봉투를 놓치고, 장치가 0대면
   `state` 는 **접속 직후 한 장뿐**이라(사건 기반 · 주기 없음) 그 한 장이 곧 전부다.
   그러면 실려 있는 키를 "없다" 로 보고한다 — 📖 docs/web/LEDGER.md §5.124.
   ⚠ 전역·프로토타입에 건다. 객체 하나에 걸면 화면의 재연결이 덮는다(§5.114). */
const EARLY_HOOK = `
  window.__rx = {}; window.__rxN = {}; window.__rxLast = {}; window.__sent = [];
  (function () { const rp = JSON.parse;
    JSON.parse = function (t) { const v = rp.apply(JSON, arguments);
      if (v && typeof v === 'object' && typeof v.type === 'string') {
        window.__rxN[v.type] = (window.__rxN[v.type] || 0) + 1;
        if (!window.__rx[v.type]) window.__rx[v.type] = v;
        window.__rxLast[v.type] = v;
      } return v; };
    const rs = WebSocket.prototype.send;
    WebSocket.prototype.send = function (x) { window.__sent.push(String(x)); return rs.apply(this, arguments); };
  })();
`;

const HEAD = process.argv.includes('--head');
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
/* 🔴 `--live <포트>` — **주입하지 않는다.** 실기 서버가 실제로 보낸 봉투만 읽는다.
   주입 모드의 초록은 *"그 자료가 오면 그렇게 그린다"* 이고, 이 모드는 *"지금 실기에서 그렇게 나온다"* 다.
   ⚠ 클릭 0 · 상행 0 — 이 화면은 원래 상행이 없고, 이 도구도 아무것도 안 보낸다. */
const LIVE = (() => { const i = process.argv.indexOf('--live'); return i >= 0 ? process.argv[i + 1] : null; })();
const SECS = (() => { const i = process.argv.indexOf('--secs'); return i >= 0 ? Math.max(5, Number(process.argv[i + 1]) || 0) : 25; })();
const URL_ = LIVE
  ? ('http://127.0.0.1:' + LIVE + '/')
  : (fileArg
      ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg)).href
      : new URL('../../서머리/server/user8080.html', import.meta.url).href);

let pass = 0, fail = 0, skipped = 0;
const ok = (n, c, d) => { if (c) { pass++; console.log('  ✅ ' + n); } else { fail++; console.log('  ❌ ' + n + (d ? '\n       → ' + d : '')); } };
const skip = (n, why) => { skipped++; console.log('  ⏭ ' + n + '  → 측정 불가: ' + why); };

const NOW = Date.now();
/**
 * 🔑 **관제 봉투 그대로** 넣는다 — devid·모듈·result 가 다 실린 그것이다(이용자 화면은 그중 일부만 쓴다).
 * `unplated` 는 🔴 **번호를 모르는 점유**다(오주차): `occupied:true` 인데 `plate:null`.
 *   계약 : docs/net/SPEC-manual-plate-2026-08-25.md §4-B — 정상 ⑦ 넣는다 / 오주차 ⑧ 안 부른다 / 출차 ⑪ 지운다.
 * `blind` 는 센서를 못 읽는 자리다 — 그 자리의 `occupied` 는 값이 아니라 **잔상**이다.
 */
const STATE = (plates, opts) => {
  const o = opts || {};
  const un = o.unplated || [], blind = o.blind || [], dead = o.dead || [];
  return {
    type: 'state', srv_id: 'S', epoch: 1, ts_ms: NOW, max_per_batch: 4,
    entry: { phase: 'idle', elapsed_ms: 0, limit_ms: 0, plate: null, plate_source: null, slot: null, attempts: 0 },
    zones: [
      ...['A1', 'A2', 'A3', 'A4', 'A5'].map((id, i) => ({
        id, occupied: !!plates[id] || un.indexOf(id) >= 0, reserved: false, actions: {},
        value_state: blind.indexOf(id) >= 0 ? 'unknown' : 'known', value_known: 1, value_total: 1,
        plate: plates[id] || null, plate_source: plates[id] ? 'camera' : null,
        usable: { ok: dead.indexOf(id) < 0, reason: null, sensors_known: 1, sensors_declared: 1,
                  controls_alive: 1, controls_total: 1, dead_modules: [], offline_devices: [] },
        modules: [{ devid: 'P' + (i + 1), name: id, idx: 0, value: !!plates[id], known: true }]
      })),
      /* ⚠ 입·출구는 `plate` 키가 **없다**(socket 확인) — 화면이 그것으로 걸러야 한다 */
      { id: 'E1', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] },
      { id: 'X1', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }
    ]
  };
};
const MAP = {
  type: 'map', srv_id: 'S', epoch: 1, grid: { rows: 1, cols: 7 },
  zones: ['A1', 'A2', 'A3', 'A4', 'A5'].map((id, i) => ({
    id, kind: 'parking', cells: [[0, i]], label: (i + 1) + '번 자리',
    active: { scope: 'assembly', ok: true, reason: null }, declared: [], modules: []
  })).concat([
    { id: 'E1', kind: 'entrance', cells: [[0, 5]], label: '입구', modules: [] },
    { id: 'X1', kind: 'exit', cells: [[0, 6]], label: '출구', modules: [] }
  ])
};

const V = `(() => {
  const on = [...document.querySelectorAll('[data-view]')].filter((s) => s.dataset.on === '1');
  const t = (id) => (document.getElementById(id) || {}).textContent || '';
  const lots = [...document.querySelectorAll('.lot')].map((l) => ({
    zone: l.dataset.zone, hit: l.dataset.hit === '1', state: l.dataset.state,
    no: (l.querySelector('.lot__no') || {}).textContent,
    st: (l.querySelector('.lot__st') || {}).textContent }));
  return {
    view: on.length === 1 ? on[0].dataset.view : ('?x' + on.length),
    shown: on.length,
    slot: t('res-slot'), nf: t('nf-desc'), ug: t('ug-desc'), err: t('err-desc'),
    count: t('count'),
    cornerShown: (document.getElementById('corner') || {dataset:{}}).dataset.on === '1',
    formErr: (document.getElementById('form-error') || {dataset:{}}).dataset.on === '1',
    chip: (document.querySelector('[data-result-plate]') || {}).textContent || '',
    lots
  };
})()`;

let client = null;
try {
  console.log('\n🔎 ' + URL_);
  client = await launch({ headless: !HEAD });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Page.addScriptToEvaluateOnNewDocument', { source: EARLY_HOOK });
  /* 🔑 **창 크기를 못 박는다.** 배치 검사는 창에 따라 답이 달라진다 —
     크기를 안 정하면 같은 검사가 기계마다 다른 말을 한다. */
  await client.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 800, deviceScaleFactor: 1, mobile: false });
  await client.send('Page.navigate', { url: URL_ });
  let ready = false;
  for (let i = 0; i < 100; i++) {
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.getElementById('btn-start')`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  ok('화면이 떴다', ready === true);
  if (ready !== true) throw new Error('화면 준비 실패');

  /* ════════════════════════════════════════════════════════════════
     🔴 실기 모드 — 주입 0 · 클릭 0. 서버가 실제로 보낸 것만 읽는다
     ★ 후킹을 **전역·프로토타입**에 건다. 객체 하나에 걸면 화면의 재연결이 그것을 덮어
       **감시가 조용히 죽는다**(📖 docs/web/LEDGER.md §5.114 — 그 함정을 실측으로 밟았다).
     ════════════════════════════════════════════════════════════════ */
  if (LIVE) {
    const early = await evaluate(client, `!!(window.__rxN && Object.keys(window.__rxN).length)`).catch(() => false);
    if (early !== true) console.log('  ⚠ 로드 전 후킹에 아직 봉투가 없다 — 접속 전이거나 후킹이 안 걸렸다');
    /* 🔑 계측기 자기검증 — 감시가 살아 있는지 먼저 세운다. 없으면 아래 0 이 무엇의 0 인지 모른다 */
    const armed = await evaluate(client, `JSON.parse.toString().indexOf('__rx') >= 0
      && WebSocket.prototype.send.toString().indexOf('__sent') >= 0`);
    ok('🔑 계측기 자기검증: 수신·전선 감시가 살아 있다', armed === true, String(armed));

    let linked = false;
    for (let i = 0; i < 200; i++) {
      linked = await evaluate(client, `(document.getElementById('link')||{dataset:{}}).dataset.ok === '1'`).catch(() => false);
      if (linked === true) break;
      await sleep(100);
    }
    ok('🔴 실기: WS 로 붙었다', linked === true, '20초 안에 안 붙었다 — 서버·포트를 확인해라');
    /* 🔴🔴 **"붙었다" 는 살아 있다는 뜻이 아니다.** 서버는 경로의 앞 세 글자만 보므로
       `/ws/무엇` 도 핸드셰이크가 통과한다 — 그러면 화면이 초록불을 켜 놓고 하행이 하나도 안 온다.
       ★ 그래서 **자료가 그려지나** 를 따로 봐야 한다. 아래 `분모` 검사가 그 자리다. */
    /* ⚠ **이 관측 자체가 서버에 연결 하나를 더한다.** 서버는 연결마다 `push_snapshot()` 을 낸다.
       ✅ `cdp.mjs` 가 프로세스 종료 시 브라우저를 죽인다 — `process.exit()` 로 끝나도 안 남는다.
       🔑 그래도 **짧게 붙어라.** 오래 붙어 있으면 재려던 것을 자기가 흔든다. */
    console.log('  · ' + SECS + '초 동안 봉투를 모은다 (주입 0 · 클릭 0)');
    console.log('    ⚠ 이 관측이 서버 연결 하나를 더한다 — 끝나면 바로 닫는다.');
    await sleep(SECS * 1000);
    const rx = await evaluate(client, `({ n: window.__rxN, keys: Object.keys(window.__rx) })`);
    console.log('  · 받은 봉투: ' + JSON.stringify(rx.n));
    ok('🔴🔴 분모: 봉투를 하나라도 받았다 (붙기만 하고 조용한 것을 여기서 잡는다)',
       rx.keys.length > 0, JSON.stringify(rx.keys) + ' — 링크는 초록인데 0 이면 경로를 의심해라');
    const stateN = (rx.n && rx.n.state) || 0;
    console.log('  · 이 관측의 분모: `state` ' + stateN + '장 · 창 ' + SECS + '초'
              + (stateN < 2 ? '  → 순간 스냅샷만 말할 수 있다' : '  → 시간 축 판정이 유효하다'));

    const probe = await evaluate(client, `(() => {
      const st = window.__rx.state, m = window.__rx.map;
      const zs = (st && Array.isArray(st.zones)) ? st.zones : [];
      const withPlate = zs.filter((z) => z && typeof z.plate !== 'undefined');
      const named = withPlate.filter((z) => typeof z.plate === 'string' && z.plate);
      /* 🔴 번호를 모르는 점유 — 계약 §4-B. 센서를 못 믿는 자리는 뺀다(잔상이다) */
      const unplated = withPlate.filter((z) => z.occupied === true && z.plate === null
        && z.value_state === 'known' && !(z.usable && z.usable.ok === false));
      const mz = (m && Array.isArray(m.zones)) ? m.zones : [];
      return {
        zonesN: zs.length,
        plateKeyN: withPlate.length,          /* 자리(si>=0)에만 plate 키가 실린다 — 서버 계약 */
        parkedN: named.length,                /* 지금 번호가 있는 자리 수 */
        unplatedN: unplated.length,           /* 번호를 모르는 점유 수 */
        vsN: withPlate.filter((z) => typeof z.value_state === 'string').length,
        labelN: mz.filter((z) => z && typeof z.label === 'string' && z.label).length
      };
    })()`);
    console.log('  · 봉투 키 표: ' + JSON.stringify(probe));
    ok('🔴 `state.zones[].plate` 키가 자리에만 실린다 (분모 ' + probe.plateKeyN + '/' + probe.zonesN + ')',
       probe.plateKeyN > 0 && probe.plateKeyN < probe.zonesN,
       '0 이면 화면이 자리를 하나도 못 그린다 · 전부면 입·출구가 안 걸러진다');
    ok('🔑 `state.zones[].value_state` 가 자리마다 실린다 (' + probe.vsN + '/' + probe.plateKeyN + ')'
       + ' — 잔상을 값으로 안 읽는 근거', probe.vsN === probe.plateKeyN && probe.vsN > 0);
    ok('🔑 `map` 이 자리 라벨을 준다 (' + probe.labelN + '개)', probe.labelN > 0,
       '없으면 화면이 자리 id 를 그대로 보인다 — 틀린 건 아니지만 이용자 어휘가 아니다');
    console.log('  · 지금 주차장: 번호 있는 자리 ' + probe.parkedN + ' · 번호 모르는 점유 ' + probe.unplatedN);
    if (probe.parkedN === 0) {
      skip('주차된 차 조회', '지금 번호가 있는 자리가 0 이다 — 차를 대고 다시 재라 (0/0 은 못 잰 것이다)');
    } else {
      ok('🔴 주차된 차가 있다 (' + probe.parkedN + '대) — 조회를 실제로 재 볼 수 있다', true);
    }
    /* ════════════════════════════════════════════════════════════════
       🔴🔴 **카운트가 실기에서 실제로 줄어드나** (REQ-0523 재검 · 2026-08-27)
       ★ 앞선 검증이 **주입**이었다 — 촬영본 우상단이 *"연결되지 않음"* 이었고
         봉투를 내가 넣었다. §"주입 자료가 실기보다 순하면 그 검사는 실기를 안 본다" 가
         **내 검사 자신에** 걸린 것이다.
       ✅ 그래서 **실서버에 붙은 채로** 조회하고 지켜본다.
       🔑 그리고 **`show()` 호출을 센다** — 되감기의 원인은 언제나 "화면이 다시 바뀌었나" 다.
       ⚠ 이 화면은 **상행이 0** 이라 조회해도 서버에 아무것도 안 나간다(안전하다). */
    {
      const plate = await evaluate(client, `(() => {
        const st = window.__rxLast && window.__rxLast.state;
        const zs = (st && Array.isArray(st.zones)) ? st.zones : [];
        const z = zs.find((x) => typeof x.plate === 'string' && x.plate);
        return z ? z.plate : null;
      })()`);
      if (!plate) {
        skip('실기에서 카운트가 줄어든다', '번호가 있는 자리가 0 이다 — 차를 대야 잰다(0/0)');
      } else {
        /* `show` 를 감싸 호출을 센다. **화면 파일은 안 고친다** — 하니스 안에서만 감싼다. */
        /* 🔑 **되감기를 직접 센다.** `show()` 가 불리는 것 자체는 **정상**이다 —
           봉투마다 `applyResult()` 가 결과를 다시 그리고, 그때 같은 화면이면 아무 일도 안 한다.
           🔴 요점은 **`startCount()` 가 다시 불리나** 이다. 그것이 30 으로 되감는 함수다.
           ⚠ `show()` 호출 수로 재면 **정상 동작이 빨강**이 된다(실측 7초에 28회 — 봉투가 그만큼 온다). */
        await evaluate(client, `(() => {
          window.__showLog = [];  window.__startLog = 0;
          const orig = show;
          show = function (n) { window.__showLog.push(n); return orig.apply(this, arguments); };
          const oc = startCount;
          startCount = function () { window.__startLog++; return oc.apply(this, arguments); };
          window.__linkLog = [];
          const os = setLink;
          setLink = function (ok, why) { window.__linkLog.push(ok ? 'up' : ('down:' + (why || ''))); return os.apply(this, arguments); };
          return true;
        })()`);
        await evaluate(client, `document.getElementById('btn-start').click()`);
        await sleep(60);
        await evaluate(client, `(() => { document.getElementById('in-plate').value = ${JSON.stringify(plate)}; return true; })()`);
        await evaluate(client, `document.getElementById('form').requestSubmit()`);
        await sleep(200);
        const a = await evaluate(client, `(() => ({
          view: [...document.querySelectorAll('[data-view]')].filter((x) => x.dataset.on === '1').map((x) => x.dataset.view)[0],
          count: (document.getElementById('count') || {}).textContent || '',
          shows: window.__showLog.slice(), starts: window.__startLog
        }))()`);
        console.log('  · 조회 "' + plate + '" 직후: ' + a.view + ' · "' + a.count + '"');
        await sleep(7000);
        const b = await evaluate(client, `(() => ({
          view: [...document.querySelectorAll('[data-view]')].filter((x) => x.dataset.on === '1').map((x) => x.dataset.view)[0],
          count: (document.getElementById('count') || {}).textContent || '',
          shows: window.__showLog.slice(), starts: window.__startLog,
          links: window.__linkLog.slice(), rx: window.__rxN && window.__rxN.state
        }))()`);
        const n0 = Number((a.count.match(/(\d+)/) || [])[1]);
        const n1 = Number((b.count.match(/(\d+)/) || [])[1]);
        console.log('  · 7초 뒤: ' + b.view + ' · "' + b.count + '"');
        const reShow = b.shows.length - a.shows.length;
        console.log('  · 그 사이 `show()` ' + reShow + '회(전부 ' + (b.shows[b.shows.length - 1] || '?') + ')'
                  + ' · `startCount()` ' + (b.starts - a.starts) + '회'
                  + ' · 링크 변화 ' + JSON.stringify(b.links) + ' · `state` 누적 ' + b.rx + '장');
        ok('🔴🔴 **실기에서** 카운트가 줄어든다 (' + n0 + ' → ' + n1 + ')',
           Number.isFinite(n0) && Number.isFinite(n1) && n1 < n0,
           n0 + ' → ' + n1 + ' — 안 줄면 **영영 안 닫힌다**');
        /* 🔴 **이것이 이 검사의 본체다.** `show()` 는 봉투마다 불려도 되지만
           `startCount()` 가 다시 불리면 **그 순간 30 으로 되감긴다**(REQ-0523 의 결함). */
        ok('🔴🔴 그 사이 `startCount()` 가 **다시 안 불린다** (같은 결과에는 시한을 안 되감는다)',
           b.starts === a.starts,
           '`show()` 는 ' + reShow + '회 불렸다(정상) · `startCount()` 는 ' + (b.starts - a.starts) + '회');
        /* 🔑 **분모를 적어 둔다** — `show()` 가 몇 번 불렸나가 곧 *"봉투가 몇 장 왔나"* 이고,
           그것이 0 이면 위 초록은 **"안 되감았다" 가 아니라 "아무 일도 안 일어났다"** 다. */
        ok('🔑 분모: 그 사이 봉투가 실제로 왔다 (`show()` ' + reShow + '회) — 0 이면 위 초록이 공허하다',
           reShow > 0, String(reShow));
      }
    }

    const sent = await evaluate(client, `window.__sent`);
    /* ⚠ 이 `0` 의 분모는 **관측 창**이다 — 감시는 살아 있음을 위에서 세웠으므로 `0/0` 은 아니다. */
    ok('🔴 **상행이 하나도 없다** (' + SECS + '초 창) — 이 화면이 죽어도 주차장은 돈다',
       Array.isArray(sent) && sent.length === 0, JSON.stringify(sent));
    console.log('\n  🔑 여기까지가 "지금 실기에서 그렇게 나온다" 다. 주입 모드의 초록과 다른 진술이다.');
  } else {

  /* 🔑 WS 대신 **봉투를 직접 넣는다** — 이 화면은 `ws.onmessage` 하나가 입구다 */
  const feed = (m) => `(() => { ws.onmessage({ data: ${JSON.stringify(JSON.stringify(m))} }); return true; })()`;
  const find = async (val) => {
    await evaluate(client, `(() => { document.getElementById('in-plate').value = ${JSON.stringify(val)}; return true; })()`);
    await evaluate(client, `document.getElementById('form').requestSubmit()`);
    await sleep(70);
    return await evaluate(client, V);
  };
  const restart = async () => {
    await evaluate(client, `document.getElementById('btn-close').click()`);
    await sleep(40);
    await evaluate(client, `document.getElementById('btn-start').click()`);
    await sleep(40);
  };

  /* ── [1] 대기 화면 — 🔴 **누를 수 있는 것이 하나뿐**(화면 명세) ────────────
     ⚠ 옛 검사는 *"버튼 1개 · 그 밖에 글자가 없다"* 였다. 새 화면은 아이콘과 한 줄 설명이 붙는다.
     🔑 명세가 막으려는 실패는 *"무엇을 눌러야 할지 모른다"* 이지 글자 수가 아니다 —
       §"규약은 낱말이 아니라 막으려는 실패로 읽어라". 그래서 **누를 수 있는 것의 수**를 센다. */
  console.log('\n[1] 대기 — 누를 수 있는 것이 하나뿐');
  let v = await evaluate(client, V);
  ok('🔴 대기 상태로 시작한다', v.view === 'IDLE', JSON.stringify({ view: v.view, shown: v.shown }));
  ok('🔑 화면이 **하나만** 보인다 (전이가 겹치지 않는다)', v.shown === 1, String(v.shown));
  const idleBits = await evaluate(client, `(() => {
    const b = [...document.querySelectorAll('button')];
    const en = b.filter((x) => !x.disabled && x.offsetParent !== null);
    return { total: b.length, enabled: en.length, label: en.map((x) => x.textContent.trim()) };
  })()`);
  ok('🔴 대기 화면에서 **누를 수 있는 것이 하나뿐**이고 그것이 조회 버튼이다',
     idleBits.enabled === 1 && idleBits.label[0] === '주차위치확인', JSON.stringify(idleBits));

  /* ── [2] 조회 — 상행 0 으로 찾는다 ───────────────────────────────────── */
  console.log('\n[2] 조회 — 상행 0 으로 찾는다');
  await evaluate(client, feed(MAP));
  await evaluate(client, feed(STATE({ A3: '123바9898' })));
  /* 🔴 **전선을 감시한다** — 이 화면은 상행이 하나도 없어야 한다 */
  await evaluate(client, `(() => {
    window.__sent = [];
    /* 🔴🔴 **객체 하나만 후킹하면 조용히 되돌아간다.** 화면은 onclose 에서 재연결하고
       file:// 에서는 연결이 즉시 실패하므로 ~1초 뒤 새 WebSocket 이 **우리 후킹을 덮는다.**
       🔴 그러면 "상행 0" 검사가 **감시가 죽어서** 0 이 된다 — 거짓 초록이다
       (§"분모 없는 0 은 건강처럼 보인다": 0/0(못 쟀다) 과 0/N(정상) 은 다르다).
       ✅ 그래서 **생성자 자체**를 간다. 재연결이 만든 것도 감시 대상이다. */
    window.WebSocket = function (u) {
      this.url = u; this.readyState = 1;
      this.onopen = null; this.onmessage = null; this.onclose = null; this.onerror = null;
      this.send = function (x) { window.__sent.push(x); };
      this.close = function () {};
    };
    if (ws) ws.send = function (x) { window.__sent.push(x); };
    return true;
  })()`);
  /* 🔑 **계측기 자기검증** — 감시가 살아 있는지 먼저 본다. 이게 없으면 아래 0 이 무엇의 0 인지 모른다. */
  const armed = await evaluate(client, `typeof ws.send === 'function' && ws.send.toString().indexOf('__sent') >= 0`);
  ok('🔑 계측기 자기검증: 전선 감시가 살아 있다 (0 이 "못 쟀다" 가 아님을 먼저 세운다)', armed === true, String(armed));

  await evaluate(client, `document.getElementById('btn-start').click()`);
  await sleep(50);
  v = await evaluate(client, V);
  ok('🔑 조회 버튼이 입력 화면을 연다', v.view === 'ASK', v.view);
  v = await find('123바9898');
  console.log('  · ' + JSON.stringify({ view: v.view, slot: v.slot, hit: v.lots.filter((l) => l.hit).map((l) => l.zone) }));
  ok('🔴 찾으면 결과 화면으로 간다', v.view === 'FOUND', v.view);
  ok('🔴 분모: 자리가 다섯 그려졌다 (입·출구는 걸러진다)', v.lots.length === 5, JSON.stringify(v.lots.map((l) => l.zone)));
  /* 🔴 **화면 나열 순서** — 사용자 지시(2026-08-27): 왼쪽부터 5 4 3 2 1.
     ⚠ **라벨이 아니라 자리 id 로** 못 박는다 — 라벨은 서버가 정하고 바뀔 수 있다.
     🔑 8081 과 **같은 배열**이어야 한다. 두 화면이 다르면 이용자가 같은 주차장을 두 모양으로 본다. */
  ok('🔴 자리를 **봉투의 역순**으로 나열한다 (8081 과 같은 배열)',
     v.lots.map((l) => l.zone).join(',') === 'A5,A4,A3,A2,A1', JSON.stringify(v.lots.map((l) => l.zone)));
  console.log('  · 화면에 보이는 번호: ' + v.lots.map((l) => l.no).join(' '));
  ok('🔴 그 차가 있는 칸 **하나만** 표시된다 (A3)',
     v.lots.filter((l) => l.hit).length === 1 && v.lots.find((l) => l.hit).zone === 'A3',
     JSON.stringify(v.lots));
  ok('🔑 자리 번호를 **이용자 어휘**로 적는다 (3번)', v.slot.indexOf('3번') >= 0, v.slot);
  ok('🔴 우측 하단에 카운트와 닫기가 뜬다', v.cornerShown === true && /\d+초/.test(v.count), JSON.stringify({ c: v.cornerShown, n: v.count }));
  const sent = await evaluate(client, `window.__sent`);
  ok('🔴 **상행이 하나도 없다** (읽기만 한다 → 8080 이 죽어도 주차장은 돈다)',
     Array.isArray(sent) && sent.length === 0, JSON.stringify(sent));

  /* ── [3] 🔴 번호를 **표시한다** · 관제 어휘는 안 샌다 ─────────────────────
     ⚠ 이 검사는 **2026-08-27 에 뒤집혔다.** 그 전 지정은 *"번호를 표시하지 않는다"* 였고
       검사도 `plateShown === false` 를 요구했다. 사용자가 표시하기로 결정했다.
     🔑 옛 요구를 그대로 뒀으면 **검사가 옛 지정을 지키는 자물쇠**가 됐다 — 📖 LEDGER §5.129. */
  console.log('\n[3] 번호 표시 · 어휘 누출');
  const leak = await evaluate(client, `(() => {
    /* 🔑 **보이는 글자 + title 속성까지** 본다 — 툴팁으로도 새면 안 된다(루트 지시) */
    let all = document.body.innerText || '';
    for (const e of document.querySelectorAll('[title]')) all += ' ' + e.getAttribute('title');
    for (const e of document.querySelectorAll('[aria-label]')) all += ' ' + e.getAttribute('aria-label');
    const bad = ['devid', 'P1', 'P2', 'P3', 'P4', 'P5', 'C1', 'result', '모듈', 'usable', 'plate_source', 'value_state'];
    return { plateShown: all.indexOf('123바9898') >= 0,
             hits: bad.filter((w) => all.indexOf(w) >= 0), sample: all.replace(/\\s+/g, ' ').slice(0, 120) };
  })()`);
  console.log('  · ' + JSON.stringify(leak));
  ok('🔴 **차량번호를 결과에 표시한다** (사용자 결정 2026-08-27 · 그 전에는 표시하지 않았다)',
     leak.plateShown === true && v.chip === '123바9898', v.chip + ' / ' + leak.sample);
  ok('🔴 관제 어휘가 새지 않는다 (devid·모듈·result·C1·P3 …)',
     leak.hits.length === 0, JSON.stringify(leak.hits));

  /* ── [4] 🔴 못 찾은 이유를 **다섯으로** 가른다 ────────────────────────
     새 UI 는 `else → ERROR` 로 셋을 뭉쳤다. 그러면 `many`(서버 결함)와 `nolink`(아직 안 옴)에
     *"입력값을 확인하세요"* 가 붙는다 — **둘 다 거짓이다.** 사람이 할 일이 갈래마다 다르다. */
  console.log('\n[4] 못 찾았을 때 — 다섯 갈래');
  await evaluate(client, `document.getElementById('btn-close').click()`);
  await sleep(50);
  v = await evaluate(client, V);
  ok('닫기를 누르면 대기로 돌아간다', v.view === 'IDLE', v.view);
  ok('🔑 그리고 앞사람 번호가 지워진다 (공용 기기다)', v.chip === '', JSON.stringify(v.chip));

  await evaluate(client, `document.getElementById('btn-start').click()`);
  await sleep(40);
  /* (a) 빈 입력 — 🔴 **입력 화면에 남는다.** 결과 화면으로 넘어가면 무엇을 고칠지 안 보인다 */
  v = await find('');
  ok('🔴 빈 입력은 **입력 화면에 남아** 폼 오류를 켠다', v.view === 'ASK' && v.formErr === true,
     JSON.stringify({ view: v.view, formErr: v.formErr }));
  /* (b) 없는 번호 · 번호 모르는 점유 0 → 🔵 **확정할 수 있다** */
  v = await find('999가0000');
  console.log('  · 없는 번호(오주차 0) → ' + v.view + ' | ' + v.nf);
  ok('🔴 없는 번호는 NOT_FOUND 다', v.view === 'NOT_FOUND', v.view);
  ok('🔴 그리고 **없다고 확정해서 말한다** (번호 모르는 점유가 0 이므로)',
     /없습니다/.test(v.nf), v.nf);
  ok('🔑 조회한 번호를 같이 보여 준다', v.chip === '999가0000', v.chip);

  /* (c) 🔴 없는 번호 · **번호 모르는 점유가 있다** → 확정할 수 없다
     ★ 이것이 `0/0`(못 쟀다) 과 `0/N`(정말 없다) 을 가르는 자리다. */
  await evaluate(client, feed(STATE({ A3: '123바9898' }, { unplated: ['A1', 'A5'] })));
  await sleep(40);
  v = await find('999가0000');
  console.log('  · 없는 번호(오주차 2) → ' + v.view + ' | ' + v.ug);
  ok('🔴 번호 모르는 점유가 있으면 **다른 화면**이다 (NOT_FOUND 로 뭉치지 않는다)',
     v.view === 'UNGUIDED', v.view);
  ok('🔴 그리고 **몇 대인지 센다** (분모를 준다 — "없다" 로 뭉치면 이용자가 포기한다)',
     v.ug.indexOf('2대') >= 0, v.ug);

  /* (d) 🔴 센서를 못 믿는 자리는 그 셈에서 **빠진다** — 잔상을 값으로 읽으면
     링크가 끊길 때마다 오주차가 늘어난다(socket 확인 · 계약 §4-B) */
  await evaluate(client, feed(STATE({ A3: '123바9898' }, { unplated: ['A1', 'A5'], blind: ['A1', 'A5'] })));
  await sleep(40);
  v = await find('999가0000');
  console.log('  · 그 둘이 `unknown` 일 때 → ' + v.view + ' | ' + v.ug);
  /* 🔴 **한 곳이라도 못 보면 "없다" 를 말할 자격이 없다** (socket 확인 · REQ-0503).
     ⚠ 앞 판은 여기서 `NOT_FOUND` 로 **단정하고 괄호로만** "5칸 중 3칸" 을 붙였다 —
       🔑 **괄호 안 조건은 읽는 사람이 안 읽는다.** 앞 문장이 답으로 기억된다.
     ★ 그래서 이 검사도 옛 거동을 못 박고 있었다. **뜻으로 다시 건다** —
       못 믿는 자리를 *오주차로 세지 않는 것*(여전히 참)과 *단정하지 않는 것*(새 규칙)을 둘 다. */
  ok('🔴🔴 못 믿는 자리가 하나라도 있으면 **"없다" 고 단정하지 않는다**',
     v.view === 'UNGUIDED' && !/차량이 없습니다/.test(v.ug), v.view + ' | ' + v.ug);
  ok('🔑 **몇 곳을 못 봤는지 문장에 넣는다** (괄호가 아니라 주어다 — 2곳)',
     /2곳/.test(v.ug) && /단정할 수 없습니다/.test(v.ug), v.ug);
  ok('🔑 그리고 못 믿는 자리를 **오주차로는 안 센다** (그 수를 지어내지 않는다)',
     !/차가 2대/.test(v.ug), v.ug);

  /* (d-2) 🔴🔴 **자리를 하나도 못 믿으면 "없다" 고 확정하지 않는다**
     ★ 8081 에서 같은 자리를 실기 화면으로 잡았다 — 보드가 없는데 빈자리를 `0` 이라 말했다.
       여기서는 그 `0` 이 *"그 차량이 없습니다"* 로 나온다. **둘 다 못 잰 것이다.** */
  await evaluate(client, feed(STATE({}, { blind: ['A1', 'A2', 'A3', 'A4', 'A5'] })));
  await sleep(40);
  v = await find('999가0000');
  console.log('  · 다섯 칸 전부 못 믿을 때 → ' + v.view + ' | ' + v.ug);
  ok('🔴🔴 자리를 하나도 못 믿으면 **"없습니다" 라고 확정하지 않는다** (0/0 은 0/N 이 아니다)',
     v.view === 'UNGUIDED' && /확인할 수 없/.test(v.ug), v.view + ' | ' + v.ug);
  /* 🔴 **대조군이 없으면 "아무것도 단정 안 하는 화면"** 이 되고 그건 반대 방향 고장이다.
     ★ 루트가 그것을 못 박으라고 했다 — *"unknown 0 이면 지금처럼 확정한다"*. */
  await evaluate(client, feed(STATE({})));
  await sleep(40);
  v = await find('999가0000');
  ok('🔑 대조군: **전수를 믿을 수 있으면 없다고 확정한다** (아무것도 단정 안 하는 화면이 아니다)',
     v.view === 'NOT_FOUND' && /차량이 없습니다/.test(v.nf), v.view + ' | ' + v.nf);
  ok('🔑 그리고 그때는 **분모 괄호가 안 붙는다** (전수라 군더더기다)',
     !/칸 중/.test(v.nf), v.nf);

  /* (e) 🔴 같은 번호가 두 자리 = **서버 결함.** 입력을 고쳐도 안 바뀐다 */
  await evaluate(client, feed(STATE({ A2: '123바9898', A4: '123바9898' })));
  await sleep(40);
  v = await find('123바9898');
  console.log('  · 두 자리 → ' + v.view + ' | ' + v.err);
  ok('🔴 같은 번호가 두 자리면 **ERROR 이고 사실을 말한다** (한 대는 한 자리다 — 숨기면 못 고친다)',
     v.view === 'ERROR' && /여러 자리/.test(v.err), v.err);
  ok('🔴 그리고 **"입력을 확인하라" 고 하지 않는다** — 고쳐도 안 바뀐다. 알릴 상대는 관리자다',
     !/입력/.test(v.err) && /관리자/.test(v.err), v.err);

  /* (f) 🔴 자료가 없을 때 — **"없다" 와 "아직 모른다" 를 가른다** */
  await evaluate(client, `(() => { S.zones = null; return true; })()`);
  v = await find('123바9898');
  console.log('  · 봉투 없음 → ' + v.view);
  ok('🔴 봉투를 못 받았으면 **기다리는 화면**이다 ("찾을 수 없다" 로 뭉치지 않는다)',
     v.view === 'LOADING', v.view);
  /* 🔑 그리고 봉투가 오면 **저절로 답이 나온다** — 질의가 살아 있다 */
  await evaluate(client, feed(STATE({ A3: '123바9898' })));
  await sleep(60);
  v = await evaluate(client, V);
  ok('🔑 봉투가 도착하면 기다리던 질의가 **저절로 답이 된다**', v.view === 'FOUND' && v.slot.indexOf('3번') >= 0,
     JSON.stringify({ view: v.view, slot: v.slot }));

  /* 🔑 관용 비교 — 사람이 공백·하이픈을 넣는다 */
  await restart();
  v = await find('123 바 9898');
  ok('🔑 공백이 섞여도 찾는다 (사람이 그렇게 친다)', v.view === 'FOUND', JSON.stringify({ view: v.view }));

  /* ── [5] 🔴 **결과를 보는 동안 차가 나가면 표시도 따라간다** ─────────────
     ★ 조회 결과는 **질의의 답**이지 한 번 정해진 사실이 아니다. 자리만 다시 칠하면
       그 차가 나간 뒤에도 같은 칸이 계속 녹색으로 남는다. */
  console.log('\n[5] 보는 중에 차가 나간다');
  v = await evaluate(client, V);
  ok('🔑 (전제) 결과 화면에 그 칸이 잡혀 있다', v.view === 'FOUND' && v.lots.filter((l) => l.hit).length === 1,
     JSON.stringify({ view: v.view, hit: v.lots.filter((l) => l.hit).map((l) => l.zone) }));
  await evaluate(client, feed(STATE({})));                     /* 차가 나갔다 */
  await sleep(60);
  v = await evaluate(client, V);
  console.log('  · 나갔다 → ' + v.view + ' | ' + v.nf);
  ok('🔴 잡힌 칸이 **풀린다** (옛 답이 녹색으로 남지 않는다)', v.lots.filter((l) => l.hit).length === 0,
     JSON.stringify(v.lots.filter((l) => l.hit).map((l) => l.zone)));
  ok('🔴 그리고 **그렇게 말한다** (빈 화면은 고장으로 읽힌다)', v.view === 'NOT_FOUND' && v.nf.length > 0, v.nf);
  await evaluate(client, feed(STATE({ A5: '123바9898' })));    /* 다른 자리로 옮겼다 */
  await sleep(60);
  v = await evaluate(client, V);
  ok('🔑 자리를 옮기면 **새 자리를 가리킨다** (5번)',
     v.lots.filter((l) => l.hit).map((l) => l.zone).join(',') === 'A5' && v.slot.indexOf('5번') >= 0,
     JSON.stringify({ slot: v.slot, hit: v.lots.filter((l) => l.hit).map((l) => l.zone) }));

  /* ── [6] 🔴 공용 기기 — **결과가 남지 않는다** ────────────────────────
     사용자 결정(2026-08-27): 번호를 표시한다. 그래서 **되돌아가는 것이 그 결정을 안전하게 만든다.**
     ⚠ 조작이 있으면 시한을 **다시 채운다.** 멈추면 다 읽고 자리를 떠도 앞사람 번호가 영영 남는다. */
  console.log('\n[6] 공용 기기 — 결과가 남지 않는다');
  const c0 = (await evaluate(client, V)).count;
  await sleep(2100);
  const c1 = (await evaluate(client, V)).count;
  const n0 = Number((c0.match(/(\d+)/) || [])[1]), n1 = Number((c1.match(/(\d+)/) || [])[1]);
  console.log('  · ' + c0 + '  →(2초)→  ' + c1);
  ok('🔴 남은 시간이 **줄어든다** (안 보이면 읽는 중에 사라진다)', n1 < n0, c0 + ' / ' + c1);
  await evaluate(client, `(() => { document.dispatchEvent(new Event('pointerdown')); return true; })()`);
  await sleep(60);
  const c2 = (await evaluate(client, V)).count;
  const n2 = Number((c2.match(/(\d+)/) || [])[1]);
  console.log('  · 손을 대면 → ' + c2);
  ok('🔴 조작이 있으면 시한이 **다시 찬다** (읽는 중에 사라지지 않는다)', n2 > n1, c1 + ' / ' + c2);
  ok('🔑 그런데 **멈추지는 않는다** — 자리를 뜨면 결국 지워진다(공용 기기다)', n2 <= 30, c2);
  /* 🔴🔴 **남은 초와 닫기가 화면 안에 있나** — 이것은 DOM 검사로 안 잡힌다.
     요소는 존재하므로 `textContent` 검사는 초록인데, 카드가 늘어나면 **창 밖으로 밀린다.**
     ★ 실제로 그렇게 있었다(2026-08-27 · 눈으로 잡았다). 키오스크에서 남은 초가 안 보이면
       이용자는 화면이 사라지는 것을 **모른 채** 읽다가 잃는다. */
  const seat = await evaluate(client, `(() => {
    const r = (id) => { const e = document.getElementById(id); if (!e) return null;
      const b = e.getBoundingClientRect();
      return { top: Math.round(b.top), bottom: Math.round(b.bottom),
               inView: b.top >= 0 && b.bottom <= innerHeight && b.width > 0 }; };
    const again = [...document.querySelectorAll('[data-again]')]
      .filter((e) => e.offsetParent !== null)[0] || null;
    const ab = again ? again.getBoundingClientRect() : null;
    const c = document.querySelector('.content');
    return { count: r('count'), close: r('btn-close'), h: innerHeight,
             again: ab ? { bottom: Math.round(ab.bottom), inView: ab.top >= 0 && ab.bottom <= innerHeight } : null,
             contentScroll: !!(c && c.scrollHeight > c.clientHeight + 1),
             contentOver: c ? (c.scrollHeight - c.clientHeight) : null,
             pageScroll: document.documentElement.scrollHeight > innerHeight + 1 };
  })()`);
  console.log('  · 바닥 요소 위치: ' + JSON.stringify(seat));
  ok('🔴 남은 초가 **창 안에 보인다** (밀려나면 이용자가 사라지는 줄 모른다)',
     !!(seat.count && seat.count.inView), JSON.stringify(seat.count) + ' / 창 높이 ' + seat.h);
  ok('🔴 닫기 버튼도 **창 안에 있다**', !!(seat.close && seat.close.inView), JSON.stringify(seat.close));
  ok('🔑 그리고 1280x800 창에서 페이지가 세로로 안 흐른다 (키오스크에서 스크롤을 요구하지 않는다)',
     seat.pageScroll === false, '스크롤 필요: ' + seat.pageScroll + ' — 창 높이 ' + seat.h);
  /* ⚠ 본문만 흐르게 해 뒀으므로 바닥(남은 초·닫기)은 안 밀린다 — 그래서 **위 검사가 초록인 채로**
     "다른 차량 조회" 만 스크롤 아래로 갈 수 있다. 🔑 안전장치가 증상을 가리는 자리다. */
  ok('🔴 "다른 차량 조회" 도 창 안에 있다 (본문만 흐르게 해 둬서 이건 따로 봐야 한다)',
     !!(seat.again && seat.again.inView), JSON.stringify(seat.again));
  ok('🔑 본문도 안 흐른다 (결과 한 판이 한 화면에 들어간다)', seat.contentScroll === false, String(seat.contentScroll));

  /* ⚠ 결과 화면은 **넷**이다. FOUND 만 재면 나머지 셋이 넘쳐도 모른다 —
     문구 길이가 화면마다 다르고, 좁은 카드(FOUND 아닌 것)는 세로가 더 길다. */
  const otherFit = [];
  for (const [label, plates, opt, q] of [
    ['UNGUIDED', {}, { unplated: ['A1', 'A3'] }, '999가0000'],
    ['NOT_FOUND', {}, {}, '999가0000'],
    ['ERROR', { A2: '111가1111', A4: '111가1111' }, {}, '111가1111']
  ]) {
    await evaluate(client, `document.getElementById('btn-close').click()`);
    await sleep(30);
    await evaluate(client, feed(STATE(plates, opt)));
    await evaluate(client, `document.getElementById('btn-start').click()`);
    await sleep(30);
    const rv = await find(q);
    const fit = await evaluate(client, `(() => {
      const c = document.querySelector('.content');
      const a = [...document.querySelectorAll('[data-again]')].filter((e) => e.offsetParent !== null)[0];
      const b = a ? a.getBoundingClientRect() : null;
      return { over: c ? c.scrollHeight - c.clientHeight : null,
               again: b ? (b.top >= 0 && b.bottom <= innerHeight) : null };
    })()`);
    otherFit.push({ want: label, got: rv.view, over: fit.over, again: fit.again });
  }
  console.log('  · 나머지 결과 화면: ' + JSON.stringify(otherFit));
  ok('🔴 결과 화면 **넷 다** 한 화면에 들어간다 (문구가 긴 것이 넘치기 쉽다)',
     otherFit.every((x) => x.want === x.got && x.over === 0 && x.again === true), JSON.stringify(otherFit));

  /* 🔴🔴 **봉투가 계속 와도 카운트가 줄어야 한다** (REQ-0523 · 사용자 신고)
     ★ 서버 스냅숏이 **1Hz** 다. 봉투마다 `applyResult()` → `show('FOUND')` → `startCount()` 라
       **매초 30 으로 되감겨 한 칸도 안 내려가고 영영 안 닫힌다.**
     🔑 그리고 이 하니스가 그걸 못 잡고 있었다 — **봉투를 한 장만 넣었기 때문이다.**
       §"주입 자료가 실기보다 순하면 그 검사는 실기를 안 본다" 의 정확한 사례다.
     ✅ 그래서 **여러 장 흘린다.** 그게 실기 조건이다.
     ⚠ 그리고 **되감기의 주체를 가른다** — 사람이 만지면 되감는 것이 **의도**다(아래 대조군). */
  {
    const c0 = Number(((await evaluate(client, V)).count.match(/(\d+)/) || [])[1]);
    /* 1초에 한 장씩 세 장 — 실기와 같은 박자 */
    for (let i = 0; i < 3; i++) {
      await sleep(1000);
      await evaluate(client, feed(STATE({ A5: '555가5555' })));
    }
    await sleep(80);
    const c3 = Number(((await evaluate(client, V)).count.match(/(\d+)/) || [])[1]);
    console.log('  · 봉투 3장(3초) 흘린 뒤: ' + c0 + '초 → ' + c3 + '초');
    ok('🔴🔴 **봉투가 와도 카운트가 줄어든다** (서버 갱신이 시한을 되감지 않는다)',
       Number.isFinite(c3) && c3 < c0, c0 + ' → ' + c3 + ' — 안 줄면 **영영 안 닫힌다**');

    /* 🔑 **대조군 ① — 사람이 만지면 되감는 것이 의도다.** 이걸 안 박으면 다음 사람이
       "되감기를 없애자" 로 읽고 **읽는 중에 화면이 사라지는** 반대 방향 고장을 만든다. */
    await evaluate(client, `(() => { document.dispatchEvent(new Event('pointerdown')); return true; })()`);
    await sleep(60);
    const cb = Number(((await evaluate(client, V)).count.match(/(\d+)/) || [])[1]);
    console.log('  · 사람이 만진 뒤: ' + c3 + '초 → ' + cb + '초');
    ok('🔑 대조군: **사람이 만지면 되감는다** (읽는 중에 사라지지 않는다 — 의도된 동작이다)',
       cb > c3, c3 + ' → ' + cb);

    /* 🔑 **대조군 ② — 결과 종류가 바뀌면 다시 준다.** 차가 나가 FOUND→NOT_FOUND 가 되면
       읽을 것이 달라졌으니 새로 세는 것이 맞다. 이걸 안 박으면 "한 번 시작하면 절대 안 되감는다"
       여도 위 검사가 초록이고, 그러면 **바뀐 문구를 1초 만에 놓친다**. */
    await evaluate(client, feed(STATE({})));            /* 차가 나갔다 → NOT_FOUND */
    await sleep(80);
    const vn = await evaluate(client, V);
    const cn = Number((vn.count.match(/(\d+)/) || [])[1]);
    console.log('  · 차가 나가 결과가 바뀐 뒤: ' + vn.view + ' · ' + cn + '초');
    ok('🔑 대조군: **결과 종류가 바뀌면 시한을 다시 준다** (바뀐 문구를 읽을 시간이다)',
       vn.view === 'NOT_FOUND' && cn > cb - 2, vn.view + ' · ' + cb + ' → ' + cn);
  }

  /* 🔴 실제로 되돌아가는지 — 시한을 짧게 줄여 **끝까지** 본다 */
  await evaluate(client, `(() => { SHOW_MS = 2000; startCount(); return true; })()`);
  await sleep(3200);
  v = await evaluate(client, V);
  ok('🔴 시한이 다하면 **처음 화면으로 돌아간다** (앞사람 번호가 남지 않는다)',
     v.view === 'IDLE' && v.chip === '', JSON.stringify({ view: v.view, chip: v.chip }));
  await evaluate(client, `(() => { SHOW_MS = 30000; return true; })()`);

  /* ── [7] 🔴 **좁은 기기에서 찾은 칸이 화면 안에 있나** ────────────────
     ★ 폰 크기(390x780)로 찍어 보고 찾았다: 격자가 두 칸씩 접혀 세로로 길어지고
       **그 차가 5번이면 스크롤 아래**다. 이 화면의 요점이 그 칸인데 안 보이면 답을 안 한 것이다.
     ⚠ 좁은 기기에서 **본문이 흐르는 것 자체는 정상**이다(스크롤이 자연스러운 기기다).
       키오스크(넓은 창)에서만 스크롤을 금지한다 — [6] 이 그것을 1280x800 으로 잰다. */
  console.log('\n[7] 어느 폭에서 찾은 칸이 화면 안에 있나');
  /* 🔴🔴 **검사는 잰 조건에서만 참이다.** 1280x800 하나로만 재고 있었는데
     좁은 기기에서 격자가 두 칸씩 접혀 **그 차가 5번이면 스크롤 아래**였다.
     🔑 그래서 **폭을 목록으로** 돈다 — 쓰일 폭만. 이 화면은 이용자가 자기 폰으로도 연다. */
  const WIDTHS = [
    { w: 320, h: 568, m: true,  why: '작은 폰' },
    { w: 360, h: 640, m: true,  why: '흔한 안드로이드 폰' },
    { w: 768, h: 1024, m: true, why: '태블릿 세로' },
  ];
  const wrows = [];
  for (const W of WIDTHS) {
    await client.send('Emulation.setDeviceMetricsOverride',
      { width: W.w, height: W.h, deviceScaleFactor: 1, mobile: W.m });
    await sleep(160);
    await evaluate(client, `document.getElementById('btn-close').click()`);
    await sleep(30);
    await evaluate(client, feed(STATE({ A5: '555가5555' })));
    await evaluate(client, `document.getElementById('btn-start').click()`);
    await sleep(30);
    const rv2 = await find('555가5555');
    const st2 = await evaluate(client, `(() => {
      const vis = (e) => { if (!e) return null; const b = e.getBoundingClientRect();
        return b.top >= 0 && b.bottom <= innerHeight && b.width > 0; };
      return { hit: vis(document.querySelector('[data-hit="1"]')),
               count: vis(document.getElementById('count')),
               close: vis(document.getElementById('btn-close')),
               scrollX: document.documentElement.scrollWidth > innerWidth + 1 };
    })()`);
    wrows.push({ w: W.w, view: rv2.view, hit: st2.hit, count: st2.count, close: st2.close, scrollX: st2.scrollX });
    console.log('  · ' + (W.w + 'x' + W.h).padEnd(9) + ' 찾은 칸 ' + String(st2.hit).padEnd(5)
              + ' · 남은 초 ' + String(st2.count).padEnd(5) + ' · 닫기 ' + String(st2.close).padEnd(5)
              + ' · 가로흐름 ' + st2.scrollX + '   ← ' + W.why);
  }
  ok('🔴 **모든 폭에서 찾은 칸이 화면 안에 있다** (이 화면의 요점이 그 칸이다)',
     wrows.every((r) => r.view === 'FOUND' && r.hit === true), JSON.stringify(wrows.map((r) => r.w + ':' + r.hit)));
  ok('🔴 **모든 폭에서 남은 초와 닫기가 보인다** (공용 기기라 지워지는 것을 알아야 한다)',
     wrows.every((r) => r.count === true && r.close === true),
     JSON.stringify(wrows.map((r) => r.w + ':' + r.count + '/' + r.close)));
  ok('🔑 어느 폭에서도 **가로로 흐르지 않는다**', wrows.every((r) => r.scrollX === false),
     JSON.stringify(wrows.map((r) => r.w + ':' + r.scrollX)));

  /* 🔑 아래는 **끌어오기가 실제로 일하는지**를 가장 좁은 폭에서 확인한다.
     ⚠ 큰 폰에서는 격자가 다 들어가 이 검사가 **아무것도 안 본다** —
       실측: 390x780 에서는 안 끌어와도 A5 가 보였다. 그래서 360x640 이다. */
  await client.send('Emulation.setDeviceMetricsOverride', { width: 360, height: 640, deviceScaleFactor: 1, mobile: true });
  await sleep(120);
  for (const [zone, plate] of [['A5', '555가5555'], ['A1', '111가1111']]) {
    await evaluate(client, `document.getElementById('btn-close').click()`);
    await sleep(30);
    const one = {}; one[zone] = plate;
    await evaluate(client, feed(STATE(one)));
    await evaluate(client, `document.getElementById('btn-start').click()`);
    await sleep(30);
    const rv = await find(plate);
    const seen = await evaluate(client, `(() => {
      const h = document.querySelector('[data-hit="1"]');
      if (!h) return null;
      const b = h.getBoundingClientRect();
      return { zone: h.dataset.zone, top: Math.round(b.top), bottom: Math.round(b.bottom),
               inView: b.top >= 0 && b.bottom <= innerHeight };
    })()`);
    /* 🔑 **그 초록이 무엇 덕인지 확인한다.** 본문을 맨 위로 되돌려 보면
       "끌어오지 않았다면 어땠을까" 가 나온다 — 그래도 보이면 이 검사는 **아무것도 안 본 것**이다. */
    const raw = await evaluate(client, `(() => {
      const c = document.querySelector('.content');
      const before = c ? c.scrollTop : 0;
      if (c) c.scrollTop = 0;
      const h = document.querySelector('[data-hit="1"]');
      const b = h ? h.getBoundingClientRect() : null;
      const r = { scrolled: before, inViewAtTop: b ? (b.top >= 0 && b.bottom <= innerHeight) : null };
      if (c) c.scrollTop = before;                    /* 되돌린다 — 다음 검사를 흔들지 않는다 */
      return r;
    })()`);
    console.log('  · ' + zone + ' 를 찾았을 때: ' + JSON.stringify(seen)
              + '  · 끌어온 양 ' + raw.scrolled + 'px · 안 끌어왔다면 보이나 ' + raw.inViewAtTop);
    ok('🔴 ' + zone + ' 이 답이어도 **그 칸이 화면 안에 있다** (좁은 기기에서 접혀도)',
       rv.view === 'FOUND' && !!(seen && seen.inView && seen.zone === zone), JSON.stringify(seen));
    if (zone === 'A5') {
      /* A5 는 격자 맨 아래다. 🔑 위 초록이 **`scrollIntoView` 덕인지** 여기서 가른다.
         ⚠ 그런데 **조건이 안 만들어질 수도 있다** — 창이 크면 안 끌어와도 보인다.
            그때 이 검사를 빨강으로 내면 **화면 탓으로 오독된다.** 그건 못 잰 것이다(0/0). */
      if (!(raw.scrolled > 0)) {
        skip('대조군: 맨 아래 칸이 안 끌어오면 화면 밖인가',
             '끌어올 필요가 없었다(0px) — 이 창에서는 격자가 통째로 들어간다. 조건이 안 만들어졌다');
      } else if (raw.inViewAtTop !== false) {
        skip('대조군: 맨 아래 칸이 안 끌어오면 화면 밖인가',
             '끌어오긴 했지만(' + raw.scrolled + 'px) **안 끌어와도 보였다** — '
             + '이 창에서는 위 검사가 아무것도 안 본다. 더 작은 창이 필요하다');
      } else {
        ok('🔑 대조군: 맨 아래 칸은 **안 끌어오면 화면 밖**이다 (위 초록이 끌어온 덕임을 보인다)',
           true, '끌어온 양 ' + raw.scrolled + 'px');
      }
    }
  }
  await client.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 800, deviceScaleFactor: 1, mobile: false });
  await sleep(120);

  /* ── [8] 🔴 **빨간불을 한 번 내 본다** ────────────────────────────────
     ★ 초록불은 "괜찮다" 와 "안 봤다" 를 같은 모양으로 만든다. */
  console.log('\n[8] 검사가 실패도 하는지 — 틀린 입력을 넣어 본다');
  await evaluate(client, `document.getElementById('btn-start').click()`);
  await sleep(40);
  await evaluate(client, feed(STATE({}, { unplated: ['A1'] })));
  await sleep(40);
  v = await find('999가0000');
  console.log('  · 오주차 1대일 때: ' + v.view + ' | ' + v.ug);
  ok('🔑 오주차 수를 1 로 바꾸면 문구도 **1대**로 바뀐다 (2대를 고정으로 안 쓴다)',
     v.view === 'UNGUIDED' && v.ug.indexOf('1대') >= 0 && v.ug.indexOf('2대') < 0, v.ug);

  skip('실기에서 8080 포트로 붙는다', '주입 모드다 — 실기는 `node web/tools/user8080.mjs --live 8080` 이 잰다');
  }
} catch (e) {
  fail++;
  console.log('\n  ❌ 하니스 자체가 죽었다 — ' + (e && e.message ? e.message : String(e)));
} finally {
  if (client) await client.close().catch(() => {});
}

console.log('\n' + '─'.repeat(60));
console.log('  ' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail'
          + (skipped ? ' / ' + skipped + ' 미측정' : ''));
process.exit(fail > 0 ? 1 : 0);
