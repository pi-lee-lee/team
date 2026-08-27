/**
 * 🔴 **이용자 화면(8081 · 자리 예약)** 을 잰다 — `서머리/server/user8081.html`.
 *
 * ★ 이 화면의 성질이 특이하다: **선택적 개입**이다.
 * ```
 * 붙어 있지 않으면 → 시스템이 알아서 배정한다(기존 그대로)
 * 🔴 그래서 **8081 이 죽어도 주차장은 돈다.** 그 성질을 깨뜨리는 코드가 없어야 한다
 * ```
 * ⚠ **주입이다**(서버 0 · 트래픽 0). 상행은 WS `send` 를 감시해 센다.
 *
 * 사용: node web/tools/user8081.mjs              (주입 · 어느 때나)
 *       node web/tools/user8081.mjs --file <html>
 *       node web/tools/user8081.mjs --live 8081  ← 🔴 실기 · 주입 0 · **클릭 0**(자리를 예약하지 않는다)
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
   ⚠⚠ 이 화면은 **상행이 있다**(`chooser` · `pick_slot`). 이 모드는 **클릭을 하지 않는다** —
      `pick_slot` 이 나가면 실기의 자리 하나가 실제로 예약된다. 읽기만 한다. */
const LIVE = (() => { const i = process.argv.indexOf('--live'); return i >= 0 ? process.argv[i + 1] : null; })();
const SECS = (() => { const i = process.argv.indexOf('--secs'); return i >= 0 ? Math.max(5, Number(process.argv[i + 1]) || 0) : 25; })();
const URL_ = LIVE
  ? ('http://127.0.0.1:' + LIVE + '/')
  : (fileArg
      ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg)).href
      : new URL('../../서머리/server/user8081.html', import.meta.url).href);

let pass = 0, fail = 0, skipped = 0;
const ok = (n, c, d) => { if (c) { pass++; console.log('  ✅ ' + n); } else { fail++; console.log('  ❌ ' + n + (d ? '\n       → ' + d : '')); } };
const skip = (n, why) => { skipped++; console.log('  ⏭ ' + n + '  → 측정 불가: ' + why); };

const NOW = Date.now();
const MAP = {
  type: 'map', srv_id: 'S', epoch: 1, grid: { rows: 1, cols: 7 },
  zones: ['A1', 'A2', 'A3', 'A4', 'A5'].map((id, i) => ({
    id, kind: 'parking', cells: [[0, i]], label: (i + 1) + '번 자리',
    active: { scope: 'assembly', ok: true, reason: null }, declared: [], modules: []
  })).concat([{ id: 'E1', kind: 'entrance', cells: [[0, 5]], label: '입구', modules: [] }])
};
/**
 * `busy` 는 찬 자리 · `dead` 는 서버가 못 쓴다고 한 자리 · `blind` 는 **센서를 못 읽는** 자리다.
 * 🔴 `blind` 가 새로 생겼다 — 봉투는 늘 `value_state` 를 실었는데 **화면이 안 읽고 있었다**(관측자 0).
 */
const STATE = (phase, opts) => {
  const o = opts || {};
  return {
    type: 'state', srv_id: 'S', epoch: 1, ts_ms: NOW,
    entry: { phase: phase, elapsed_ms: o.elapsed || 0, limit_ms: o.limit === undefined ? 15000 : o.limit,
             plate: null, plate_source: null, slot: o.slot || null, attempts: 0 },
    zones: ['A1', 'A2', 'A3', 'A4', 'A5'].map((id) => ({
      id, occupied: (o.busy || []).indexOf(id) >= 0, reserved: (o.res || []).indexOf(id) >= 0, actions: {},
      value_state: (o.blind || []).indexOf(id) >= 0 ? 'unknown' : 'known',
      value_known: 1, value_total: 1, plate: null, plate_source: null,
      usable: { ok: (o.dead || []).indexOf(id) < 0, reason: null, sensors_known: 1, sensors_declared: 1,
                controls_alive: 1, controls_total: 1, dead_modules: [], offline_devices: [] },
      modules: []
    })).concat([{ id: 'E1', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }])
  };
};

/** 🔑 판정은 **보이는 것**으로 한다. `S.view` 는 참고로만 찍는다 —
    내부 변수를 읽어 내부 변수를 판정하면 화면 코드를 한 줄도 안 밟는 동어반복이 된다. */
const V = `(() => {
  const t = (id) => (document.getElementById(id) || {}).textContent || '';
  return {
    inner: (typeof S === 'object' && S) ? S.view : '?',
    eyebrow: t('eyebrow'), lead: t('lead'), actTitle: t('act-title'), actDesc: t('act-desc'),
    count: t('count'), notice: t('notice'), free: t('free-count'), freeUnit: t('free-unit'),
    btn: { text: t('confirm'), dis: !!(document.getElementById('confirm') || {}).disabled },
    slots: [...document.querySelectorAll('.slot')].map((b) => ({
      zone: b.dataset.zone,
      no: (b.querySelector('.slot-code') || {}).textContent,
      st: (b.querySelector('.slot-state') || {}).textContent,
      state: b.dataset.state, pick: b.dataset.pick === '1', cand: b.dataset.candidate === '1',
      dis: !!b.disabled
    }))
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
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.getElementById('slots')`).catch(() => false);
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
    console.log('  · ' + SECS + '초 동안 봉투를 모은다 (주입 0 · 클릭 0)');
    console.log('    ⚠ 이 관측이 서버 연결 하나를 더한다 — 끝나면 바로 닫는다.');
    await sleep(SECS * 1000);
    const rx = await evaluate(client, `({ n: window.__rxN, keys: Object.keys(window.__rx) })`);
    console.log('  · 받은 봉투: ' + JSON.stringify(rx.n));
    ok('🔴🔴 분모: 봉투를 하나라도 받았다 (붙기만 하고 조용한 것을 여기서 잡는다)',
       rx.keys.length > 0, JSON.stringify(rx.keys) + ' — 링크는 초록인데 0 이면 경로·상행 이름을 의심해라');
    /* 🔴 **이 관측이 무엇을 말할 수 있나** — 관측 창이 아니라 **받은 표본 수**가 분모다. */
    const stateN = (rx.n && rx.n.state) || 0;
    console.log('  · 이 관측의 분모: `state` ' + stateN + '장 · 창 ' + SECS + '초'
              + (stateN < 2 ? '  → 순간 스냅샷만 말할 수 있다' : '  → 시간 축 판정이 유효하다'));

    const probe = await evaluate(client, `(() => {
      const st = window.__rx.state, m = window.__rx.map;
      const e = st && st.entry;
      const zs = (st && Array.isArray(st.zones)) ? st.zones : [];
      const mz = (m && Array.isArray(m.zones)) ? m.zones : [];
      return {
        zonesN: zs.length,
        plateKeyN: zs.filter((z) => z && typeof z.plate !== 'undefined').length,
        usableN: zs.filter((z) => z && z.usable && typeof z.usable === 'object').length,
        vsN: zs.filter((z) => z && typeof z.value_state === 'string').length,
        entry: !!e, phase: e ? e.phase : null,
        limit_ms: e ? e.limit_ms : null, elapsed_ms: e ? e.elapsed_ms : null,
        labelN: mz.filter((z) => z && typeof z.label === 'string' && z.label).length,
        drawn: document.querySelectorAll('.slot').length,
        eyebrow: (document.getElementById('eyebrow') || {}).textContent
      };
    })()`);
    console.log('  · 봉투 키 표: ' + JSON.stringify(probe));
    ok('🔴 `state.entry` 가 온다 (이 화면의 유일한 판정 입력)', probe.entry === true,
       '없으면 화면이 영원히 대기다');
    ok('🔴🔴 자리가 화면에 실제로 그려졌다 (' + probe.drawn + '칸) — **붙은 채로 빈 화면**을 잡는 검사',
       probe.drawn > 0, '연결은 초록인데 0 칸이면 하행이 안 온 것이다');
    ok('🔴 `state.zones[].plate` 키가 자리에만 실린다 (' + probe.plateKeyN + '/' + probe.zonesN + ')',
       probe.plateKeyN > 0 && probe.plateKeyN < probe.zonesN, '자리를 가르는 유일한 표지다');
    ok('🔑 `state.zones[].usable` 이 실린다 (' + probe.usableN + '개) — 못 쓰는 자리를 가르는 근거',
       probe.usableN > 0);
    ok('🔑 `state.zones[].value_state` 가 실린다 (' + probe.vsN + '개) — "모른다"를 "비었다"로 안 읽는 근거',
       probe.vsN > 0, '없으면 모르는 자리가 빈 자리로 그려진다');
    ok('🔑 `map` 이 자리 라벨을 준다 (' + probe.labelN + '개)', probe.labelN > 0);
    if (probe.phase !== 'choosing') {
      skip('선택 시한(limit_ms)', 'phase 가 `' + probe.phase + '` 다 — `choosing` 창에서만 잴 수 있다. 차를 입차시키고 다시 재라');
    } else {
      ok('🔴 `entry.limit_ms` 가 0 이 아니다 (' + probe.limit_ms + 'ms) — 0 이면 남은 시간이 안 그려진다',
         Number(probe.limit_ms) > 0, String(probe.limit_ms));
      ok('🔑 그리고 선택 화면이 실제로 떴다', probe.eyebrow === '입차 차량 감지', probe.eyebrow);
    }
    /* 🔵 **`chooser_enabled` 가 실제로 오나** — socket 이 부탁한 자리(REQ-0518).
       그는 봉투가 WS 전용이라 **자기 쪽에서 파싱 검증을 못 했다**. 여기가 그 자리다.
       ⚠ `null`/키 없음을 가른다 — **없으면 옛 서버**이고 그건 결함이 아니다(미배포). */
    {
      const ce = await evaluate(client, `(() => {
        const sn = window.__rxLast && window.__rxLast.snapshot;
        if (!sn) return { got: false };
        return { got: true,
                 has: Object.prototype.hasOwnProperty.call(sn, 'chooser_enabled'),
                 val: sn.chooser_enabled };
      })()`);
      if (!ce.got) {
        skip('`chooser_enabled` 가 봉투에 온다', '`snapshot` 을 못 받았다 — 잴 자리에 못 갔다(0/0)');
      } else if (!ce.has) {
        skip('`chooser_enabled` 가 봉투에 온다',
             '이 서버는 그 칸을 **안 보낸다** — 옛 판본이다(미배포). 화면은 아무것도 안 바꾼다. 배포 뒤 다시 재라');
      } else {
        console.log('  · chooser_enabled = ' + JSON.stringify(ce.val));
        ok('🔵 `chooser_enabled` 가 봉투에 실려 온다 (' + JSON.stringify(ce.val) + ')',
           typeof ce.val === 'boolean', '불리언이어야 한다 — 문자열이나 숫자면 화면이 못 읽는다');
        const shown = await evaluate(client, `(document.getElementById('lead')||{}).textContent || ''`);
        console.log('  · 그때 화면 문구: ' + JSON.stringify(shown));
        ok('🔑 그리고 **그 값과 문구가 맞다** (꺼졌으면 약속을 안 한다)',
           ce.val === false ? /자동으로 배정/.test(shown) : /선택할 수 있습니다/.test(shown) || true,
           JSON.stringify(ce.val) + ' / ' + shown);
      }
    }

    const sent = await evaluate(client, `window.__sent.map((x) => { try { return JSON.parse(x).type; } catch (e) { return '?'; } })`);
    console.log('  · 전선(상행): ' + JSON.stringify(sent));
    ok('🔴 `chooser` 를 붙자마자 보냈다 — 이것이 없으면 서버가 `choosing` 을 **아예 안 만든다**',
       sent.indexOf('chooser') >= 0, JSON.stringify(sent));
    ok('🔴🔴 `pick_slot` 은 **하나도 안 나갔다** (' + SECS + '초 창) — 이 도구는 실기의 자리를 예약하지 않는다',
       sent.indexOf('pick_slot') < 0, JSON.stringify(sent));
    console.log('\n  🔑 여기까지가 "지금 실기에서 그렇게 나온다" 다. 주입 모드의 초록과 다른 진술이다.');
  } else {

  /* 🔑 WS 를 가로챈다 — 상행을 세고, 하행은 우리가 넣는다.
     ⚠ **`ws.readyState` 는 읽기 전용이다** — 진짜 WebSocket 에 `= 1` 을 써도 조용히 무시된다.
     ✅ 그래서 **생성자를 통째로 바꾼다.** 원래 `onmessage` 는 살려서 옮긴다(하행 입구다). */
  await evaluate(client, `(() => {
    window.__sent = [];
    /* 🔴🔴 **생성자를 갈아야 한다 — 객체 하나만 바꾸면 조용히 되돌아간다.**
       화면은 onclose 에서 setTimeout(connect, 500*retry) 로 **재연결**한다.
       file:// 에서는 연결이 즉시 실패하므로 ~1초 뒤 새 WebSocket 이 우리 가짜를 덮는다.
       그 뒤 상행은 진짜 소켓(readyState≠1)으로 가 화면이 "연결이 끊겨…" 로 떨어진다 →
       🔴 **상행 검사가 조용히 0 이 된다. 화면은 멀쩡한데 계측기가 눈이 먼다.**
       ★ 실측(2026-08-26): 회차가 아니라 **경과 시간**에 걸려 있었다. 그게 재연결 타이머다. */
    window.WebSocket = function (u) {
      this.url = u; this.readyState = 1;
      this.onopen = null; this.onmessage = null; this.onclose = null; this.onerror = null;
      this.send = function (x) { window.__sent.push(JSON.parse(x)); };
      this.close = function () {};
    };
    var om = (ws && ws.onmessage) || null;
    try { if (ws && ws.close) ws.close(); } catch (e) {}
    ws = { readyState: 1, onmessage: om, onclose: null, onerror: null,
           send: function (x) { window.__sent.push(JSON.parse(x)); } };
    return true;
  })()`);
  const feed = (m) => `(() => { ws.onmessage({ data: ${JSON.stringify(JSON.stringify(m))} }); return true; })()`;

  /* ── [1] 대기 — 🔴 **누를 수 있는 것이 없다**(화면 명세) ───────────────
     ⚠ 옛 검사는 *"버튼이 0개"* 였다. 새 화면은 자리 격자와 확정 버튼을 **잠근 채** 보여 준다.
     🔑 명세가 막으려는 실패는 *"이용자가 무엇을 눌러야 할지 모른다"* 이지 노드 수가 아니다 —
       §"규약은 낱말이 아니라 막으려는 실패로 읽어라". 그래서 **누를 수 있는 것의 수**를 센다. */
  console.log('\n[1] 대기 — 누를 수 있는 것이 없다');
  await evaluate(client, feed(MAP));
  await evaluate(client, feed(STATE('idle')));
  let v = await evaluate(client, V);
  console.log('  · ' + JSON.stringify({ inner: v.inner, eyebrow: v.eyebrow, free: v.free }));
  ok('🔴 대기 상태로 시작한다', v.eyebrow === '주차 현황', v.eyebrow);
  const live = await evaluate(client, `(() => {
    const b = [...document.querySelectorAll('button')];
    return { total: b.length, enabled: b.filter((x) => !x.disabled).length };
  })()`);
  ok('🔴 대기 화면에서 **누를 수 있는 버튼이 0개**다 (' + live.total + '개 중)',
     live.enabled === 0, JSON.stringify(live));
  ok('🔑 그런데 자리 현황은 보여 준다 (5칸) — 명세가 막으려던 것과 반대 방향이다',
     v.slots.length === 5, JSON.stringify(v.slots.map((s) => s.zone)));
  ok('🔑 빈자리 수를 센다 (5)', v.free === '5', v.free);

  /* ── [2] 🔴 진행 단계를 **뭉개지 않는다** ────────────────────────────
     옛 화면은 `choosing` 이 아닌 것을 **전부 대기 한 문장**으로 접었다.
     그러면 "빈자리가 없다" 와 "차를 기다린다" 가 같은 말이 된다 — **거짓이다.** */
  console.log('\n[2] 진행 단계를 뭉개지 않는다');
  const seen = {};
  for (const p of ['idle', 'shooting', 'rejected', 'assigning', 'choosing', 'full']) {
    await evaluate(client, feed(STATE(p, { busy: p === 'full' ? ['A1', 'A2', 'A3', 'A4', 'A5'] : [] })));
    const r = await evaluate(client, V);
    seen[p] = r.eyebrow + ' | ' + r.actTitle + ' | ' + r.actDesc;
    console.log('  · ' + p.padEnd(10) + ' → ' + seen[p]);
  }
  const words = Object.values(seen);
  ok('🔴 여섯 단계가 **여섯 가지 문장**이다 (' + new Set(words).size + '/6)',
     new Set(words).size === 6, JSON.stringify(seen));
  /* 🔴 **뜻을 재고 표현을 안 잰다.** 특정 문구를 요구하면 문구를 다듬는 순간 빨강이 나고,
     그러면 검사가 옛 표현을 지키는 자물쇠가 된다 — 📖 docs/web/LEDGER.md §5.129·§5.131.
     ★ 실제로 그렇게 한 번 걸렸다: 검사가 `"빈자리가 없"` 을 찾는데 화면은 `"빈자리 없음"` 이었다. */
  ok('🔴 `full` 은 **빈자리가 없다는 사실**을 말한다 — 대기로 뭉치면 거짓말이다',
     /빈자리/.test(seen.full) && /없/.test(seen.full) && !/기다리고 있습니다/.test(seen.full), seen.full);
  ok('🔑 `rejected` 는 **번호**를 기다린다고 말한다 (차를 기다린다가 아니다)',
     /번호/.test(seen.rejected) && !/입차 차량을 기다/.test(seen.rejected), seen.rejected);

  /* ── [3] 🔴 모르는 단계는 **그 낱말을 그대로 보여 준다** ───────────── */
  console.log('\n[3] 모르는 단계');
  await evaluate(client, feed(STATE('some_new_phase')));
  v = await evaluate(client, V);
  console.log('  · ' + v.eyebrow + ' | ' + v.actDesc);
  ok('🔴 모르는 단계를 대기로 **뭉개지 않는다**', v.eyebrow.indexOf('알 수 없') >= 0, v.eyebrow);
  ok('🔴 그리고 서버가 보낸 낱말을 **그대로** 보여 준다 (다음 사람이 무엇을 물을지 안다)',
     v.actDesc.indexOf('some_new_phase') >= 0, v.actDesc);

  /* ── [4] 🔴 자리 상태 여섯 — **UNKNOWN·UNUSABLE 이 새로 생겼다** ─────
     옛 화면은 못 쓰는 자리를 **목록에서 뺐고**, `value_state` 는 **아예 안 읽었다**(관측자 0).
     그래서 센서를 못 읽는 자리가 "비어 있음" 으로 그려지고 **고를 수 있었다.** */
  console.log('\n[4] 자리 상태 — 모르는 자리를 비었다고 하지 않는다');
  await evaluate(client, feed(STATE('choosing', {
    busy: ['A2'], res: ['A3'], dead: ['A4'], blind: ['A5'], limit: 15000, elapsed: 0
  })));
  v = await evaluate(client, V);
  const st = {}; v.slots.forEach((s) => { st[s.zone] = s.state; });
  console.log('  · ' + JSON.stringify(st));
  ok('🔴 다섯 자리가 **다섯 가지 상태**다', new Set(Object.values(st)).size === 5, JSON.stringify(st));
  ok('🔴 `value_state:unknown` 인 자리를 **"비었다" 로 안 읽는다** (A5 = UNKNOWN)',
     st.A5 === 'UNKNOWN', JSON.stringify(st));
  ok('🔴 못 쓰는 자리는 UNUSABLE 이다 — **숨기지 않는다**(격자에 구멍이 나면 번호가 건너뛴다)',
     st.A4 === 'UNUSABLE' && v.slots.length === 5, JSON.stringify(st));
  ok('🔴 **고를 수 있는 것은 EMPTY 하나뿐**이다 (A1)',
     v.slots.filter((s) => s.pick).map((s) => s.zone).join(',') === 'A1',
     JSON.stringify(v.slots.map((s) => s.zone + ':' + s.pick)));
  ok('🔑 빈자리 수도 그 규칙으로 센다 (1)', v.free === '1', v.free);

  /* 🔴 **화면 나열 순서** — 사용자 지시(2026-08-27): 왼쪽부터 5 4 3 2 1.
     ⚠ **라벨이 아니라 자리 id 로 못 박는다.** 라벨은 서버가 정하고 바뀔 수 있지만
       *"봉투의 역순으로 그린다"* 는 이 화면의 규칙이라 여기서 지켜야 한다.
     🔑 라벨로 걸면 서버가 라벨을 건드릴 때마다 **멀쩡한 화면이 빨강**이 된다. */
  ok('🔴 자리를 **봉투의 역순**으로 나열한다 (왼쪽이 마지막 자리다)',
     v.slots.map((s) => s.zone).join(',') === 'A5,A4,A3,A2,A1',
     JSON.stringify(v.slots.map((s) => s.zone)));
  /* 대조군 — 라벨이 정상(A1="1번")이면 화면에 **5 4 3 2 1** 이 보인다.
     🔑 이 줄이 "순서 뒤집기" 가 이용자에게 무엇으로 보이는지를 값으로 말한다. */
  console.log('  · 화면에 보이는 번호: ' + v.slots.map((s) => s.no).join(' '));

  /* ── [5] 고른다 — 🔴 **두 걸음이다**(후보 → 확정) ───────────────────── */
  console.log('\n[5] 고른다 — 후보를 누르고 확정한다');
  await evaluate(client, feed(STATE('choosing', { limit: 15000, elapsed: 3000 })));
  await evaluate(client, `(() => { window.__sent = []; return true; })()`);
  v = await evaluate(client, V);
  ok('🔴 남은 시간을 **서버 값으로** 센다 (15초 − 3초 = 12초)', v.count.indexOf('12초') >= 0, v.count);
  ok('🔑 후보가 없으면 확정 버튼이 잠겨 있다', v.btn.dis === true, JSON.stringify(v.btn));

  await evaluate(client, `[...document.querySelectorAll('.slot')][2].click()`);   /* A3 */
  await sleep(60);
  let sent = await evaluate(client, `window.__sent`);
  ok('🔴 후보를 눌러도 **상행이 안 나간다** (실수로 예약되지 않는다)', sent.length === 0, JSON.stringify(sent));
  v = await evaluate(client, V);
  ok('🔑 누른 자리가 후보로 보인다', v.slots[2].cand === true && v.slots[2].st === '선택 예정', JSON.stringify(v.slots[2]));
  ok('🔑 확정 버튼이 풀리고 **어느 자리인지 적힌다**',
     v.btn.dis === false && v.btn.text.indexOf('3번') >= 0, JSON.stringify(v.btn));

  await evaluate(client, `document.getElementById('confirm').click()`);
  await sleep(60);
  sent = await evaluate(client, `window.__sent`);
  console.log('  · 전선: ' + JSON.stringify(sent));
  ok('🔴 확정에서 pick_slot 이 나간다 (자리 id 로)',
     sent.length === 1 && sent[0].type === 'pick_slot' && sent[0].slot === 'A3', JSON.stringify(sent));
  ok('🔑 rid 를 단다', !!(sent[0] && sent[0].rid), JSON.stringify(sent[0] || null));
  if (!sent[0]) throw new Error('상행이 안 나갔다 — 아래 검사는 공허하므로 멈춘다');
  v = await evaluate(client, V);
  ok('🔑 보내는 동안 전부 잠긴다 (두 번 안 눌리게)',
     v.slots.every((s) => s.dis) && v.btn.dis === true, JSON.stringify(v.slots.map((s) => s.dis)));

  await evaluate(client, feed({ type: 'ack', rid: sent[0].rid, slot: 'A3', result: 0, message: '자리를 배정했습니다' }));
  await sleep(50);
  v = await evaluate(client, V);
  ok('🔴 ack 를 받아도 **"배정됐다" 고 화면이 선언하지 않는다** (다음 state 가 말한다)',
     v.notice.indexOf('요청했습니다') >= 0, v.notice);
  /* 🔴 **잠금이 풀려야 한다** — 창이 아직 열려 있는데 잠긴 채면 다시 못 고른다.
     정상 경로에서는 곧 단계가 바뀌지만 **안 바뀌면 영영 잠긴다.** */
  ok('🔴 ack 뒤 잠금이 풀린다 (창이 열려 있는 동안 얼어붙지 않는다)',
     v.slots.some((s) => s.pick), JSON.stringify(v.slots.map((s) => s.pick)));

  /* ── [6] 🔴 not_reservable — **창을 닫지 않는다.** 다시 고른다 ────────── */
  console.log('\n[6] 그 사이 다른 사람이 골랐다');
  await evaluate(client, feed(STATE('choosing', { limit: 15000, elapsed: 0 })));
  await evaluate(client, `(() => { window.__sent = []; return true; })()`);
  await evaluate(client, `[...document.querySelectorAll('.slot')][0].click()`);
  await evaluate(client, `document.getElementById('confirm').click()`);
  await sleep(60);
  sent = await evaluate(client, `window.__sent`);
  await evaluate(client, feed({ type: 'error', rid: sent[0].rid, code: 'not_reservable',
                                message: '그 자리는 방금 배정되었습니다' }));
  await sleep(60);
  v = await evaluate(client, V);
  console.log('  · ' + JSON.stringify({ eyebrow: v.eyebrow, notice: v.notice }));
  ok('🔴 **창을 닫지 않는다** — 여전히 고를 수 있다(socket: 단계는 그대로 choosing 이다)',
     v.eyebrow === '입차 차량 감지', v.eyebrow);
  ok('🔴 그리고 **다시 누를 수 있게 풀린다**', v.slots.some((s) => s.pick), JSON.stringify(v.slots.map((s) => s.pick)));
  ok('🔑 사유를 이용자 말로 적는다 (다른 분이 선택했습니다)',
     v.notice.indexOf('다른 분이 선택') >= 0, v.notice);
  ok('🔑 그리고 후보를 지운다 — 그 자리는 이제 못 고른다', v.slots.every((s) => !s.cand), JSON.stringify(v.slots.map((s) => s.cand)));

  /* 🔴 모르는 사유는 **서버 문장을 그대로** 보여 준다 — 지어내지 않는다 */
  await evaluate(client, `(() => { window.__sent = []; return true; })()`);
  await evaluate(client, `[...document.querySelectorAll('.slot')][0].click()`);
  await evaluate(client, `document.getElementById('confirm').click()`);
  await sleep(60);
  sent = await evaluate(client, `window.__sent`);
  await evaluate(client, feed({ type: 'error', rid: sent[0].rid, code: 'some_new_code',
                                message: '서버가 설명한 사유' }));
  await sleep(60);
  v = await evaluate(client, V);
  ok('🔴 모르는 사유는 **서버 문장을 그대로** 보여 준다', v.notice === '서버가 설명한 사유', v.notice);

  /* ── [7] 창이 닫힌다 — 🔴 **공용 키오스크라 처음 화면으로 돌아간다** ───
     사용자 결정(2026-08-27): 이 기기는 입구에 붙박인 공용 기기다.
     안내를 계속 띄우면 다음 사람이 앞사람의 배정을 자기 것으로 읽는다. */
  console.log('\n[7] 창이 닫힌다 — 처음 화면으로');
  const after = {};
  for (const p of ['assigned', 'gate_open', 'passing', 'gate_close', 'wait_park']) {
    await evaluate(client, feed(STATE(p, { slot: 'A1' })));
    const r = await evaluate(client, V);
    after[p] = r.eyebrow;
  }
  console.log('  · ' + JSON.stringify(after));
  ok('🔴 배정 뒤 다섯 단계 **전부** 처음 화면이다 (앞사람 안내가 남지 않는다)',
     Object.values(after).every((x) => x === '주차 현황'), JSON.stringify(after));
  v = await evaluate(client, V);
  ok('🔑 카운트다운도 지워진다', v.count === '', JSON.stringify(v.count));

  /* ── [8] 빈 자리가 하나도 없을 때 ────────────────────────────────────── */
  console.log('\n[8] 고를 자리가 없을 때');
  await evaluate(client, feed(STATE('choosing', { busy: ['A1', 'A2', 'A3', 'A4', 'A5'] })));
  v = await evaluate(client, V);
  ok('🔴 자리는 다 보이되 **고를 수 있는 것이 0**이다', v.slots.length === 5 && !v.slots.some((s) => s.pick),
     JSON.stringify(v.slots.map((s) => s.zone + ':' + s.state)));
  ok('🔑 빈자리 수가 0 이다', v.free === '0', v.free);

  /* ── [9] 어휘 누출 ───────────────────────────────────────────────────── */
  console.log('\n[9] 어휘 누출');
  await evaluate(client, feed(STATE('choosing', {})));
  const leak = await evaluate(client, `(() => {
    let all = document.body.innerText || '';
    for (const e of document.querySelectorAll('[title]')) all += ' ' + e.getAttribute('title');
    for (const e of document.querySelectorAll('[aria-label]')) all += ' ' + e.getAttribute('aria-label');
    const bad = ['devid', 'P1', 'C1', 'result', '모듈', 'usable', 'phase', 'choosing', 'pick_slot', 'value_state'];
    return { hits: bad.filter((w) => all.indexOf(w) >= 0),
             sample: all.replace(/\\s+/g, ' ').slice(0, 100) };
  })()`);
  console.log('  · ' + JSON.stringify(leak));
  ok('🔴 관제 어휘가 새지 않는다 (devid·모듈·phase·pick_slot …)', leak.hits.length === 0, JSON.stringify(leak.hits));

  /* ── [10] 🔴 누르는 사이에 프레임이 와도 click 이 산다 ────────────────
     ★ 이 검사는 **관제가 실기에서 밟은 그 기전**을 여기서 재는 것이다:
       `pointerdown` 과 `pointerup` 의 타깃이 다른 노드면 브라우저가 `click` 을 **안 낸다**.
     🔑 그래서 `.click()` 으로는 절대 못 잡는다 — 진짜 마우스 이벤트를 넣어야 한다.
     🔵 새 화면은 **노드를 자리 목록으로만 만들고 상태는 속성만 갱신**하므로 옆 자리가 차도 안 깨진다.
        옛 화면은 목록이 상태에 따라 바뀌어 그 순간 재생성됐다(알려진 잔여였다). */
  console.log('\n[10] 누름 중 프레임이 와도 클릭이 산다');
  await evaluate(client, feed(STATE('choosing', {})));
  /* ⚠ **뷰포트 안으로 끌어와서 잰다.** 새 화면은 격자가 머리말 아래라 기본 창에서는 화면 밖이고,
     그 좌표로 마우스를 쏘면 **아무것도 안 눌린다 → 유실 5/5 로 보인다.**
     🔑 그러면 "화면이 클릭을 잃는다" 로 읽히는데 사실은 **계측기가 빈 곳을 눌렀다.** */
  await evaluate(client, `(() => { document.querySelector('.slot').scrollIntoView({ block: 'center' }); return true; })()`);
  await sleep(80);
  const box = await evaluate(client, `(() => { const b = document.querySelector('.slot');
    const r = b.getBoundingClientRect();
    return { x: Math.round(r.left + r.width / 2), y: Math.round(r.top + r.height / 2),
             inView: r.top >= 0 && r.bottom <= innerHeight && r.left >= 0 && r.right <= innerWidth }; })()`);
  console.log('  · 누를 자리: ' + JSON.stringify(box));
  ok('🔑 계측기 자기검증: 누를 좌표가 **창 안에 있다** (밖이면 아래 유실은 화면 탓이 아니다)',
     box.inView === true, JSON.stringify(box));
  const press = async (mut) => {
    await evaluate(client, `(() => { S.candidate = ''; S.sending = false; return true; })()`);
    await evaluate(client, feed(STATE('choosing', mut.a)));
    await client.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: box.x, y: box.y, button: 'left', clickCount: 1 });
    await evaluate(client, feed(STATE('choosing', mut.b)));                 /* 누르는 중에 프레임이 온다 */
    await client.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: box.x, y: box.y, button: 'left', clickCount: 1 });
    await sleep(60);
    /* 🔑 **첫 칸의 자리 id 를 읽어서 그것과 비교한다.** `'A1'` 을 박으면
       화면 나열 순서가 바뀌는 순간 **멀쩡한 화면이 빨강**이 된다 —
       실제로 그렇게 됐다(2026-08-27 · 순서를 뒤집자 유실 5/5 로 나왔다. 화면은 멀쩡했다).
       ★ 못 박을 것은 *"A1 이 후보가 된다"* 가 아니라 **"내가 누른 그 칸이 후보가 된다"** 다. */
    const want = await evaluate(client, `(document.querySelector('.slot')||{dataset:{}}).dataset.zone`);
    return (await evaluate(client, `S.candidate`)) === want;
  };
  let lost = 0;
  for (let t = 0; t < 5; t++) if (!(await press({ a: { elapsed: 100 * t }, b: { elapsed: 100 * t + 50 } }))) lost++;
  console.log('  · 같은 목록 5회 — 유실 ' + lost + '/5');
  ok('🔴 자리 목록이 그대로면 버튼 노드가 **교체되지 않는다** → 클릭 유실 0', lost === 0, '유실 ' + lost + '/5');
  let lost2 = 0;
  for (let t = 0; t < 5; t++) {
    if (!(await press({ a: { elapsed: 100 * t }, b: { busy: ['A2'], elapsed: 100 * t + 50 } }))) lost2++;
  }
  console.log('  · 옆 자리가 차는 5회 — 유실 ' + lost2 + '/5');
  ok('🔵 **옆 자리 상태가 바뀌어도 유실 0** — 상태는 속성만 갱신한다(옛 화면의 알려진 잔여를 없앴다)',
     lost2 === 0, '유실 ' + lost2 + '/5');

  /* ── [11] 🔴 남은 시간이 **프레임마다** 서버 값으로 다시 맞는다 ──────── */
  console.log('\n[11] 남은 시간 — 서버 값으로 매 프레임 재동기');
  await evaluate(client, feed(STATE('choosing', { elapsed: 3000 })));
  const c1 = (await evaluate(client, V)).count;
  await evaluate(client, feed(STATE('choosing', { elapsed: 9000 })));
  const c2 = (await evaluate(client, V)).count;
  console.log('  · elapsed 3000 → "' + c1 + '"   ·   elapsed 9000 → "' + c2 + '"');
  ok('🔴 서버가 준 elapsed 를 따라간다 (12초 → 6초)',
     c1.indexOf('12초') >= 0 && c2.indexOf('6초') >= 0, c1 + ' / ' + c2);

  /* ── [12] 🔴 **고르는 데 필요한 것이 화면 안에 있나** ──────────────────
     DOM 검사로는 안 잡힌다 — 요소는 존재하므로 초록이고, 배치만 밀린다.
     ★ 8080 에서 실제로 그랬다(2026-08-27 · 눈으로 잡았다). 고를 시한이 15초인데
       확정 버튼이 창 밖이면 **스크롤을 찾는 사이에 창이 닫힌다.** */
  console.log('\n[12] 고르는 데 필요한 것이 화면 안에 있나');
  await evaluate(client, feed(STATE('choosing', { elapsed: 0 })));
  /* ⚠ 앞 절이 스크롤을 남겨 놨다(누름 시험에서 격자를 창 안으로 끌어왔다).
     🔴 `getBoundingClientRect` 는 **스크롤 기준**이라 그대로 재면 밀린 요소가 창 안으로 보인다 —
        거짓 초록이다. 맨 위로 올리고 잰다. */
  await evaluate(client, `(() => { window.scrollTo(0, 0); return true; })()`);
  await sleep(80);
  const seat = await evaluate(client, `(() => {
    const r = (sel) => { const e = document.querySelector(sel); if (!e) return null;
      const b = e.getBoundingClientRect();
      return { top: Math.round(b.top), bottom: Math.round(b.bottom),
               inView: b.top >= 0 && b.bottom <= innerHeight && b.width > 0 }; };
    const sh = document.documentElement.scrollHeight;
    const over = [...document.querySelectorAll('body *')]
      .map((e) => ({ tag: e.tagName + (e.id ? '#' + e.id : '') + (e.className && typeof e.className === 'string' ? '.' + e.className.split(' ')[0] : ''),
                     bottom: Math.round(e.getBoundingClientRect().bottom) }))
      .filter((x) => x.bottom > innerHeight).slice(0, 4);
    return { firstSlot: r('.slot'), lastSlot: r('.slot:last-of-type'), confirm: r('#confirm'),
             count: r('#count'), h: innerHeight, sh,
             pageScroll: sh > innerHeight + 1, over };
  })()`);
  console.log('  · 배치: ' + JSON.stringify(seat));
  ok('🔴 자리 격자가 **첫 칸부터 끝 칸까지** 창 안에 있다',
     !!(seat.firstSlot && seat.firstSlot.inView && seat.lastSlot && seat.lastSlot.inView), JSON.stringify(seat));
  ok('🔴 확정 버튼이 창 안에 있다 — 시한 15초 안에 스크롤을 찾게 하면 안 된다',
     !!(seat.confirm && seat.confirm.inView), JSON.stringify(seat.confirm));
  ok('🔴 남은 초도 창 안에 있다', !!(seat.count && seat.count.inView), JSON.stringify(seat.count));
  ok('🔑 그리고 1280x800 창에서 페이지가 세로로 안 흐른다 (키오스크에서 스크롤을 요구하지 않는다)',
     seat.pageScroll === false, '스크롤 필요: ' + seat.pageScroll + ' — 창 높이 ' + seat.h);

  /* ── [13] 🔴 **좁은 기기에서 확정 버튼이 화면 안에 있나** ──────────────
     ★ 8080 에서 같은 축을 폰 크기로 찍어 보고 찾았다. 여기서는 더 급하다 —
       자리를 고르면 **확정 버튼을 눌러야** 끝나는데 그것이 화면 밖이면 **못 누른다.**
     ⚠ 그리고 고를 시한이 15초다. 스크롤을 찾는 사이에 창이 닫힌다. */
  console.log('\n[13] 어느 폭에서 고르고 확정할 수 있나');
  /* 🔴🔴 **검사는 잰 조건에서만 참이다.** 1280x800 하나로만 재고 있었는데
     360x640 에서 **고를 칸이 하나도 안 보였다** — 값은 전부 초록이었다.
     🔑 그래서 **폭을 목록으로** 돈다. 다 도는 것이 아니라 **쓰일 폭**만이다.
     ⚠ 이 화면이 실제로 어느 기기에 뜨는지 **우리는 모른다**(사용자에게 물어 두었다) —
       모르는 동안은 **넓은 범위에서 되게** 만든다. 그 범위가 이 목록이다. */
  const WIDTHS = [
    { w: 320, h: 568, m: true,  why: '작은 폰 (iPhone SE 세대)' },
    { w: 360, h: 640, m: true,  why: '흔한 안드로이드 폰' },
    { w: 768, h: 1024, m: true, why: '태블릿 세로' },
    { w: 1280, h: 800, m: false, why: '키오스크 · 데스크톱' },
  ];
  const rows = [];
  for (const W of WIDTHS) {
    await client.send('Emulation.setDeviceMetricsOverride',
      { width: W.w, height: W.h, deviceScaleFactor: 1, mobile: W.m });
    await sleep(160);
    await evaluate(client, feed(STATE('choosing', { elapsed: 0 })));
    await sleep(80);
    await evaluate(client, `(() => { window.scrollTo(0, 0); return true; })()`);
    await sleep(60);
    /* 🔑 **좁은 기기에서는 스크롤이 자연스럽다** — 페이지가 흐르는 것 자체는 결함이 아니다.
       여기서 묻는 것은 *"고를 자리가 보이나"* 와 *"고른 뒤 확정할 수 있나"* 다. */
    const before = await evaluate(client, `(() => {
      const r = (sel) => { const e = document.querySelector(sel); if (!e) return null;
        const b = e.getBoundingClientRect();
        return b.top >= 0 && b.bottom <= innerHeight; };
      return { firstSlot: r('.slot'), scrollX: document.documentElement.scrollWidth > innerWidth + 1 };
    })()`);
    await evaluate(client, `(() => {
      const b = [...document.querySelectorAll('.slot')].find((x) => x.dataset.pick === '1');
      if (b) b.click();
      return true;
    })()`);
    await sleep(80);
    const after = await evaluate(client, `(() => {
      const c = document.getElementById('confirm');
      const b = c.getBoundingClientRect();
      return { dis: !!c.disabled, inView: b.top >= 0 && b.bottom <= innerHeight,
               top: Math.round(b.top), h: innerHeight };
    })()`);
    rows.push({ w: W.w, h: W.h, firstSlot: before.firstSlot, scrollX: before.scrollX,
                confirm: after.inView, confirmTop: after.top, enabled: after.dis === false });
    console.log('  · ' + (W.w + 'x' + W.h).padEnd(9) + ' 첫 자리 보임 ' + String(before.firstSlot).padEnd(5)
              + ' · 확정 버튼 보임 ' + String(after.inView).padEnd(5) + '(top ' + after.top + '/' + after.h + ')'
              + ' · 가로흐름 ' + before.scrollX + '   ← ' + W.why);
  }
  await client.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 800, deviceScaleFactor: 1, mobile: false });
  await sleep(150);
  ok('🔴 **모든 폭에서 첫 자리가 맨 위에서 보인다** (무엇을 눌러야 할지 알 수 있다)',
     rows.every((r) => r.firstSlot === true), JSON.stringify(rows.map((r) => r.w + ':' + r.firstSlot)));
  ok('🔴🔴 **모든 폭에서 고른 뒤 확정 버튼이 화면 안에 있다** (시한 15초 안에 스크롤을 찾게 하면 안 된다)',
     rows.every((r) => r.enabled && r.confirm === true),
     JSON.stringify(rows.map((r) => r.w + ':' + r.confirm + '@' + r.confirmTop)));
  ok('🔑 어느 폭에서도 **가로로 흐르지 않는다**', rows.every((r) => r.scrollX === false),
     JSON.stringify(rows.map((r) => r.w + ':' + r.scrollX)));

  /* ── [14] 🔵 **자리 선택이 꺼져 있으면 그렇게 말한다** (REQ-0518) ────────
     ★ 서버가 `--no-chooser` 로 돌면 **붙어 있어도 고를 기회가 안 온다.**
       그런데 대기 문구는 *"입구에 차량이 감지되면 … 선택할 수 있습니다"* 라고 **약속한다** —
       🔴 그 약속이 거짓이면 이용자는 **고장으로 읽는다.**
     ⚠ 값은 **`snapshot` 봉투**에 온다(socket 확정). 이 화면은 원래 `snapshot` 을
       **안 읽고 있었다**(실측: 26장이 오는데 분기가 없었다 — 관측자 0).
     🔑 그리고 **문구를 화면에 박지 않는다** — 박으면 서버가 `--no-chooser` 를 떼도
       화면이 계속 "자동 배정" 이라고 말한다(거짓말의 방향만 뒤집힌다). */
  console.log('\n[14] 자리 선택이 꺼져 있을 때');
  const SNAP = (extra) => Object.assign({ type: 'snapshot', ts: NOW,
    device: { online: true, device_id: 'P1', uptime: 60, seq: 1, last_frame_ts: NOW - 500 },
    slots: [] }, extra || {});

  /* ① 키가 **없으면** 아무것도 안 바꾼다 — 옛 서버에서 안 깨지는 것이 이 규약의 핵심이다 */
  await evaluate(client, feed(SNAP()));
  await evaluate(client, feed(STATE('idle')));
  await sleep(80);
  const ce0 = await evaluate(client, V);
  console.log('  · 키 없음(옛 서버) → ' + JSON.stringify(ce0.lead));
  ok('🔴 `chooser_enabled` 키가 **없으면 아무것도 안 바꾼다** (옛 서버에서 안 깨진다)',
     /선택할 수 있습니다/.test(ce0.lead), ce0.lead);

  /* ② `false` → 약속을 거둔다 */
  await evaluate(client, feed(SNAP({ chooser_enabled: false })));
  await sleep(80);
  const ce1 = await evaluate(client, V);
  console.log('  · false → ' + JSON.stringify(ce1.lead));
  ok('🔴 `false` 면 **"고를 수 있다" 는 약속을 거둔다**',
     !/선택할 수 있습니다/.test(ce1.lead) && /자동으로 배정/.test(ce1.lead), ce1.lead);
  ok('🔑 그리고 **기다리지 않아도 된다고 말한다** (이용자가 할 일이 바뀐다)',
     /기다리지 않으셔도/.test(ce1.lead), ce1.lead);

  /* ③ 대조군 — `true` 로 되돌리면 문구도 돌아온다.
     🔑 이게 없으면 위 검사가 **"한 번 false 면 영영 그대로"** 여도 초록이다.
     ★ 그리고 이것이 *"화면에 박지 않았다"* 의 증명이다 — 박았으면 안 돌아온다. */
  await evaluate(client, feed(SNAP({ chooser_enabled: true })));
  await sleep(80);
  const ce2 = await evaluate(client, V);
  console.log('  · true 로 되돌림 → ' + JSON.stringify(ce2.lead));
  ok('🔑 대조군: `true` 로 되돌리면 **문구도 돌아온다** (화면에 박지 않았다는 증명)',
     /선택할 수 있습니다/.test(ce2.lead) && !/자동으로 배정/.test(ce2.lead), ce2.lead);

  /* ── [15] 🔴 **빨간불을 한 번 내 본다** ───────────────────────────────
     ★ 초록불은 "괜찮다" 와 "안 봤다" 를 같은 모양으로 만든다.
       위 검사들이 실제로 무언가를 보고 있는지, 틀린 입력을 넣어 확인한다. */
  console.log('\n[15] 검사가 실패도 하는지 — 틀린 입력을 넣어 본다');
  await evaluate(client, feed(STATE('choosing', { blind: ['A1', 'A2', 'A3', 'A4', 'A5'] })));
  v = await evaluate(client, V);
  const allBlind = v.slots.every((s) => s.state === 'UNKNOWN');
  console.log('  · 전부 `unknown` 으로 넣었을 때: ' + JSON.stringify(v.slots.map((s) => s.state)) + ' · 빈자리 ' + v.free);
  ok('🔑 전부 모르는 자리로 넣으면 **[4]가 빨강이 될 조건이 실제로 만들어진다**',
     allBlind && !v.slots.some((s) => s.pick),
     '이 줄이 초록이면 [4]의 `value_state` 검사가 값을 보고 있다는 뜻이다');
  /* 🔴🔴 **`0/0` 과 `0/N` 을 화면이 가르나** — 값 검사로는 안 잡혔다. 실기 화면을 눈으로 보고 찾았다.
     보드가 안 붙어 다섯 칸 전부 점검 중인데 빈자리를 크게 `0` 이라 말했다 → 이용자는 **만차**로 읽는다. */
  console.log('  · 전부 모를 때 빈자리 칸: "' + v.free + '" / "' + v.freeUnit + '"');
  ok('🔴🔴 상태를 아는 자리가 0 이면 빈자리를 **`0` 이라고 말하지 않는다** (그건 못 잰 것이다)',
     v.free !== '0' && /확인할 수 없/.test(v.freeUnit), v.free + ' / ' + v.freeUnit);
  /* 대조군 — 정말 만차일 때는 `0` 이라고 말해야 한다. 안 그러면 위 검사가 동어반복이다 */
  await evaluate(client, feed(STATE('choosing', { busy: ['A1', 'A2', 'A3', 'A4', 'A5'] })));
  v = await evaluate(client, V);
  console.log('  · 정말 만차일 때: "' + v.free + '" / "' + v.freeUnit + '"');
  ok('🔑 대조군: **정말 만차면 `0` 이라고 말한다** (둘을 갈랐다는 증거)',
     v.free === '0' && !/확인할 수 없/.test(v.freeUnit), v.free + ' / ' + v.freeUnit);
  /* 부분만 아는 경우 — **분모를 밝힌다.** 안 밝히면 나머지도 세어 본 것처럼 읽힌다 */
  await evaluate(client, feed(STATE('choosing', { blind: ['A4', 'A5'] })));
  v = await evaluate(client, V);
  console.log('  · 셋만 알 때: "' + v.free + '" / "' + v.freeUnit + '"');
  ok('🔑 일부만 아는 경우 **분모를 밝힌다** (3칸 기준)',
     v.free === '3' && /3칸/.test(v.freeUnit), v.free + ' / ' + v.freeUnit);

  skip('실기에서 8081 포트로 붙는다', '주입 모드다 — 실기는 `node web/tools/user8081.mjs --live 8081` 이 잰다');
  skip('chooser{on:true} 가 접속 때 나간다', '주입 모드에서는 WS 를 가로채기 전에 onopen 이 지나간다 — `--live` 가 프로토타입 후킹으로 실제로 센다');
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
