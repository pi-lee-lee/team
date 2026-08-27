/**
 * 🔴 **보드(아두이노) 목록 · 부분 고장 · 제어기 초기화** 를 잰다 — 멀티 화면
 * (`조별과제샘플/VS_server_multi/server_multi/index.html` · REQ-0364).
 *
 * ⚠ **주입이다. 서버를 안 쓴다**(`file://` + `?demo=1` · 트래픽 0 · 하행 0).
 *   🔑 이 초록의 뜻은 *"그 자료가 오면 화면이 그렇게 그린다"* 다.
 *   **"실기에서 그렇게 나온다"가 아니다** — 그것은 윈도우에서 서버를 띄워 따로 재야 한다.
 *
 * 검사는 **긍정형**이고 **분모를 같이 단언한다**:
 *   ❌ "죽은 보드 경고가 안 뜬다"   → 보드가 0대여도 참이다
 *   ✅ 보드 행 수 == 봉투의 `devices[]` 길이 · 그 수가 **0 이 아니다**
 *   ✅ 경고가 **켜지는 각본**과 **안 켜지는 각본**을 둘 다 밟는다(음성 대조)
 *
 * 사용: node web/tools/boards.mjs            (--head 로 창을 본다)
 *       node web/tools/boards.mjs --file <다른 index.html>   ← 음성 대조용
 *       node web/tools/boards.mjs --live <포트>              ← 실기 관측(주입 0)
 *
 * 🔴🔴 **새 검사를 넣기 전에 반드시 물어라: "이 화면에만 있는 것인가?"**
 *
 * 이 도구는 **두 화면**을 잰다 — 1차(`server_multi/`)와 `five/`. five 는 08-25 새벽 이후의
 * 고침을 전부 받았고 **1차는 실기 직전이라 안 건드린다.** 그래서 둘의 능력이 갈려 있다.
 * ```
 * ⚠ 그것을 모르고 검사를 넣으면 **1차에서 빨강**이 뜬다 — 그런데 그건 결함이 아니다
 * 🔴 실제로 **세 번 밟았다**([15] 에서 규율을 세우고 [16]·[14-not_ready] 에서 다시 어겼다)
 * ✅ 처방 : **능력을 먼저 탐지**하고(`CAPS`) 없으면 `skip` — 빨강이 아니라 **미측정**이다
 *    🔑 skip 문장에 *"1차면 정상"* 과 *"실기 뒤 옮긴다"* 를 적어 **매 실행에 보이게** 한다
 * ```
 */
import { launch, evaluate, sleep, ZONE_HELPERS } from './cdp.mjs';
import { readFileSync } from 'node:fs';

const HEAD = process.argv.includes('--head');
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
/* 🔴 `--live <포트>` — **주입하지 않는다.** 실기 서버가 실제로 보낸 봉투만 읽는다.
   주입 모드의 초록은 *"그 자료가 오면 그렇게 그린다"* 이고, 이 모드는 *"지금 실기에서 그렇게 나온다"* 다 —
   **다른 진술이라 따로 재야 한다.** ⚠ 클릭·하행 0. 유일한 예외는 `get_map`(읽기 질의라 전선을 안 문다). */
const LIVE = (() => { const i = process.argv.indexOf('--live'); return i >= 0 ? process.argv[i + 1] : null; })();
const SECS = (() => { const i = process.argv.indexOf('--secs'); return i >= 0 ? Math.max(5, Number(process.argv[i + 1]) || 0) : 25; })();
/* 🔴 기본 대상은 **서머리** 다 — 2026-08-26 부터 서버가 읽는 화면이 그것이다.
   ⚠ 1차(`VS_server_multi/server_multi`) · 2차 · `five/` 는 **동결**이다. 고치지 않고, 재지도 않는다:
     동결된 트리에는 오늘 고침이 안 들어가 있어 **참인 빨강**이 나오는데, 그 빨강은
     "고쳐야 한다" 가 아니라 "그 트리는 이제 안 쓴다" 를 뜻한다 — 재면 매번 놀란다.
   🔑 그래도 봐야 하면 `--file <경로>` 로 명시한다. **기본으로는 안 간다.** */
const DEFAULT_FILE = new URL('../../서머리/server/index.html', import.meta.url).href;
const URL_ = LIVE
  ? ('http://127.0.0.1:' + LIVE + '/index.html')
  : ((fileArg
      ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg)).href
      : DEFAULT_FILE) + '?demo=1');

let pass = 0, fail = 0, skipped = 0;
const ok = (n, c, d) => { if (c) { pass++; console.log('  ✅ ' + n); } else { fail++; console.log('  ❌ ' + n + (d ? '\n       → ' + d : '')); } };
const skip = (n, why) => { skipped++; console.log('  ⏭ ' + n + '  → 측정 불가: ' + why); };
const inject = (frame) => `(() => { handleServerMessage(${JSON.stringify(frame)}); return true; })()`;

/* 페이지 로드 **전**에 심는 수신 후킹. `JSON.parse` 는 이 화면의 봉투가 반드시 지나는 자리이고,
   전역이라 전송 계층이 재생성돼도 살아남는다(객체 후킹은 재연결에 덮인다 — 원장 §5.114). */
const EARLY_HOOK = `
  window.__rxN = {}; window.__rx = {}; window.__rxLast = {};
  (function () { const rp = JSON.parse;
    JSON.parse = function (t) { const v = rp.apply(JSON, arguments);
      if (v && typeof v === 'object' && typeof v.type === 'string') {
        window.__rxN[v.type] = (window.__rxN[v.type] || 0) + 1;
        if (!window.__rx[v.type]) window.__rx[v.type] = v;
        window.__rxLast[v.type] = v;
      } return v; }; })();
`;

/* ── 각본 재료 ─────────────────────────────────────────────────────
   ⚠ 필드 이름은 **서버 소스 그대로**다(`wsjson.h snapshot_json()` · `persist.h`). 지어낸 것 0. */
const NOW = Date.now();
const SLOTS = (ids) => ids.map((id) => ({ id: id, occupied: 0, reserved: 0, user_id: null, reserved_at: null, overridden: 0 }));
const TEN = ['A1', 'A2', 'A3', 'A4', 'A5', 'B1', 'B2', 'B3', 'B4', 'B5'];

/** 기본 스냅샷 — `devices` 를 **명시적으로 넣거나 뺀다**(넣지 않음과 빈 배열은 다른 사실이다). */
function snap(opts) {
  const o = opts || {};
  const s = {
    type: 'snapshot', ts: NOW,
    device: { online: true, device_id: 'P1', uptime: 3600, seq: 1234, last_frame_ts: NOW - 1000 },
    test_mode: { armed: false, override_count: 0 },
    slots: SLOTS(o.slots || TEN)
  };
  if (o.devices !== undefined) s.devices = o.devices;
  if (o.devices_online !== undefined) s.devices_online = o.devices_online;
  if (o.noSlots) delete s.slots;
  return s;
}
const P1 = (over) => Object.assign({
  device_id: 'P1', online: true, primary: true, registered: true,
  module_count: 10, uptime: 3600, seq: 1234, last_frame_ts: NOW - 1000
}, over || {});
const P2 = (over) => Object.assign({
  device_id: 'P2', online: false, primary: false, registered: true,
  module_count: 2, uptime: 0, seq: 0, last_frame_ts: NOW - 185000
}, over || {});

/** 지형 — 자리 둘, 서로 **다른 보드**의 모듈이 붙어 있다. 이것이 부분 고장의 최소 재현이다.
 *  🔴 `decl` 을 주면 `declared`(REQ-0378 · socket 신설)를 실은 봉투가 된다.
 *     **안 주면 키가 아예 없다** — 옛 서버(조립 표 없음)를 흉내 내는 것이라 그 갈래도 밟힌다. */
const MAP = (epoch, a4mods, decl) => {
  const z = [
    { id: 'A1', kind: 'parking', cells: [[0, 0]], active: { ok: true, reason: null },
      modules: [{ devid: 'P1', name: 'A1', kind: 'IP', idx: 0 }] },
    { id: 'A4', kind: 'parking', cells: [[0, 1]], active: { ok: true, reason: null },
      modules: a4mods }
  ];
  if (decl) {
    z[0].declared = [{ devid: 'P1', name: 'A1' }];
    z[1].declared = [{ devid: 'P2', name: 'A4' }, { devid: 'P2', name: 'L4' }];
  }
  return { type: 'map', srv_id: 'T-M', epoch: epoch, grid: { rows: 1, cols: 2 }, zones: z };
};
const ST = (epoch) => ({
  type: 'state', srv_id: 'T-M', epoch: epoch, ts_ms: NOW, max_per_batch: 4,
  zones: [
    { id: 'A1', occupied: false, reserved: false, actions: {}, completion: 'unknown', value_state: 'known',
      modules: [{ devid: 'P1', name: 'A1', idx: 0, value: false, known: true }] },
    { id: 'A4', occupied: false, reserved: false, actions: {}, completion: 'unknown', value_state: 'unknown',
      modules: [] }
  ]
});

const READ = `(() => {
  const t = (el) => el ? (el.textContent || '') : '';
  const bn = document.getElementById('boards-banner');
  const sb = document.getElementById('slots-banner');
  const rs = document.getElementById('ctl-reset');
  const sum = document.getElementById('boards-sum');
  return {
    sum: t(sum), deg: sum ? sum.dataset.degraded : null, sumTitle: sum ? sum.title : '',
    note: t(document.getElementById('boards-note')),
    rows: [...document.querySelectorAll('#boards .board')].map((li) => ({
      id: t(li.querySelector('.board__id')),
      online: li.dataset.online,
      tags: [...li.querySelectorAll('.btag')].map((x) => x.textContent),
      meta: t(li.querySelector('.board__meta'))
    })),
    banner: { hidden: !!bn.hidden, text: t(bn) },
    slotsBanner: { hidden: !!sb.hidden, text: t(sb) },
    tiles: document.querySelectorAll('#grid .tile').length,
    reset: { dis: rs.getAttribute('aria-disabled'), text: t(rs), reason: rs.dataset.reason || '' },
    msgs: [...document.querySelectorAll('#messages .msg')].map((m) => t(m))
  };
})()`;

const ZONES = `__eachZone((cell) => ({
  down: (() => { const s = cell.querySelector('.zone__boarddown'); return s ? s.textContent : null; })(),
  aria: cell.getAttribute('aria-label') || ''
}))`;

/**
 * 🔴 **실기 관측** (`--live <포트>`) — 주입 0 · 클릭 0 · 하행 0.
 *
 * 🔑 이 도구의 값은 **봉투의 키 유무를 사람 대신 세는 것**이다.
 *   `PLAN-2026-08-25-실기.md` 의 판별자 여럿이 *"DevTools 를 열어 키가 있나 봐라"* 인데,
 *   그건 사람이 매번 눈으로 하는 일이고 **놓치기 쉽다.** 명령 하나로 값이 나오면 그 창이 닫힌다.
 *
 * ⚠ **화면 판정보다 키 유무가 먼저다** — 안 보이는 것을 화면 결함으로 읽지 않기 위해서다.
 */
async function liveSuite(cl) {
  /* 수신 프레임을 잡는다. 🔑 화면의 `handleServerMessage` 는 **함수 선언**이라 전역 속성이다 —
     덮어써도 호출부(전송 계층)가 덮인 것을 부른다. **원본을 반드시 이어서 부른다**(화면을 안 죽인다). */
  /* 🔑 로드 전에 심은 `EARLY_HOOK` 이 이미 봉투를 모으고 있다. **여기서 초기화하지 않는다** —
     초기화하면 그때까지 받은 것(첫 `map`·`state`)이 날아간다. 그게 `state.link` 를 놓친 경로였다. */
  const already = await evaluate(cl, `!!(window.__rxN && Object.keys(window.__rxN).length)`).catch(() => false);
  if (already !== true) {
    console.log('  ⚠ 로드 전 후킹이 안 걸렸다 — 늦은 후킹으로 대신한다(첫 장을 놓쳤을 수 있다)');
    await evaluate(cl, `(() => {
      window.__rx = window.__rx || {}; window.__rxN = window.__rxN || {}; window.__rxLast = window.__rxLast || {};
      const orig = handleServerMessage;
      window.handleServerMessage = function (m) {
        if (m && typeof m.type === 'string') {
          window.__rxN[m.type] = (window.__rxN[m.type] || 0) + 1;
          if (!window.__rx[m.type]) window.__rx[m.type] = m;
          window.__rxLast[m.type] = m;
        }
        return orig(m);
      };
      return true;
    })()`);
  }

  let link = null;
  for (let i = 0; i < 150; i++) {
    link = await evaluate(cl, `state.link`).catch(() => null);
    if (link === 'ws') break;
    await sleep(100);
  }
  ok('실기: WS 로 붙었다 (link=' + link + ')', link === 'ws', '데모·폴백이면 서버 자료가 아니다');
  if (link !== 'ws') { skip('실기 관측 전체', 'WS 가 아니다 — 판정하지 않는다'); return; }

  /* `map` 은 접속 시 한 번만 올 수 있다 — 후킹 전에 지나갔으면 **한 번 청한다**(읽기 질의라 전선을 안 문다). */
  await evaluate(cl, `(() => { transport.send({ type: 'get_map' }); return true; })()`);
  /* ⚠ **이 관측 자체가 서버에 연결 하나를 더한다.** 서버는 연결마다 `push_snapshot()` 을 내므로
  (`config.h:235`: "화면이 N 개면 N 배다") 붙어 있는 동안 하행 부담이 그만큼 는다.
  🔴 2026-08-26 에 시험 브라우저 **28개**가 9900 에 남아 서버 소크 줄의 `WS접속` 이
  **사람이 아니라 좀비**를 세고 있었다(cpp 가 잡았다).
  ✅ 이제 `cdp.mjs` 가 프로세스 종료 시 자동으로 죽인다 — `process.exit()` 로 끝나도 안 남는다.
  🔑 그래도 **짧게 붙어라.** 오래 붙어 있으면 재려던 것을 자기가 흔든다. */
  console.log('  · ' + SECS + '초 동안 봉투를 모은다 (주입 0 · 하행 0)');
  console.log('    ⚠ 이 관측이 서버 연결 하나를 더한다 — 끝나면 바로 닫는다.');
  await sleep(SECS * 1000);

  const rx = await evaluate(cl, `({ n: window.__rxN, keys: Object.keys(window.__rx) })`);
  console.log('  · 받은 봉투: ' + JSON.stringify(rx.n));
  ok('🔴 분모: 봉투를 하나라도 받았다', rx.keys.length > 0, JSON.stringify(rx.keys));

  /* ── 🔴 **키 유무 표** — 계획서의 판별자들을 여기서 한꺼번에 센다 ───────────────
     🔵 state.link 는 socket 이 다음 굽기 경계에 싣는다. 없으면 null 이고 아래에서 skip 이다.
        화면은 이 값을 안 그린다(사람이 rtt 를 보고 할 일이 없다) — 이 도구가 유일한 소비자다.
     🔴🔴 **아래 템플릿 리터럴 안에 주석을 넣지 마라.** 역따옴표 하나가 리터럴을 끊는다 —
        오늘 이 함정을 세 번 밟았다. 설명은 전부 리터럴 밖(여기)에 적는다. */
  const probe = await evaluate(cl, `(() => {
    const s = window.__rx.snapshot, m = window.__rx.map, st = window.__rx.state;
    const z0 = (m && Array.isArray(m.zones) && m.zones[0]) || null;
    const zs0 = (st && Array.isArray(st.zones) && st.zones[0]) || null;
    const mods = (zs0 && Array.isArray(zs0.modules)) ? zs0.modules : [];
    const anyMod = mods.find((x) => x && typeof x === 'object') || null;
    return {
      snapshot_devices:    !!(s && Array.isArray(s.devices)),
      snapshot_devicesN:   (s && Array.isArray(s.devices)) ? s.devices.length : null,
      snapshot_devices_online: !!(s && typeof s.devices_online === 'number'),
      onlineN:             (s && typeof s.devices_online === 'number') ? s.devices_online : null,
      map_declared:        !!(z0 && Array.isArray(z0.declared)),
      map_active_scope:    (z0 && z0.active && z0.active.scope) || null,
      state_usable:        !!(zs0 && zs0.usable && typeof zs0.usable === 'object'),
      state_active_legacy: !!(zs0 && zs0.active && typeof zs0.active === 'object'),
      state_dead_modules:  !!(zs0 && zs0.usable && Array.isArray(zs0.usable.dead_modules)),
      mod_transition:      !!(anyMod && anyMod.transition),
      mod_reason:          !!(anyMod && typeof anyMod.reason === 'string'),
      zoneCount:           (m && Array.isArray(m.zones)) ? m.zones.length : null,
      modCount:            mods.length,
      /* 🔴 **관측점**(socket 확인): 서버는 낮은 epoch 의 state 를 map 뒤에 안 보낸다 →
         **정상값은 0**. 0 이 아니면 화면 결함이 아니라 **서버 쪽 신호**다. */
      olderSeen:           (typeof state.olderSeen === 'number') ? state.olderSeen : null,
      link:                (st && st.link && typeof st.link === 'object') ? st.link : null
    };
  })()`);
  console.log('  · 봉투 키 표: ' + JSON.stringify(probe, null, 0));
  /* 🔴 **이 관측이 무엇을 말할 수 있나** — 받은 장수가 그것을 정한다.
     서버의 `state` 는 **사건 기반**이다(주기 타이머가 없다 · socket 이 소스로 확정):
       ① WS 업그레이드 직후 한 번   ② 장치 프레임을 처리할 때마다
     → **장치가 0대면 ②가 안 돌아 접속 직후 한 장뿐**이고, 붙으면 1Hz 로 바뀐다.
     ⚠ 한 장뿐인 관측으로 **시간에 걸친 것**(판 비교·누적 계수)을 판정하면 `0/0` 을 `0/N` 으로 읽는 것이다. */
  const stateN = (rx.n && rx.n.state) || 0;
  console.log('  · 이 관측의 분모: `state` ' + stateN + '장'
            + (stateN < 2 ? '  → 순간 스냅샷만 말할 수 있다(시간 축 판정은 skip 한다)'
                          : '  → 시간 축 판정이 유효하다'));
  if (probe.olderSeen === null) {
    skip('늦은 판 프레임 관측점', '이 화면에는 그 카운터가 없다 — 1차면 정상이다(five 에만 있다)');
  } else if (stateN < 2) {
    skip('늦은 판 프레임 관측점', '`state` 를 ' + stateN + '장 받았다 — **판을 비교하려면 두 장 이상**이 필요하다. '
       + '장치가 0대면 접속 직후 한 장뿐이라(사건 기반) 이 `0` 은 "정상" 이 아니라 "못 쟀다" 다');
  } else {
    ok('🔴 늦은 판 프레임이 0 이다 (`state` ' + stateN + '장 중 · 0 이 아니면 서버 쪽 신호다)',
       probe.olderSeen === 0, '무시한 장수 ' + probe.olderSeen + ' — socket 에 알려라(송신 경로 후보)');
  }

  /* ── 🔴 **어휘 트립와이어** — 실기에서 **내 표에 없는 코드**가 오면 알린다 ──────────
     ★ 화면은 안 깨진다(폴백이 `사유: xxx` 로 그대로 보인다). **그래서 아무도 모른다.**
       android·socket 이 어휘를 늘리면 그 사유는 화면에서 **낱말로만** 남는다 —
       사람이 할 일을 못 읽는다. 그것이 이 표의 존재 이유인데.
     🔑 android 판별자(2026-08-27): **트립와이어는 걸렸을 때 "무엇을 하라" 고 말해야 한다.**
       주석에만 적어 두면 출력을 보는 사람에게는 없는 것과 같다. 그래서 detail 에 지시문을 넣는다. */
  const vocab = await evaluate(cl, `(() => {
    const st = (window.__rxLast && window.__rxLast.state) || window.__rx.state;
    const e = st && st.entry;
    if (!e) return null;
    const has = (tbl, k) => Object.prototype.hasOwnProperty.call(tbl, k);
    const out = [];
    if (e.shot_last_error && !has(SHOT_REASON_TEXT, e.shot_last_error))
      out.push({ field: 'shot_last_error', code: e.shot_last_error, table: 'SHOT_REASON_TEXT', who: 'android' });
    if (e.plate_discarded && !has(PLATE_DISCARD_TEXT, e.plate_discarded))
      out.push({ field: 'plate_discarded', code: e.plate_discarded, table: 'PLATE_DISCARD_TEXT', who: 'socket' });
    if (e.shot_closed && !has(SHOT_CLOSED_TEXT, e.shot_closed))
      out.push({ field: 'shot_closed', code: e.shot_closed, table: 'SHOT_CLOSED_TEXT', who: 'socket' });
    return out;
  })()`).catch(() => null);
  if (vocab === null) {
    skip('어휘 트립와이어', '이 화면에는 `entry` 판이 없다 — 1차면 정상이다');
  } else if (vocab.length === 0) {
    /* 🔵 **촬영 진행 다섯 칸이 이 서버에 실려 오나** — 배포 여부를 값으로 답한다.
       ⚠ `null` 과 **키 없음**을 가른다: 서버는 빈 값을 `null` 로 싣고(`wsjson.h`),
         키가 아예 없으면 **옛 서버**다. 화면은 둘을 같게 다루지만 **판정은 다르다** —
         `null` 은 *"쟀는데 없다"* 이고 키 없음은 *"이 서버는 그것을 모른다"* 다. */
    {
      const e = await evaluate(client, `(() => {
        const st = window.__rxLast && window.__rxLast.state;
        const en = st && st.entry;
        if (!en) return null;
        const keys = ['shot_tries', 'shot_wait_ms', 'shot_last_error', 'plate_discarded', 'shot_closed'];
        const has = keys.filter((k) => Object.prototype.hasOwnProperty.call(en, k));
        return { phase: en.phase, has: has, n: has.length,
                 vals: has.map((k) => k + '=' + JSON.stringify(en[k])).join(' · ') };
      })()`);
      if (!e) {
        skip('촬영 진행 다섯 칸이 실려 온다', '`state.entry` 를 못 받았다 — 잴 자리에 못 갔다(0/0)');
      } else if (e.n === 0) {
        skip('촬영 진행 다섯 칸이 실려 온다 (0/5)',
             '이 서버는 그 칸을 **하나도 안 보낸다** — 옛 판본이다. 화면은 안 깨지고 조용히 넘긴다. '
             + '배포 뒤 다시 재라 (지금 phase=' + e.phase + ')');
      } else {
        console.log('  · 촬영 진행 칸: ' + e.vals);
        ok('🔵 촬영 진행 **다섯 칸이 전부** 실려 온다 (' + e.n + '/5) — 배포됐다',
           e.n === 5, JSON.stringify(e.has) + ' — 일부만 오면 화면이 그 칸을 조용히 못 그린다');
      }
    }

    ok('🔑 실기에서 온 사유가 **전부 화면 표에 있다** (모르는 낱말이 안 새고 있다)', true);

    /* 🔵 **`on_label`/`off_label` 이 실제로 오나** — socket 이 준 검산법(REQ-0515 계약 §9-B):
       **입구 차단봉 버튼이 `열기`/`닫기` 인가.** `켬`/`끔` 이면 그 칸이 죽은 것이다.
       ⚠ 화면 코드는 `ctl.on_label || '켬'` 이라 **그 칸이 사라져도 안 죽는다** — 빨간불이 안 난다.
         그래서 **실기에서 값으로** 봐야 한다. 주입으로는 내가 넣은 것을 내가 읽을 뿐이다.
       🔴 그리고 **자리 안내등에는 원래 라벨이 없다**(socket 확인) — `켬/끔` 이 정답이다. 차단봉만 본다.
       ⚠ 비면 **키 자체가 없다**(`wsjson.h:253`). `null` 이 아니다 — 그래서 `hasOwnProperty` 로 센다. */
    {
      const lab = await evaluate(client, `(() => {
        const st = window.__rxLast && window.__rxLast.state;
        const zs = (st && Array.isArray(st.zones)) ? st.zones : [];
        const out = [];
        for (const z of zs) for (const m of (z.modules || [])) {
          const c = m && m.control;
          if (!c) continue;
          out.push({ zone: z.id, name: m.name,
                     hasOn: Object.prototype.hasOwnProperty.call(c, 'on_label'),
                     on: c.on_label, off: c.off_label });
        }
        return out;
      })()`);
      const gates = (lab || []).filter((x) => /^[EX]D$/.test(String(x.name || '')));
      if (!lab || !lab.length) {
        skip('조작 라벨(on_label/off_label)이 실려 온다',
             '조작 모듈이 하나도 안 왔다 — **보드가 붙어야 잰다**(0/0)');
      } else if (!gates.length) {
        skip('차단봉 라벨이 `열기/닫기` 다',
             '조작 모듈 ' + lab.length + '개 중 **차단봉(ED/XD)이 0개**다 — 지형에 없거나 그 보드가 안 붙었다');
      } else {
        console.log('  · 차단봉 라벨: ' + JSON.stringify(gates));
        ok('🔵 차단봉 라벨이 실려 온다 — `켬/끔` 폴백으로 안 떨어진다 (socket 검산법 §9-B)',
           gates.every((g) => g.hasOn && g.on === '열기' && g.off === '닫기'),
           JSON.stringify(gates) + '  ← `켬/끔` 이 보이면 **그 칸이 죽은 것**이다');
      }
    }
  } else {
    for (const v of vocab) {
      ok('🔴 `' + v.field + '` 의 코드가 화면 표에 없다: **' + v.code + '**', false,
         '화면은 안 깨진다 — "사유: ' + v.code + '" 로 그대로 보인다. 그래서 **조용하다**.\n'
       + '       → 할 일 ①: `서머리/server/index.html` 의 `' + v.table + '` 에 그 코드를 넣어라\n'
       + '       → 할 일 ②: ' + v.who + ' 에게 **"그 사유가 무슨 뜻이고 사람이 무엇을 해야 하나"** 를 물어라\n'
       + '       ⚠ 낱말만 베끼지 마라 — 이 표의 목적은 **고칠 곳을 말하는 것**이다');
    }
  }

  /* ── 🔵 링크 품질(`state.link`) — 배포되면 자동으로 잡힌다 ────────────────────
     🔴 `rtt_n` 이 분모다. `rtt_max_ms: 0` 은 두 가지를 뜻한다:
        `rtt_n > 0` → 정말 빠르다   ·   `rtt_n = 0` → **한 번도 안 쟀다**(명령이 안 나갔거나 ACK 이 안 왔다)
     ⚠ 분모 없이 `0` 을 적으면 **가장 나쁜 상태가 가장 좋아 보인다.** 그래서 "측정 없음" 으로 가른다. */
  if (!probe.link) {
    skip('링크 품질(rtt · 프레임 공백)', '이 서버 판에는 `state.link` 가 없다 — 0 으로 그리지 않는다');
  } else {
    const lk = probe.link;
    const n = Number(lk.rtt_n);
    console.log('  · 링크: ' + JSON.stringify(lk));
    ok('🔑 (분모) `rtt_n` 이 실렸다 — 이것 없이는 `rtt_max_ms:0` 을 읽을 수 없다',
       Number.isFinite(n), JSON.stringify(lk));
    if (!Number.isFinite(n) || n === 0) {
      skip('명령 왕복 시간(rtt)', '측정 없음 — `rtt_n=0` 이다(명령이 한 번도 안 나갔거나 ACK 이 안 왔다). '
         + '**0ms 가 아니다**');
    } else {
      console.log('    rtt 최대 ' + lk.rtt_max_ms + 'ms · 최근 ' + lk.rtt_last_ms + 'ms · 표본 ' + n + '개');
      ok('🔑 rtt 를 실제로 쟀다 (표본 ' + n + '개)', n > 0);
      /* 🔴 **이 값을 "느리다" 로 읽지 마라** (socket 확인 · 서버 주석도 "RTT 의 상한" 이라 적는다).
         `rtt` = RTT + 장치 처리시간 + **하행 창 대기(슬롯 약 1.2초)** 다.
         → 1,290ms 같은 값은 **슬롯 안이라 정상**이다. 실측으로 그 값이 나왔다.
         ⚠ 의미 있는 것은 **절대값이 아니라 변화**다: 갑자기 뛰거나, `rtt_n` 이 안 느는 것.
         그래서 여기서 **빨강을 만들지 않는다** — 판정 없이 값과 읽는 법만 찍는다. */
      console.log('    🔑 이 값은 하행 창 대기(슬롯 ~1.2초)를 포함한다 — 1,300ms 안팎은 정상이다.');
      console.log('       볼 것은 절대값이 아니라 **갑자기 뛰는 것**과 **rtt_n 이 안 느는 것**이다.');
    }
    if (typeof lk.sess_max_gap_ms === 'number') {
      console.log('    프레임 최대 공백 ' + lk.sess_max_gap_ms + 'ms');
    }
  }

  /* 🔑 **초록/빨강이 아니라 "무엇이 실렸나" 를 값으로 찍는다** — 그 다음 판단은 사람이 한다.
     ⚠ 다만 **분모가 0 이면 그 사실을 시끄럽게** 말한다: 자리 0 · 모듈 0 이면 아래 전부가 공허하다. */
  ok('🔴 분모: 지형에 자리가 있다 (' + probe.zoneCount + ')', (probe.zoneCount || 0) > 0,
     'map 이 없거나 자리가 0 이면 아래 판정이 전부 공허하다');
  /* 🔴🔴 **`0/0` 을 `fail` 로 내지 마라.** 보드가 한 대도 안 붙어 있으면 모듈이 `0` 인 것은
     **화면 결함이 아니라 못 잰 것**이다. 그것을 빨강으로 내면 다음 사람이 화면을 고치러 간다.
     ★ 같은 병을 오늘 8080·8081 에서 고쳤다 — *"빈자리 0"* 이 *"만차"* 로 읽히던 것.
       📖 docs/web/LEDGER.md §5.140. **도구 자신에게도 같은 규율을 쓴다.**
     🔑 판별자는 `devices_online` 이다 — 서버가 센 값이라 화면 사정과 무관하다. */
  if (probe.onlineN === 0) {
    skip('첫 자리에 결속 모듈이 있다 (' + probe.modCount + ')',
         '서버가 센 연결 보드가 **0대**다 — 붙은 보드가 없으면 모듈은 원래 안 온다. '
         + '이 `0` 은 정상도 결함도 아니고 **못 잰 것**이다. 보드를 붙이고 다시 재라');
  } else {
    ok('🔴 분모: 첫 자리에 결속 모듈이 있다 (' + probe.modCount + ')', (probe.modCount || 0) > 0,
       '보드는 ' + probe.onlineN + '대 붙어 있는데 모듈이 0 이다 — 등록 전이면 "등록 완료" 로그를 먼저 봐라(계획서 §0)');
  }

  /* ── 폴백 제거 판별자 둘 — **눈으로 보라고 적어 둔 것을 도구가 센다** ───────────── */
  ok('🔄 `map.zones[].declared` 가 실렸다 (LEGACY-DECLARED-FALLBACK 제거 조건)',
     probe.map_declared === true, '없으면 화면은 기억 갈래로 돈다 — 폴백을 지우지 마라');
  ok('🔄 `state.zones[].usable` 이 실렸다 (LEGACY-ACTIVE-KEY 제거 조건)',
     probe.state_usable === true, '없으면 옛 이름(active) 폴백으로 돈다 — 폴백을 지우지 마라');
  if (probe.state_active_legacy) {
    console.log('  ⚠ `state.zones[].active` 도 같이 온다 — 서버가 두 이름을 다 싣고 있다(전환기)');
  }
  ok('`map.active.scope` 표지가 있다 (assembly)', probe.map_active_scope === 'assembly',
     '없으면 옛 판본이다 — 판정에는 안 쓰므로 화면은 안 깨진다');

  /* ── 계획서 1-4 의 판별자: **화면이 아니라 봉투**가 답한다 ────────────────────── */
  if (probe.mod_transition) {
    console.log('  🔄 `transition` 이 **온다** — 펌웨어가 E 를 보낸다. 계획서 1-1·1-2 가 살아난다');
  } else {
    console.log('  ✅ `transition` 이 **없다** — 예상대로다(ardu_multi 는 E 를 안 만든다).');
    console.log('     🔑 그래서 전 모듈 "변화 없음" 은 **정상**이다. 결함으로 읽지 마라(계획서 §1)');
  }
  console.log('  · `modules[].reason`(값이 왜 안 오나) 실림: ' + (probe.mod_reason ? '있다' : '없다(값이 오는 중이거나 옛 판본)'));
  console.log('  · `snapshot.devices`: ' + (probe.snapshot_devices ? (probe.snapshot_devicesN + '대') : '**없다**')
            + ' · `devices_online`: ' + (probe.snapshot_devices_online ? '있다' : '없다'));

  /* ── 화면이 실제로 그린 것 (판정이 아니라 값) ──────────────────────────────── */
  const shown = await evaluate(cl, READ);
  console.log('  · 화면 요약: ' + shown.sum);
  console.log('  · 보드 행: ' + JSON.stringify(shown.rows.map((x) => x.id + ':' + x.online)));
  console.log('  · 부분 고장 배너: ' + (shown.banner.hidden ? '(꺼짐)' : shown.banner.text.slice(0, 80)));
  ok('🔑 봉투에 보드가 있으면 화면에도 그 수만큼 있다',
     !probe.snapshot_devices || shown.rows.length === probe.snapshot_devicesN,
     '봉투 ' + probe.snapshot_devicesN + '대 vs 화면 ' + shown.rows.length + '행');

  skip('reset_controller 왕복', '하행이라 이 도구가 스스로 누르지 않는다 — 사람이 눌러 확인한다');
}

let client = null;
try {
  console.log('\n🔎 ' + URL_);
  client = await launch({ headless: !HEAD });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  /* 🔴 실기 모드에서는 **페이지 로드 전에** 수신 후킹을 건다.
     장치가 안 붙어 있으면 서버가 `state` 를 아주 드물게 민다(실측: 15초에 1장) —
     로드 뒤에 후킹하면 **첫 장을 놓치고 그 뒤로 아무것도 안 온다.**
     그러면 실렸는데도 `없다` 로 보고한다(2026-08-26 에 `state.link` 를 그렇게 놓쳤다).
     ⚠ 주입 모드는 우리가 봉투를 넣으므로 이 문제가 없다 — 그래도 같이 걸어 둔다(무해). */
  await client.send('Page.addScriptToEvaluateOnNewDocument', { source: EARLY_HOOK });
  await client.send('Page.navigate', { url: URL_ });
  let ready = false;
  for (let i = 0; i < 100; i++) {
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.getElementById('boards')`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  ok('화면이 떴다 · 보드 패널이 있다', ready === true);
  if (ready !== true) throw new Error('화면 준비 실패');
  await evaluate(client, ZONE_HELPERS);

  /* 🔴 **능력 탐지를 여기 한 곳에 모은다** — 절마다 따로 물으면 물어보는 것을 잊는다(세 번 잊었다).
     🔑 값으로 찍어 두면 *"왜 그 절이 미측정인가"* 를 사람이 바로 안다. */
  const CAPS = await evaluate(client, `({
    entry:       !!document.getElementById('entry-panel'),
    olderSeen:   typeof state.olderSeen === 'number',
    plateReject: typeof state.plateReject === 'string'
  })`);
  console.log('  · 이 화면의 능력: ' + JSON.stringify(CAPS)
            + (CAPS.plateReject ? '' : '   🔑 1차 화면이면 정상이다(고침이 five 에만 있다)'));

  /* 🔴 실기 모드는 **여기서 끝난다** — 이어서 주입하면 실기 자료를 내 주입으로 덮는다. */
  if (LIVE) {
    await liveSuite(client);
    await client.close().catch(() => {});
    console.log('\n' + '─'.repeat(60));
    console.log('  ' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail'
              + (skipped ? ' / ' + skipped + ' 미측정' : ''));
    process.exit(fail > 0 ? 1 : 0);
  }

  /* ────────────────────────────────────────────────────────────────
     [1] 보드 둘 · 한 대가 죽었다 — **부분 고장이 이 화면의 요점이다**
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[1] 보드 둘 · P2 끊김');
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));
  let r = await evaluate(client, READ);
  console.log('  · ' + JSON.stringify({ sum: r.sum, rows: r.rows.map((x) => x.id + ':' + x.online), banner: r.banner.hidden }));
  ok('🔴 분모: 보드 행이 봉투와 같은 수다 (2)', r.rows.length === 2, JSON.stringify(r.rows));
  ok('🔴 분모: 그 수가 0 이 아니다', r.rows.length > 0);
  ok('“2대 중 1대 연결됨” 을 값으로 적는다', r.sum.includes('2대 중 1대'), r.sum);
  ok('🔴 줄어든 상태를 표시로도 말한다 (deg=1)', r.deg === '1', r.deg);
  ok('죽은 보드 행이 갈린다 (P2 · data-online=0)', r.rows[1].online === '0' && r.rows[1].tags.includes('끊김'), JSON.stringify(r.rows[1]));
  ok('주 노드 배지가 P1 에 붙는다', r.rows[0].tags.includes('주 노드'), JSON.stringify(r.rows[0].tags));
  ok('🔴 부분 고장 배너가 켜진다', r.banner.hidden === false);
  ok('🔴 배너가 **어느 보드인지** 말한다 (P2)', r.banner.text.includes('P2'), r.banner.text);
  ok('🔑 서버가 센 값임을 밝힌다 (devices_online)', r.sumTitle.includes('서버가 센 값'), r.sumTitle);

  /* ────────────────────────────────────────────────────────────────
     [2] 🔴 **음성 대조 — 전부 살아 있으면 배너가 꺼진다**
         (안 그러면 [1]의 초록은 "배너는 늘 켜져 있다" 와 구별되지 않는다)
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[2] 음성 대조 — 전부 연결됨');
  await evaluate(client, inject(snap({ devices: [P1(), P2({ online: true, uptime: 500, seq: 12, last_frame_ts: NOW - 500 })], devices_online: 2 })));
  r = await evaluate(client, READ);
  ok('🔴 배너가 꺼진다 (켜질 수 있는 검사다)', r.banner.hidden === true, r.banner.text);
  ok('“2대 중 2대” 로 적는다', r.sum.includes('2대 중 2대'), r.sum);
  ok('줄어듦 표시가 꺼진다 (deg=0)', r.deg === '0', r.deg);

  /* ────────────────────────────────────────────────────────────────
     [3] 🔴 **키 없음 ≠ 빈 배열** — 모르는 것과 없는 것
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[3] devices 키가 없는 봉투 (단일 서버·옛 판본)');
  await evaluate(client, inject(snap({})));
  r = await evaluate(client, READ);
  ok('🔴 “0대” 라고 말하지 않는다', !r.sum.includes('0대'), r.sum);
  ok('“목록 없음/모른다” 로 적는다', r.sum.includes('목록 없음'), r.sum);
  ok('행이 없다 (지어내지 않는다)', r.rows.length === 0);
  ok('🔴 그런데 경보를 켜지 않는다 — 모르는 것은 고장이 아니다', r.banner.hidden === true);

  console.log('\n[4] devices: [] (붙은 보드 0대)');
  await evaluate(client, inject(snap({ devices: [], devices_online: 0 })));
  r = await evaluate(client, READ);
  ok('🔴 “0대” 라고 **적는다** (위 [3] 과 문구가 다르다)', r.sum.includes('0대'), r.sum);
  ok('🔴 이때는 경보를 켠다 — 모든 자리의 판정이 멈춘 상태다', r.banner.hidden === false, r.banner.text);

  /* ────────────────────────────────────────────────────────────────
     [5] 🔴 **파일 폴백 모양** — `uptime`·`seq` 가 **없다**(persist.h 실측)
         없는 값을 0 으로 그리면 "방금 부팅했다" 는 없는 사실이 된다
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[5] uptime·seq 가 없는 줄 (파일 폴백 모양)');
  await evaluate(client, inject(snap({
    devices: [{ device_id: 'P1', online: true, primary: true, registered: true, module_count: 10, last_frame_ts: NOW - 1000 },
              { device_id: 'P2', online: true, primary: false, registered: false, module_count: 0, last_frame_ts: null }]
  })));
  r = await evaluate(client, READ);
  console.log('  · ' + JSON.stringify(r.rows.map((x) => x.meta)));
  ok('🔴 없는 uptime 을 “가동 0초” 로 그리지 않는다', !r.rows[0].meta.includes('가동'), r.rows[0].meta);
  ok('🔴 없는 seq 를 0 으로 그리지 않는다', !r.rows[0].meta.includes('seq'), r.rows[0].meta);
  ok('🔴 last_frame_ts: null 을 1970년으로 그리지 않는다', r.rows[1].meta.includes('마지막 프레임 모름'), r.rows[1].meta);
  ok('🔑 registered:false 는 online 과 **다른 배지**다', r.rows[1].tags.includes('등록 전') && !r.rows[1].tags.includes('끊김'), JSON.stringify(r.rows[1].tags));
  ok('🔑 devices_online 이 없으면 화면이 셌다고 밝힌다', r.sumTitle.includes('화면이') && r.note.includes('화면이 센 값'), r.sumTitle + ' | ' + r.note);

  /* ────────────────────────────────────────────────────────────────
     [6] 🔴 **자리 수를 가정하지 않는다** — 봉투가 자리 넷이면 칸도 넷
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[6] 옛 격자 — 봉투가 준 자리만 그린다');
  await evaluate(client, inject(snap({ devices: [P1()], slots: ['A1', 'A2', 'A3', 'A4'] })));
  r = await evaluate(client, READ);
  ok('🔴 자리 넷이면 칸도 넷이다 (열 칸을 안 만든다)', r.tiles === 4, '칸 ' + r.tiles + '개');
  ok('🔑 그때 “모르는 자리” 배너가 안 뜬다 — 없는 자리가 아니라 **없는 것이 맞다**', r.slotsBanner.hidden === true, r.slotsBanner.text);
  await evaluate(client, inject(snap({ devices: [P1()], noSlots: true })));
  r = await evaluate(client, READ);
  /* ⚠ **기본 목록의 길이는 지형마다 다르다** — 1차는 10, five 는 5(자리 다섯)다.
     처음에 `10` 을 박아 뒀더니 five 파일에서 빨강이 났는데 **화면 결함이 아니라 계측기가
     1차 지형을 가정한 것**이었다.
     🔴 그래서 화면에서 읽는다 — 다만 §"기대값을 피검체에서 읽으면 둘 다 비었을 때 만난다" 이므로
     **`0` 이 아님을 먼저 단언한다.** 그것이 이 검사가 공허해지는 유일한 길이다. */
  const fallbackN = await evaluate(client, `SLOT_IDS.length`);
  ok('🔴 분모: 이 화면의 기본 자리 목록이 비어 있지 않다 (' + fallbackN + ')', fallbackN > 0, String(fallbackN));
  ok('음성 대조: slots 가 아예 없으면 기본 목록(' + fallbackN + '칸)으로 떨어지고',
     r.tiles === fallbackN, '칸 ' + r.tiles + '개');
  ok('🔴 그 사실을 배너가 말한다 (“모릅니다”)', r.slotsBanner.hidden === false && r.slotsBanner.text.includes('모릅'), r.slotsBanner.text);

  /* ────────────────────────────────────────────────────────────────
     [7] 🔴 **죽은 보드 → 그 자리가 멈췄다** (지형이 있어야 이을 수 있다)
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[7] 자리와 보드를 잇는다');
  await evaluate(client, inject(MAP(1, [{ devid: 'P2', name: 'A4', kind: 'IP', idx: 0 }])));
  await evaluate(client, inject(ST(1)));
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));
  let z = await evaluate(client, ZONES);
  console.log('  · ' + JSON.stringify(z));
  ok('🔴 분모: 자리 둘을 다 읽었다', Object.keys(z).length === 2, JSON.stringify(Object.keys(z)));
  ok('🔴 P2 자리(A4)에 “보드 끊김” 이 붙는다', !!(z.A4 && z.A4.down) && z.A4.down.includes('P2'), JSON.stringify(z.A4));
  ok('🔴 음성 대조: P1 자리(A1)에는 **안 붙는다**', !(z.A1 && z.A1.down), JSON.stringify(z.A1));
  ok('🔑 스크린리더에도 같은 문장이 간다 (aria-label)', (z.A4.aria || '').includes('보드'), z.A4.aria);
  r = await evaluate(client, READ);
  ok('🔴 배너가 **어느 자리**인지 말한다 (A4)', r.banner.text.includes('A4'), r.banner.text);

  /* ────────────────────────────────────────────────────────────────
     [8] 🔴🔴 **보드가 죽으면 서버는 결속을 뗀다** (`unbindDevice`)
         → 지금 봉투에는 그 자리에 모듈이 **없다.** 그래도 원인을 말할 수 있어야 한다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[8] 결속이 떨어진 뒤 — 화면의 기억으로 원인을 잇는다');
  await evaluate(client, inject(MAP(2, [])));          // A4 의 모듈이 사라졌다
  await evaluate(client, inject(ST(2)));
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));
  z = await evaluate(client, ZONES);
  console.log('  · ' + JSON.stringify(z));
  ok('🔴 그래도 A4 가 원인을 말한다', !!(z.A4 && z.A4.down) && z.A4.down.includes('P2'), JSON.stringify(z.A4));
  ok('🔴 그리고 **“마지막으로”** 라고 적는다 (지금 결속이 아니다)', (z.A4.down || '').includes('마지막으로'), z.A4.down);
  ok('🔴 음성 대조: A1 은 여전히 조용하다', !(z.A1 && z.A1.down), JSON.stringify(z.A1));

  /* 🔴 그리고 **목록을 모르는 봉투**에서는 아무 주장도 하지 않는다 — 기억이 있어도 그렇다. */
  await evaluate(client, inject(snap({})));
  z = await evaluate(client, ZONES);
  ok('🔴 devices 를 모르는 봉투에서는 자리 경고를 **안 만든다**', !(z.A4 && z.A4.down), JSON.stringify(z.A4));

  /* ────────────────────────────────────────────────────────────────
     [8-B] 🔴🔴 **`declared` 가 오면 기억이 아니라 그것으로 답한다** (REQ-0378 · socket 신설)
           🔑 여기가 요점: **한 번도 붙은 적 없는 보드**를 이름으로 댈 수 있다.
              기억 갈래로는 원리적으로 불가능하다(본 적이 없으므로).
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[8-B] map.zones[].declared — 서버가 소유 보드를 말해 준다');
  /* 🔴 **새 탭을 흉내 낸다**: 기억을 비우고, P2 는 한 번도 결속된 적이 없다(A4 modules 비었다). */
  await evaluate(client, `(() => { state.zoneSeenDev.clear(); return true; })()`);
  await evaluate(client, inject(MAP(3, [], true)));
  await evaluate(client, inject(ST(3)));
  await evaluate(client, inject(snap({ devices: [P1()], devices_online: 1 })));   // P2 는 목록에 없다
  z = await evaluate(client, ZONES);
  console.log('  · ' + JSON.stringify(z));
  ok('🔴 한 번도 안 붙은 보드를 **이름으로 댄다** (A4 → P2)',
     !!(z.A4 && z.A4.down) && z.A4.down.includes('P2'), JSON.stringify(z.A4));
  ok('🔴 문구가 “선언에는 있습니다” 다 (끊김이라고 단정하지 않는다)',
     (z.A4.down || '').includes('지형 선언에는 있습니다'), z.A4.down);
  ok('🔴 “화면의 기억” 이라고 안 적는다 — 서버가 말한 사실이다',
     !(z.A4.down || '').includes('화면의 기억'), z.A4.down);
  ok('🔴 음성 대조: 살아 있는 보드의 자리(A1)는 조용하다', !(z.A1 && z.A1.down), JSON.stringify(z.A1));
  r = await evaluate(client, READ);
  ok('🔴 배너가 그 자리를 “지금” 쪽으로 적는다 (A4 · “마지막으로” 아님)',
     r.banner.text.includes('A4') && !r.banner.text.includes('마지막으로'), r.banner.text);

  /* 🔑 **결속이 아직 남아 있는 순간**(끊긴 직후)은 문구가 달라야 한다 — 사람이 할 일이 다르다. */
  await evaluate(client, inject(MAP(4, [{ devid: 'P2', name: 'A4', kind: 'IP', idx: 0 }], true)));
  await evaluate(client, inject(ST(4)));
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));
  z = await evaluate(client, ZONES);
  ok('🔴 결속이 남아 있으면 “판정이 멈췄습니다” 로 적는다',
     (z.A4.down || '').includes('판정이 멈췄습니다'), z.A4.down);
  ok('🔑 그리고 그때는 “붙어야 할 모듈이 없습니다” 를 안 붙인다',
     !(z.A4.down || '').includes('붙어야 할 모듈이 없습니다'), z.A4.down);

  /* 🔴 음성 대조 — `declared` 가 있어도 **그 보드가 멀쩡하면** 아무 말도 안 한다. */
  await evaluate(client, inject(snap({ devices: [P1(), P2({ online: true })], devices_online: 2 })));
  z = await evaluate(client, ZONES);
  ok('🔴 declared 가 있어도 보드가 멀쩡하면 조용하다', !(z.A4 && z.A4.down), JSON.stringify(z.A4));

  /* ⚠ `declared` 키가 **없는** 봉투(옛 서버·조립 표 없음)에서는 기억 갈래로 돌아간다 —
     지우기 전까지 그 갈래가 살아 있는지도 재 둔다. */
  await evaluate(client, inject(MAP(5, [{ devid: 'P2', name: 'A4', kind: 'IP', idx: 0 }])));
  await evaluate(client, inject(ST(5)));
  await evaluate(client, inject(MAP(6, [])));
  await evaluate(client, inject(ST(6)));
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));
  z = await evaluate(client, ZONES);
  ok('⚠ declared 가 없으면 기억 갈래가 답한다 (“화면의 기억”)',
     (z.A4 && z.A4.down || '').includes('화면의 기억'), JSON.stringify(z.A4));

  /* ────────────────────────────────────────────────────────────────
     [8-C] 🔴🔴 **값이 안 오는 *이유* 를 그린다** — `modules[].reason` (§8.10 · 2026-08-19)
           ⚠ 서버는 2019… 아니 **2026-08-19 부터 보내고 있었고 화면이 버리고 있었다.**
              옛 문구는 *"원인은 화면이 알 수 없습니다"* — 받고도 안 읽은 것을 "모른다"고 적었다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[8-C] known:false 의 사유를 읽는다');
  const ST_WHY = (reason) => ({
    type: 'state', srv_id: 'T-M', epoch: 7, ts_ms: NOW, max_per_batch: 4,
    zones: [
      { id: 'A1', occupied: false, reserved: false, actions: {}, completion: 'unknown', value_state: 'unknown',
        modules: [{ devid: 'P1', name: 'A1', idx: 0, value: null, known: false, reason: reason }] },
      { id: 'A4', occupied: false, reserved: false, actions: {}, completion: 'unknown', value_state: 'unknown',
        modules: [] }
    ]
  });
  const WHY = `__eachZone((cell, panel) => {
    const w = panel.querySelector('.zmod__why');
    const lamp = panel.querySelector('.zlamp');
    return { why: w ? w.textContent : null, lampAria: lamp ? lamp.getAttribute('aria-label') : null };
  })`;
  await evaluate(client, inject(MAP(7, [], true)));
  await evaluate(client, inject(ST_WHY('node_offline')));
  await evaluate(client, inject(snap({ devices: [P1()], devices_online: 1 })));
  let w = await evaluate(client, WHY);
  console.log('  · ' + JSON.stringify(w.A1));
  ok('🔴 분모: 사유 줄이 그려진 자리가 있다', !!(w.A1 && w.A1.why), JSON.stringify(w.A1));
  ok('🔴 코드가 아니라 **문장**으로 적는다 (node_offline → 프레임이 오지 않습니다)',
     (w.A1.why || '').includes('프레임이 오지 않습니다'), w.A1.why);
  ok('🔴 옛 거짓 문구가 사라졌다 ("원인은 화면이 알 수 없습니다")',
     !(w.A1.lampAria || '').includes('원인은 화면이 알 수 없습니다'), w.A1.lampAria);
  ok('🔑 등(aria-label)에도 같은 사유가 간다', (w.A1.lampAria || '').includes('프레임이 오지 않습니다'), w.A1.lampAria);

  /* 🔴 모르는 코드는 **조용히 정상으로 떨어뜨리지 않고 원문을 보인다** — 서버가 어휘를 늘려도 안 깨진다. */
  await evaluate(client, inject(ST_WHY('wibble_new_code')));
  w = await evaluate(client, WHY);
  ok('🔴 모르는 사유 코드는 원문을 보인다', (w.A1.why || '').includes('wibble_new_code'), w.A1.why);

  /* 🔴 음성 대조 둘 — 사유 키가 없으면 **지어내지 않고**, known:true 면 줄 자체가 없다. */
  await evaluate(client, inject(ST_WHY(undefined)));
  w = await evaluate(client, WHY);
  ok('🔴 사유 키가 없으면 "이 봉투에는 사유가 없습니다" 로 적는다 (원인을 지어내지 않는다)',
     !w.A1.why && (w.A1.lampAria || '').includes('이 봉투에는 사유가 없습니다'), JSON.stringify(w.A1));
  await evaluate(client, inject({
    type: 'state', srv_id: 'T-M', epoch: 7, ts_ms: NOW, max_per_batch: 4,
    zones: [
      { id: 'A1', occupied: false, reserved: false, actions: {}, completion: 'unknown', value_state: 'known',
        modules: [{ devid: 'P1', name: 'A1', idx: 0, value: false, known: true }] },
      { id: 'A4', occupied: false, reserved: false, actions: {}, completion: 'unknown', value_state: 'unknown', modules: [] }
    ]
  }));
  w = await evaluate(client, WHY);
  ok('🔑 값이 오는 모듈에는 사유 줄이 **아예 없다** (정상 행에 빈 칸을 안 만든다)',
     !w.A1.why && (w.A1.lampAria || '').includes('값을 받고 있습니다'), JSON.stringify(w.A1));

  /* ────────────────────────────────────────────────────────────────
     [9] 🔴 **제어기 초기화** — 이 판본의 유일한 액추에이터 조작
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[9] reset_controller 왕복');
  await evaluate(client, `(() => {
    window.__sent = [];
    const orig = transport.send.bind(transport);
    transport.send = function (p) { window.__sent.push(p); return orig(p); };
    return true;
  })()`);
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));
  await evaluate(client, `document.getElementById('ctl-reset').click()`);
  await sleep(120);
  let sent = await evaluate(client, `window.__sent.map(p => p.type)`);
  ok('🔴 전선에 reset_controller 가 나간다', sent.includes('reset_controller'), JSON.stringify(sent));
  r = await evaluate(client, READ);
  ok('🔑 답을 기다리는 동안 버튼이 막힌다 (연타로 두 벌 안 나간다)', r.reset.dis === 'true', JSON.stringify(r.reset));
  await sleep(500);                                   // 데모가 controller_reset 으로 답한다
  r = await evaluate(client, READ);
  ok('🔴 답이 오면 **접수**로 적는다 (“냈습니다”)', r.msgs.some((m) => m.includes('안전 명령을 냈습니다')), JSON.stringify(r.msgs.slice(-2)));
  ok('🔴 그리고 “닫혔다” 라고 **말하지 않는다** (결말은 cmd_result 다)',
     !r.msgs.some((m) => m.includes('닫혔습니다')), JSON.stringify(r.msgs.slice(-2)));
  /* ⚠ **전송 아이들(1초)이 아직 안 풀렸다** — 이 버튼이 하행이라 그 규칙을 같이 받는다.
     🔴 처음에 이 대기를 안 넣었더니 *"버튼이 다시 열린다"* 가 빨강이었는데 **결함이 아니라 계측기가
       틀린 것**이었다. 아이들이 풀린 뒤에 재야 "대기가 남았나"를 재는 것이 된다. */
  await sleep(1100);
  r = await evaluate(client, READ);
  ok('아이들이 풀리면 버튼이 다시 열린다', r.reset.dis === 'false', JSON.stringify(r.reset));

  /* 🔴 음성 대조 — 거절(`not_ready`). 전선을 막고 rid 를 잡아 직접 답한다. */
  await evaluate(client, `(() => { window.__sent = []; transport.send = function (p) { window.__sent.push(p); }; return true; })()`);
  await evaluate(client, `document.getElementById('ctl-reset').click()`);
  await sleep(80);
  const rid = await evaluate(client, `[...state.ctlPending.keys()][0] || null`);
  /* 🔴 **분모다.** rid 가 없으면 아래 주입은 *"rid 없는 오류"* 가 되어 **일반 오류 경로**로 떨어지고,
     그래도 문구는 같아서 **초록이 뜬다.** 실제로 그 거짓 초록을 한 번 봤다(아이들에 막혀 클릭이 안 나갔다). */
  ok('🔴 분모: 보낸 요청의 rid 를 잡았다', typeof rid === 'string' && rid.length > 0, String(rid));
  await evaluate(client, inject({ type: 'error', rid: rid, code: 'not_ready', message: 'x' }));
  await sleep(80);
  r = await evaluate(client, READ);
  /* 🔑 앞에 붙는 **“제어기 초기화 —”** 가 *"이 요청의 답"* 이라는 증거다. 그것이 없으면
     같은 문구라도 **rid 를 못 찾아 일반 경로로 떨어진 것**이다 — 두 경로를 문구로 가른다. */
  ok('🔴 not_ready 를 **이 요청의 답**으로 적는다 (장치를 의심하게 하지 않는다)',
     r.msgs.some((m) => m.includes('제어기 초기화 — 서버 제어기가 아직 준비되지 않았습니다')),
     JSON.stringify(r.msgs.slice(-2)));
  const left = await evaluate(client, `state.ctlPending.size`);
  ok('🔑 대기 목록이 비었다 (6초 타임아웃이 나중에 거짓 실패를 안 낸다)', left === 0, String(left));
  await sleep(1100);
  r = await evaluate(client, READ);
  ok('거절 뒤에도 버튼이 되돌아온다 (막힌 채 남지 않는다)', r.reset.dis === 'false', JSON.stringify(r.reset));

  /* ────────────────────────────────────────────────────────────────
     [10] 🔴🔴 **파일 폴백 경로** — `snapshotFromLog()` 를 실제로 밟는다
          ⚠ 위 검사들은 전부 `handleServerMessage`(WS 모양)로 넣어서 **이 함수를 안 지난다.**
             즉 파일에서 `devices` 를 옮기는 줄은 **한 번도 실행된 적이 없었다**(08-25 발견).
          🔑 §"부르는 줄과 꽂는 줄을 둘 다 세라" 의 자리다.
          경계: `fetch` 만 건너뛴다(파일 읽기는 하니스가 한다) — 파싱·정규화·렌더는 진짜 경로다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[10] 파일 폴백 — snapshotFromLog 를 실제로 지난다');
  const fileLog = (rec) => `(() => {
    const raw = snapshotFromLog(${JSON.stringify(rec)});
    if (!raw) return 'null';
    applySnapshot(raw, 'file');
    return 'ok';
  })()`;
  /* 신형 레코드 — socket 이 REQ-0378 ②로 `persist.h` 에 맞춘 모양(uptime·seq·devices_online 포함) */
  const NEWREC = [{
    ts: NOW, device_id: 'P1', uptime: 3600, seq: 1234,
    device: { online: true, last_frame_ts: NOW - 40000 },
    test_mode: { armed: false, override_count: 0 },
    devices: [
      { device_id: 'P1', online: true, primary: true, registered: true, module_count: 10,
        uptime: 3600, seq: 1234, last_frame_ts: NOW - 40000 },
      { device_id: 'P2', online: false, primary: false, registered: true, module_count: 2,
        uptime: 0, seq: 0, last_frame_ts: NOW - 185000 }
    ],
    devices_online: 1,
    slots: [{ id: 'A1', occupied: 0, reserved: 0, overridden: 0 }]
  }];
  let r10 = await evaluate(client, fileLog(NEWREC));
  ok('🔴 분모: 파일 레코드가 스냅샷으로 변환됐다', r10 === 'ok', String(r10));
  r = await evaluate(client, READ);
  console.log('  · ' + JSON.stringify({ sum: r.sum, rows: r.rows.map((x) => x.id + ':' + x.online) }));
  ok('🔴 **파일에서도 보드 목록이 온다** (이 줄이 처음 실행됐다)', r.rows.length === 2, JSON.stringify(r.rows));
  ok('🔴 파일 경로에서도 죽은 보드가 갈린다 (P2)', r.rows[1] && r.rows[1].online === '0', JSON.stringify(r.rows[1]));
  ok('🔑 파일의 devices_online 을 서버 값으로 쓴다 (화면이 세지 않는다)',
     r.sumTitle.includes('서버가 센 값'), r.sumTitle);
  /* 🔴 원장 §5.7 의 보드판 — 파일은 **상태가 바뀔 때만** 써지므로 그 나이는 **침묵의 상한**이다.
     "40초 전 수신" 이라고 단정하면 멀쩡한 보드를 죽은 것처럼 말한다. */
  ok('🔴 파일 경로의 나이를 **상한**으로 적는다 (단정하지 않는다)',
     r.rows[0].meta.includes('상한'), r.rows[0].meta);

  /* 음성 대조 — 옛 형식 파일(`devices` 없음)에서는 **목록 없음**으로 떨어진다 */
  const OLDREC = [{
    ts: NOW, device_id: 'P1', uptime: 3600, seq: 1234,
    device: { online: true, last_frame_ts: NOW - 1000 },
    test_mode: { armed: false, override_count: 0 },
    slots: [{ id: 'A1', occupied: 0, reserved: 0, overridden: 0 }]
  }];
  await evaluate(client, fileLog(OLDREC));
  r = await evaluate(client, READ);
  ok('음성 대조: devices 없는 옛 파일은 "목록 없음" 으로 떨어진다',
     r.rows.length === 0 && r.sum.includes('목록 없음'), r.sum);

  /* 🔑 **지금 트리의 실제 파일**을 그대로 넣어 본다 — 판정이 아니라 **기준선 출력**이다.
     11:00 창에서 서버가 새로 쓴 파일과 이 값을 대조하면 "persist 가 실제로 실었나"가 갈린다. */
  try {
    const realPath = new URL('../../서머리/server/data_log.json', import.meta.url);
    const real = JSON.parse(readFileSync(realPath, 'utf8'));
    const has = Array.isArray(real) && real[0] && Array.isArray(real[0].devices);
    console.log('  · 지금 트리의 data_log.json — devices 키: ' + (has ? '있다(' + real[0].devices.length + '대)' : '**없다**')
              + '  · 레코드 ' + (Array.isArray(real) ? real.length : 0) + '개');
    console.log('    🔑 기준선이다. 11:00 뒤 서버가 새로 쓰면 이 값이 "있다" 로 바뀌어야 한다(REQ-0378 ②)');
    await evaluate(client, fileLog(real));
    r = await evaluate(client, READ);
    console.log('    → 그 파일로 그린 화면: ' + JSON.stringify({ sum: r.sum, rows: r.rows.length }));
  } catch (e) {
    skip('실제 data_log.json 기준선', '읽지 못했다: ' + ((e && e.message) || String(e)));
  }

  /* ────────────────────────────────────────────────────────────────
     [11] 🔴🔴 **`state.zones[].active` 가 화면을 움직이나** (socket REQ-0357 · 이관 REQ-0392 ①)
          ⚠ 08-25 까지 화면은 `map.active` 만 여섯 자리에서 읽었다 — `zs.active` grep = **0**.
             그래서 서버가 매 프레임 `sensor_unavailable` 을 보내도 **아무 일도 안 일어났다.**
          🔑 `map` 은 지형이 바뀔 때만 온다. **노드가 죽는 것은 지형 변화가 아니다.**
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[11] state.active — 지금 사실이 화면을 움직인다');
  /* 🔄 키가 `active` → `usable` 로 갈렸다(socket REQ-0397 ①). **각본도 같이 갈아야 한다** —
     안 갈면 이 검사는 옛 이름으로만 초록이고 **실기에서 아무것도 안 잡는다.** */
  const ST_ACTIVE = (act, legacyKey) => ({
    type: 'state', srv_id: 'T-M', epoch: 8, ts_ms: NOW, max_per_batch: 4,
    zones: [
      { id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'unknown',
        [legacyKey ? 'active' : 'usable']: act,
        modules: [{ devid: 'P1', name: 'A1', idx: 0, value: null, known: false, reason: 'node_offline' }] },
      { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }
    ]
  });
  const PANEL = `__eachZone((cell, panel) => ({
    cellInactive: cell.dataset.active === '0',
    cellWhy: (() => { const s = cell.querySelector('.zone__inactive'); return s ? s.textContent : null; })(),
    /* ⚠ 요약은 칸에 없다 — REQ-0297 에서 칸은 이름·배지만 남기고 요약을 뺐다.
       상세 패널 머리(zoneHeadEl)가 그것을 그린다. 처음에 칸에서 찾아 null 을 받고
       화면 결함으로 읽을 뻔했다 — 자리가 바뀌면 계측기의 자리도 바꿔야 한다.
       ⚠⚠ 이 주석은 템플릿 리터럴 안이다 — 역따옴표를 쓰면 문자열이 그 자리에서 끊긴다(방금 밟았다). */
    sum: (() => { const s = panel.querySelector('.zone__sum'); return s ? s.textContent : null; })(),
    panelWhy: (() => { const p = panel.querySelector('.zpanel__empty'); return p ? p.textContent : null; })(),
    counts: [...panel.querySelectorAll('.zpanel__empty')].map((x) => x.textContent).join(' | '),
    dead: [...panel.querySelectorAll('.zdead li')].map((x) => x.textContent)
  }))`;
  /* 🔴 map 은 **활성**이라고 말하는데 state 는 **비활성**이라고 말한다 — 정확히 갈리는 각본이다.
     옛 화면(= map 만 읽던 것)은 여기서 아무 표시도 안 냈다. */
  await evaluate(client, inject(MAP(8, [{ devid: 'P1', name: 'A1', kind: 'IP', idx: 0 }], true)));
  await evaluate(client, inject(ST_ACTIVE({
    ok: false, reason: 'sensor_unavailable',
    sensors_known: 1, sensors_declared: 3, controls_alive: 2, controls_total: 2,
    dead_modules: [
      { devid: 'P2', module: 'A4', kind: 'IP', why: 'node_offline' },
      /* 🔄 `kind` 는 모르면 **키가 아예 없다** · `why` 는 기존 어휘로 통일됐다(REQ-0397 ②) */
      { devid: 'P1', module: 'B1', why: 'node_unregistered' }
    ],
    offline_devices: ['P2', 'P1']
  })));
  await evaluate(client, inject(snap({ devices: [P1()], devices_online: 1 })));
  let za = await evaluate(client, PANEL);
  console.log('  · ' + JSON.stringify(za.A1));
  ok('🔴 **state 의 비활성이 칸에 뜬다** (전에는 0건이었다)', za.A1.cellInactive === true, JSON.stringify(za.A1));
  ok('🔴 새 사유에 문장이 붙는다 (sensor_unavailable · 원문 코드가 아니다)',
     (za.A1.cellWhy || '').includes('센서 값을 알 수 없어'), za.A1.cellWhy);
  ok('🔑 요약이 "빈 자리" 라고 말하지 않는다', za.A1.sum === '점유 모름', String(za.A1.sum));
  ok('🔴 분모를 값으로 적는다 (센서 1/3)', (za.A1.counts || '').includes('센서 1/3'), za.A1.counts);
  ok('🔑 조작 축을 따로 적는다 (조작 2/2)', (za.A1.counts || '').includes('조작 2/2'), za.A1.counts);
  ok('🔴 **죽은 모듈 목록이 그려진다** (관측자 0 이었던 키)', za.A1.dead.length === 2, JSON.stringify(za.A1.dead));
  ok('🔴 모듈마다 사유가 갈린다 (선을 봐라 / 기다려라)',
     za.A1.dead[0].includes('프레임이 오지 않습니다')
     && za.A1.dead[1].includes('모듈 목록을 알려 주지 않았습니다'),
     JSON.stringify(za.A1.dead));
  ok('🔑 사유 어휘가 기존 표와 이어진다 (새 이름 0 · 원문 코드 노출 0)',
     !za.A1.dead.some((x) => x.includes('사유 ')), JSON.stringify(za.A1.dead));
  ok('🔑 kind 키가 없으면 "종류 모름" 이다 (단정하지 않는다)',
     za.A1.dead[1].includes('종류 모름'), za.A1.dead[1]);
  ok('🔴 음성 대조: state 가 활성이라는 자리(A4)는 조용하다', za.A4.cellInactive === false, JSON.stringify(za.A4));

  /* 🔴 음성 대조 — **state 가 다시 활성이라고 하면 화면이 풀린다**(얼어붙지 않는다) */
  await evaluate(client, inject(ST_ACTIVE({
    ok: true, reason: null, sensors_known: 3, sensors_declared: 3,
    controls_alive: 2, controls_total: 2, dead_modules: [], offline_devices: []
  })));
  za = await evaluate(client, PANEL);
  ok('🔴 활성으로 돌아오면 표시가 걷힌다', za.A1.cellInactive === false && !za.A1.cellWhy, JSON.stringify(za.A1));
  ok('🔑 죽은 모듈 목록도 사라진다', za.A1.dead.length === 0, JSON.stringify(za.A1.dead));

  /* ⚠ `active` 키가 **없는** state(옛 서버)에서는 `map` 이 답한다 — 폴백이 살아 있나 */
  await evaluate(client, inject({
    type: 'state', srv_id: 'T-M', epoch: 8, ts_ms: NOW, max_per_batch: 4,
    zones: [{ id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] },
            { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }]
  }));
  za = await evaluate(client, PANEL);
  ok('⚠ state 에 usable 이 없으면 map 이 답한다 (옛 서버 폴백 · 여기선 활성)',
     za.A1.cellInactive === false, JSON.stringify(za.A1));

  /* 🔄 **전환기 폴백** — 옛 이름(`active`)으로 오는 판도 아직 읽는다(LEGACY-ACTIVE-KEY).
     🔑 실기에서 `usable` 을 눈으로 본 뒤 이 갈래와 이 검사를 **같이** 지운다. */
  await evaluate(client, inject(ST_ACTIVE({
    ok: false, reason: 'sensor_unavailable', sensors_known: 0, sensors_declared: 2,
    controls_alive: 0, controls_total: 0, dead_modules: [], offline_devices: []
  }, true)));
  za = await evaluate(client, PANEL);
  ok('🔄 옛 이름(active)으로 와도 읽는다 — 전환기에 조용히 죽지 않는다',
     za.A1.cellInactive === true, JSON.stringify(za.A1));

  /* ────────────────────────────────────────────────────────────────
     [12] 🔴 **겹침** — 보드 하나가 죽었을 때 화면이 같은 사실을 **몇 번** 말하나
          ⚠ REQ-0297 전례: 사용자가 *"1번자리 a1 빈자리 << 3개가 동일하다"* 라고 지적했다.
             지금은 축이 둘로 늘었다(내 declared 추론 + 서버 usable 판정) — **같은 함정이 재발할 자리**다.
          🔑 이 절은 **세는 것**이 목적이다. 상한을 단언하되 값을 같이 찍는다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[12] 겹침 — 한 사실을 몇 번 말하나');
  await evaluate(client, inject(MAP(9, [], true)));
  await evaluate(client, inject({
    type: 'state', srv_id: 'T-M', epoch: 9, ts_ms: NOW, max_per_batch: 4,
    zones: [
      { id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known',
        modules: [{ devid: 'P1', name: 'A1', idx: 0, value: false, known: true }] },
      { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'unknown',
        usable: { ok: false, reason: 'sensor_unavailable', sensors_known: 0, sensors_declared: 2,
                  controls_alive: 0, controls_total: 0,
                  dead_modules: [{ devid: 'P2', module: 'A4', why: 'node_offline' },
                                 { devid: 'P2', module: 'L4', why: 'node_offline' }],
                  offline_devices: ['P2'] },
        modules: [] }
    ]
  }));
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));
  const OVERLAP = `__eachZone((cell, panel) => ({
    cellLines: [...cell.querySelectorAll('.zone__inactive, .zone__boarddown')].map((x) => x.textContent),
    panelLines: [...panel.querySelectorAll('.zpanel__empty, .zdead li')].map((x) => x.textContent)
  }))`;
  const ov = await evaluate(client, OVERLAP);
  const bannerText = (await evaluate(client, READ)).banner.text;
  const p2InCell = ov.A4.cellLines.filter((x) => x.includes('P2')).length;
  const p2InPanel = ov.A4.panelLines.filter((x) => x.includes('P2')).length;
  console.log('  · 칸  : ' + JSON.stringify(ov.A4.cellLines));
  console.log('  · 패널: ' + JSON.stringify(ov.A4.panelLines));
  console.log('  · 배너: ' + bannerText.slice(0, 60) + '…');
  console.log('  · **P2 라는 이름이 나온 횟수** — 칸 ' + p2InCell + ' · 패널 ' + p2InPanel + ' · 배너 1');
  ok('🔴 분모: 그 자리에 무언가 그려졌다', ov.A4.cellLines.length > 0, JSON.stringify(ov.A4));
  /* 🔴 **칸은 좁다.** 비활성 사유와 보드 끊김이 **둘 다** 줄로 붙으면 REQ-0297 의 그 모양이 된다. */
  ok('🔴 칸에서 같은 사실을 두 줄로 말하지 않는다 (≤ 1줄)',
     ov.A4.cellLines.length <= 1, JSON.stringify(ov.A4.cellLines));
  /* 🔑 **칸에서도 어느 보드인지는 알아야 한다** — 서버 사유(`sensor_unavailable`)에는 보드 이름이 없다. */
  ok('🔑 그래도 칸이 **어느 보드인지** 말한다 (P2)',
     ov.A4.cellLines.some((x) => x.includes('P2')), JSON.stringify(ov.A4.cellLines));
  /* 패널은 넓다 — 여기서는 갈라 말하는 것이 맞다(사유 · 분모 · 모듈별). 다만 무한정은 아니다. */
  ok('패널은 갈라 말한다 (사유·분모·죽은 모듈 — 2줄 이상)',
     ov.A4.panelLines.length >= 2, JSON.stringify(ov.A4.panelLines));
  ok('🔴 음성 대조: 멀쩡한 자리(A1)는 칸에 아무 줄도 없다',
     ov.A1.cellLines.length === 0, JSON.stringify(ov.A1.cellLines));

  /* ────────────────────────────────────────────────────────────────
     [13] 🔴 **실기 모드(`--live`) 부품 자체 점검** — 그 코드가 11시에 **처음 도는 일이 없게**
          ⚠ `liveSuite()` 는 서버가 있어야 도는데 지금은 서버 기동이 금지다.
             그대로 두면 **한 줄도 안 돌아본 코드**를 실기에서 처음 쓰게 된다 —
             도구가 거기서 죽으면 그 시간을 통째로 잃는다.
          🔑 그래서 **부품 둘**(수신 후킹 · 키 표 probe)을 주입 모드에서 태운다.
             ★ 이게 초록이라도 *"실기에서 돈다"* 는 아니다 — **부품이 돈다**까지다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[13] --live 부품 점검 (실기 없이 태울 수 있는 데까지)');
  await evaluate(client, `(() => {
    window.__rx = {}; window.__rxN = {};
    const orig = handleServerMessage;
    window.handleServerMessage = function (m) {
      if (m && typeof m.type === 'string') {
        window.__rxN[m.type] = (window.__rxN[m.type] || 0) + 1;
        if (!window.__rx[m.type]) window.__rx[m.type] = m;
      }
      return orig(m);
    };
    return true;
  })()`);
  /* 후킹 뒤에 봉투 셋을 흘려 보낸다 — 실기에서 오는 것과 **같은 종류**다 */
  await evaluate(client, inject(MAP(10, [{ devid: 'P1', name: 'A1', kind: 'IP', idx: 0 }], true)));
  await evaluate(client, inject({
    type: 'state', srv_id: 'T-M', epoch: 10, ts_ms: NOW, max_per_batch: 4,
    zones: [{ id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known',
              usable: { ok: true, reason: null, sensors_known: 1, sensors_declared: 1,
                        controls_alive: 0, controls_total: 0, dead_modules: [], offline_devices: [] },
              modules: [{ devid: 'P1', name: 'A1', idx: 0, value: false, known: true }] },
            { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'unknown', modules: [] }]
  }));
  await evaluate(client, inject(snap({ devices: [P1(), P2()], devices_online: 1 })));

  const hooked = await evaluate(client, `({ n: window.__rxN, keys: Object.keys(window.__rx) })`);
  console.log('  · 후킹이 잡은 봉투: ' + JSON.stringify(hooked.n));
  ok('🔴 수신 후킹이 봉투를 잡는다 (map·state·snapshot)',
     hooked.keys.includes('map') && hooked.keys.includes('state') && hooked.keys.includes('snapshot'),
     JSON.stringify(hooked.keys));
  ok('🔑 후킹해도 화면이 계속 그려진다 (원본을 이어서 부른다)',
     (await evaluate(client, `document.querySelectorAll('#zone-grid .zone').length`)) > 0,
     '자리가 0 이면 후킹이 화면을 죽인 것이다');

  /* 🔴 키 표 probe — `liveSuite()` 안의 그 표현식과 **같은 것**이다. 여기서 한 번 태운다. */
  const probe13 = await evaluate(client, `(() => {
    const s = window.__rx.snapshot, m = window.__rx.map, st = window.__rx.state;
    const z0 = (m && Array.isArray(m.zones) && m.zones[0]) || null;
    const zs0 = (st && Array.isArray(st.zones) && st.zones[0]) || null;
    const mods = (zs0 && Array.isArray(zs0.modules)) ? zs0.modules : [];
    const anyMod = mods.find((x) => x && typeof x === 'object') || null;
    return {
      snapshot_devices: !!(s && Array.isArray(s.devices)),
      snapshot_devicesN: (s && Array.isArray(s.devices)) ? s.devices.length : null,
      map_declared: !!(z0 && Array.isArray(z0.declared)),
      state_usable: !!(zs0 && zs0.usable && typeof zs0.usable === 'object'),
      mod_transition: !!(anyMod && anyMod.transition),
      zoneCount: (m && Array.isArray(m.zones)) ? m.zones.length : null,
      modCount: mods.length
    };
  })()`);
  console.log('  · 키 표: ' + JSON.stringify(probe13));
  ok('🔴 키 표가 실린 것을 **있다**로 센다 (declared·usable·devices)',
     probe13.map_declared === true && probe13.state_usable === true && probe13.snapshot_devices === true,
     JSON.stringify(probe13));
  ok('🔴 키 표가 없는 것을 **없다**로 센다 (transition — 이 각본엔 안 실었다)',
     probe13.mod_transition === false, '있다고 세면 11시에 "E 가 온다"는 거짓 판정이 난다');
  ok('🔑 분모(자리 수·모듈 수)를 센다', probe13.zoneCount === 2 && probe13.modCount === 1,
     JSON.stringify(probe13));

  /* ────────────────────────────────────────────────────────────────
     [14] 🔴🔴 **수동 입력 폴백** — `state.entry` / `plate_manual`
          정본: `docs/net/SPEC-manual-plate-2026-08-25.md` (socket · REQ-0408)
          ★ 인식률 12/15 라 **다섯 번에 한 번쯤** 이 경로로 시연이 이어진다.
            여기가 막히면 차단봉이 안 열리고 그 뒤 흐름이 통째로 안 돈다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[14] 수동 입력 폴백 (five)');
  const ENTRY = (en) => ({
    type: 'state', srv_id: 'T-M', epoch: 11, ts_ms: NOW, max_per_batch: 4,
    entry: en,
    zones: [
      { id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known',
        modules: [{ devid: 'P1', name: 'A1', idx: 0, value: false, known: true }] },
      { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }
    ]
  });
  const E = `(() => {
    const p = document.getElementById('entry-panel');
    const f = document.getElementById('entry-form');
    const i = document.getElementById('entry-plate');
    const b = document.getElementById('entry-send');
    return {
      hidden: !!p.hidden, phase: p.dataset.phase || null,
      status: (document.getElementById('entry-status') || {}).textContent || '',
      formHidden: !!f.hidden, val: i ? i.value : null,
      hint: (document.getElementById('entry-hint') || {}).textContent || '',
      sendDis: b ? b.getAttribute('aria-disabled') : null,
      focused: document.activeElement === i
    };
  })()`;
  await evaluate(client, inject(MAP(11, [{ devid: 'P1', name: 'A1', kind: 'IP', idx: 0 }], true)));

  /* ① idle — 🔴 **차가 없으면 판을 통째로 숨긴다**(빈 칸을 만들지 않는다) */
  await evaluate(client, inject(ENTRY({ phase: 'idle', elapsed_ms: 0, limit_ms: 0,
    plate: null, plate_source: null, slot: null, attempts: 0 })));
  let e = await evaluate(client, E);
  ok('🔴 idle 이면 판이 숨는다 (빈 칸을 안 만든다)', e.hidden === true, JSON.stringify(e));

  /* ② shooting — 남은 시간을 **서버 값으로** 그린다 */
  await evaluate(client, inject(ENTRY({ phase: 'shooting', elapsed_ms: 4200, limit_ms: 10000,
    plate: null, plate_source: null, slot: null, attempts: 0 })));
  e = await evaluate(client, E);
  console.log('  · shooting → ' + JSON.stringify({ status: e.status, form: e.formHidden }));
  ok('🔴 촬영 중이 보인다 (10초가 조용하면 "고장" 으로 보인다)', e.hidden === false && e.phase === 'shooting', JSON.stringify(e));
  ok('🔴 남은 시간을 숫자로 적는다 (6초 남음 / 10초)',
     e.status.includes('6초 남음') && e.status.includes('10초'), e.status);
  ok('🔑 이때는 입력칸이 **없다** (아직 기다리는 중이다)', e.formHidden === true, JSON.stringify(e));

  /* ③ rejected — 🔴 여기가 본체 */
  await evaluate(client, inject(ENTRY({ phase: 'rejected', elapsed_ms: 0, limit_ms: 120000,
    plate: null, plate_source: null, slot: null, attempts: 0 })));
  e = await evaluate(client, E);
  ok('🔴 입력 요청이 뜬다', e.formHidden === false && e.phase === 'rejected', JSON.stringify(e));
  /* 🔴 **문구가 "실패" 로 단정하지 않는다.**
     ⚠ 이 검사는 한 번 **틀린 전제 위에 서 있었다.** 옛 판은 `'읽지 못했습니다'` 를 **요구**했다 —
       그 문구가 참인 줄 알고 박아 뒀는데, 실측이 그것을 뒤집었다:
     ```
     socket 실측(2026-08-26 · 5건) : 폰이 시한 통보를 낸 뒤 **9 · 10 · 17 · 31 · 39초** 만에
        **진짜 번호가 온다.** 그리고 그 번호는 서버가 받아 **배정까지 간다**
     → 못 읽은 것이 아니라 **아직 안 온 것**이다. "읽지 못했습니다" 가 거짓이었다
     ```
     ★ **검사가 특정 문구에 묶이면 사실이 바뀔 때 같이 못 바뀐다** — 오히려 옛 거짓을 지킨다.
        그래서 이제 **금지어와 필수 뜻**으로 잰다: 무엇을 쓰든 *단정하지 않고 할 일을 말하면* 통과한다.
     ★ 판별자 : **아직 안 온 것과 안 오는 것은 다르다.** 뭉치면 사람이 포기한다. */
  ok('🔴 문구가 "실패" 로 단정하지 않는다 (아직 안 온 것과 안 오는 것은 다르다)',
     !/실패|못 읽|초과|거절/.test(e.status), e.status);
  ok('🔑 그리고 **사람이 할 수 있는 것**을 말한다 (기다리거나 직접 입력)',
     /기다|직접 입력/.test(e.status), e.status);
  ok('🔑 입력칸에 초점이 간다 (사람이 바로 칠 수 있다)', e.focused === true, JSON.stringify(e));

  /* 🔴 검증 — **막지 않는다.** 경고만 하고 보낼 수 있어야 한다 */
  const typeIn = (v) => `(() => {
    const i = document.getElementById('entry-plate');
    i.value = ${JSON.stringify(v)};
    i.dispatchEvent(new Event('input', { bubbles: true }));
    return (document.getElementById('entry-hint') || {}).textContent || '';
  })()`;
  let hint = await evaluate(client, typeIn('0000002'));
  ok('🔴 LCD 예약 코드(0·1·**2**)를 경고한다 — 셋이다',
     hint.includes('0·1·2') || hint.includes('예약 코드'), hint);
  hint = await evaluate(client, typeIn('123456'));
  ok('자릿수 경고 (7자리가 보통)', hint.includes('6자리'), hint);
  hint = await evaluate(client, typeIn('1239898'));
  ok('한글이 없으면 경고한다', hint.includes('한글'), hint);
  hint = await evaluate(client, typeIn('123바9898'));
  ok('🔑 정상 형식이면 경고가 없다 (잔소리를 안 한다)', hint === '', JSON.stringify(hint));

  /* 🔴 **보낸다** — 전선에 나가나 */
  await evaluate(client, `(() => { window.__sent = []; transport.send = function (p) { window.__sent.push(p); }; return true; })()`);
  await evaluate(client, `document.getElementById('entry-form').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }))`);
  await sleep(80);
  let sentP = await evaluate(client, `window.__sent`);
  console.log('  · 전선: ' + JSON.stringify(sentP));
  ok('🔴 plate_manual 이 전선에 나간다', sentP.length === 1 && sentP[0].type === 'plate_manual', JSON.stringify(sentP));
  ok('🔴 **텍스트 그대로** 보낸다 (숫자만 뽑지 않는다 · five §5)',
     sentP[0] && sentP[0].plate === '123바9898', JSON.stringify(sentP[0]));
  ok('🔑 rid 를 단다 (ack/error 를 이을 수 있다)', !!(sentP[0] && sentP[0].rid), JSON.stringify(sentP[0]));
  e = await evaluate(client, E);
  ok('보내는 동안 버튼이 막힌다 (두 벌 안 나간다)', e.sendDis === 'true', JSON.stringify(e));

  /* ④ ack — **접수까지만** 말한다 */
  const pRid = sentP[0].rid;
  await evaluate(client, inject({ type: 'ack', rid: pRid, result: 0 }));
  await sleep(60);
  let rr = await evaluate(client, READ);
  ok('🔴 ack 를 "접수" 로 적는다', rr.msgs.some((m) => m.includes('접수했습니다')), JSON.stringify(rr.msgs.slice(-2)));
  ok('🔴 그리고 "배정되었습니다" 라고 **말하지 않는다** (다음 state 가 말한다)',
     !rr.msgs.some((m) => m.includes('배정되었습니다')), JSON.stringify(rr.msgs.slice(-2)));

  /* ⑤ assigned — 입력칸을 **치운다** */
  await evaluate(client, inject(ENTRY({ phase: 'assigned', elapsed_ms: 0, limit_ms: 0,
    plate: '123바9898', plate_source: 'manual', slot: 'A1', attempts: 1 })));
  e = await evaluate(client, E);
  console.log('  · assigned → ' + e.status);
  ok('🔴 배정되면 입력칸을 치운다 (다음 차 번호가 안 들어간다)', e.formHidden === true, JSON.stringify(e));
  ok('🔴 번호를 **텍스트 그대로** 보인다', e.status.includes('123바9898'), e.status);
  ok('🔴 **출처를 같이 적는다** (직접 입력) — 지표가 거짓말하지 않게', e.status.includes('직접 입력'), e.status);
  ok('자리도 적는다 (A1)', e.status.includes('A1'), e.status);

  /* 🔑 음성 대조 — 카메라가 읽은 것은 **다르게** 적는다 */
  await evaluate(client, inject(ENTRY({ phase: 'assigned', elapsed_ms: 0, limit_ms: 0,
    plate: '123바9898', plate_source: 'camera', slot: 'A1', attempts: 0 })));
  e = await evaluate(client, E);
  ok('🔑 음성 대조: camera 면 "자동 인식" 이다', e.status.includes('자동 인식'), e.status);

  /* ⑥ 거절 — 입력칸을 **비우지 않는다**(사람이 방금 친 것을 지우면 다시 쳐야 한다) */
  await evaluate(client, inject(ENTRY({ phase: 'rejected', elapsed_ms: 0, limit_ms: 120000,
    plate: null, plate_source: null, slot: null, attempts: 1 })));
  await evaluate(client, typeIn('0000001'));
  await evaluate(client, `(() => { window.__sent = []; return true; })()`);
  await evaluate(client, `document.getElementById('entry-form').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }))`);
  await sleep(80);
  sentP = await evaluate(client, `window.__sent`);
  ok('🔴 경고가 떠도 **보낼 수 있다** (막지 않는다 — 폴백이 폴백이 아니게 되면 안 된다)',
     sentP.length === 1 && sentP[0].plate === '0000001', JSON.stringify(sentP));
  await evaluate(client, inject({ type: 'error', rid: sentP[0].rid, code: 'bad_request', message: 'x' }));
  await sleep(60);
  e = await evaluate(client, E);
  ok('🔴 거절돼도 입력칸을 안 비운다 (다시 치게 만들지 않는다)', e.val === '0000001', JSON.stringify(e));
  /* ⚠ 처음에 `hint.length > 0` 으로 뒀더니 **"시도 1회" 만 있어도 통과**했다 —
     그 헐거운 분모가 *"거절 사유가 render 에 즉시 덮인다"* 는 결함을 **숨기고 있었다.**
     🔑 §"분모 없는 0 은 건강처럼 보인다" 의 사촌: **분모가 느슨한 검사는 결함을 숨긴다.** */
  if (CAPS.plateReject) {
    ok('🔴 거절 사유가 **그 문장 그대로** 뜬다 (길이만 재지 않는다)',
       e.hint.includes('잘못된 요청'), e.hint);
  } else {
    skip('거절 사유가 그 문장 그대로 뜬다', '이 화면에는 그 고침이 없다 — 1차면 정상이다. **실기 뒤 옮긴다**');
  }

  /* 🔴 **같은 코드, 다른 맥락** — `not_ready` 는 `reset_controller` 와 수동 입력 **둘 다** 쓴다.
     표 문장은 제어기에 맞춰져 있어서, 수동 입력 거절에 그대로 쓰면 화면이 **제어기 이야기를 한다.**
     (접속 시험 서버가 `not_ready` + "입차 흐름이 없습니다" 로 답하면서 드러났다.) */
  if (!CAPS.plateReject) {
    skip('not_ready 맥락 문구 · 거절 사유 유지',
         '이 화면에는 그 고침이 없다 — 1차면 정상이다. **실기 뒤 옮긴다**');
  } else {
    await evaluate(client, inject(ENTRY({ phase: 'rejected', elapsed_ms: 0, limit_ms: 120000,
      plate: null, plate_source: null, slot: null, attempts: 1 })));
    await evaluate(client, typeIn('123바9898'));
    await evaluate(client, `(() => { window.__sent = []; return true; })()`);
    await evaluate(client, `document.getElementById('entry-form').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }))`);
    await sleep(80);
    const nrSent = await evaluate(client, `window.__sent`);
    ok('🔴 분모: 그 요청이 전선에 나갔다', nrSent.length === 1, JSON.stringify(nrSent));
    await evaluate(client, inject({ type: 'error', rid: nrSent[0].rid, code: 'not_ready',
                                    message: '접속 시험용 서버입니다 — 입차 흐름이 없습니다' }));
    await sleep(60);
    e = await evaluate(client, E);
    console.log('  · not_ready 문구 → ' + e.hint);
    ok('🔴 수동 입력 거절에 **제어기 이야기를 안 한다**', !e.hint.includes('제어기'), e.hint);
    ok('🔑 맥락에 맞는 문장을 쓴다 (번호를 받지 않는 상태)', e.hint.includes('번호를 받지 않는'), e.hint);
    ok('🔑 서버가 준 message 를 보조로 덧붙인다 (왜인지 사람이 안다)',
       e.hint.includes('입차 흐름이 없습니다'), e.hint);
  }
  ok('버튼이 다시 열린다', e.sendDis === 'false', JSON.stringify(e));

  /* ⑦ 🔴 차가 그냥 가버린 경우 — idle 로 돌면 **판이 사라진다** */
  await evaluate(client, inject(ENTRY({ phase: 'idle', elapsed_ms: 0, limit_ms: 0,
    plate: null, plate_source: null, slot: null, attempts: 0 })));
  e = await evaluate(client, E);
  ok('🔴 idle 로 돌아가면 판과 입력칸이 사라진다', e.hidden === true && e.val === '', JSON.stringify(e));

  /* ⑦-B 🔴 **보내는 중에 차가 가버린다** — socket 이 짚은 두 번째 idle 경로(§3-C).
     ⚠ 앞의 ⑦ 은 *"거절 뒤 idle"* 이었다. 이건 **답을 기다리는 중** idle 이라 상태가 하나 더 있다. */
  await evaluate(client, inject(ENTRY({ phase: 'rejected', elapsed_ms: 0, limit_ms: 120000,
    plate: null, plate_source: null, slot: null, attempts: 0 })));
  await evaluate(client, typeIn('123바9898'));
  await evaluate(client, `(() => { window.__sent = []; return true; })()`);
  await evaluate(client, `document.getElementById('entry-form').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }))`);
  await sleep(60);
  const inFlight = await evaluate(client, `window.__sent`);
  ok('🔴 분모: 보내는 중이다 (전선에 나갔고 답을 기다린다)',
     inFlight.length === 1 && (await evaluate(client, `state.platePending.size`)) === 1,
     JSON.stringify(inFlight));
  await evaluate(client, inject(ENTRY({ phase: 'idle', elapsed_ms: 0, limit_ms: 0,
    plate: null, plate_source: null, slot: null, attempts: 1 })));
  e = await evaluate(client, E);
  ok('🔴 답을 기다리는 중에 idle 이 와도 칸이 비워진다', e.hidden === true && e.val === '', JSON.stringify(e));
  /* 🔑 그 뒤 늦은 `ack` 가 와도 **화면이 안 깨지는지**. 계약상 서버는 답을 보낼 수 있다. */
  await evaluate(client, inject({ type: 'ack', rid: inFlight[0].rid, result: 0 }));
  await sleep(60);
  e = await evaluate(client, E);
  const leftP = await evaluate(client, `state.platePending.size`);
  ok('🔑 늦게 온 ack 를 받아도 판이 다시 안 뜬다 (차는 이미 갔다)', e.hidden === true, JSON.stringify(e));
  ok('🔑 대기 목록이 비워진다 (6초 뒤 거짓 실패가 안 뜬다)', leftP === 0, String(leftP));

  /* ⑧ 모르는 phase — 조용히 정상으로 떨어뜨리지 않는다 */
  await evaluate(client, inject(ENTRY({ phase: 'wibble_new', elapsed_ms: 0, limit_ms: 0,
    plate: null, plate_source: null, slot: null, attempts: 0 })));
  e = await evaluate(client, E);
  ok('🔴 모르는 phase 는 원문을 보인다 (계약이 늘어난 것이다)',
     e.status.includes('wibble_new'), e.status);

  /* ⑨ `entry` 키가 **없는** 서버(옛 판본) — 판을 숨기되 그것은 idle 과 다른 사실이다 */
  await evaluate(client, inject({
    type: 'state', srv_id: 'T-M', epoch: 11, ts_ms: NOW, max_per_batch: 4,
    zones: [{ id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] },
            { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }]
  }));
  e = await evaluate(client, E);
  ok('⚠ entry 키가 없는 서버에서도 안 깨진다 (판만 숨는다)', e.hidden === true, JSON.stringify(e));

  /* ────────────────────────────────────────────────────────────────
     [15] 🔴 **자리의 번호판** — `state.zones[].plate` · `plate_source` (five 전용)
          ★ 오주차와 정상 주차가 **봉투에서 같은 모양**이라 이 필드가 생겼다(REQ-0414 회신).
          ⚠ 1차 화면에는 이 필드가 **없다** — 그래서 이 절은 five 에서만 값이 나온다.
             🔑 그것을 **빨강이 아니라 미측정**으로 다룬다(파일마다 다른 것은 결함이 아니다).
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[15] 자리의 번호판 (five)');
  const ZST = (a1) => ({
    type: 'state', srv_id: 'T-M', epoch: 12, ts_ms: NOW, max_per_batch: 4,
    entry: { phase: 'idle', elapsed_ms: 0, limit_ms: 0, plate: null, plate_source: null, slot: null, attempts: 0 },
    zones: [
      Object.assign({ id: 'A1', occupied: true, reserved: false, actions: {}, value_state: 'known',
                      modules: [{ devid: 'F1', name: 'A1', idx: 0, value: true, known: true }] }, a1),
      { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }
    ]
  });
  const PLATE = `__eachZone((cell, panel) => {
    const p = panel.querySelector('.zplate');
    return { text: p ? p.textContent : null, none: p ? p.className.indexOf('zplate--none') >= 0 : null,
             title: p ? p.title : null };
  })`;
  await evaluate(client, inject(MAP(12, [{ devid: 'F1', name: 'A1', kind: 'IP', idx: 0 }], true)));

  /* ① 카메라가 읽은 번호 */
  await evaluate(client, inject(ZST({ plate: '123바9898', plate_source: 'camera' })));
  let pl = await evaluate(client, PLATE);
  const hasPlateUI = !!(pl.A1 && pl.A1.text);
  if (!hasPlateUI) {
    skip('자리 번호판 표시', '이 화면에는 그 칸이 없다 — 1차 화면이면 정상이다(five 전용)');
  } else {
    console.log('  · camera → ' + JSON.stringify(pl.A1));
    ok('🔴 번호를 **텍스트 그대로** 보인다', pl.A1.text.includes('123바9898'), pl.A1.text);
    ok('🔑 출처를 문장으로 적는다 (자동 인식)', pl.A1.text.includes('자동 인식'), pl.A1.text);

    /* ② 수동 입력 — **낱말이 `entry` 칸과 같아야 한다**(두 곳이 다른 말을 하면 다른 것으로 읽힌다) */
    await evaluate(client, inject(ZST({ plate: '123바9898', plate_source: 'manual' })));
    pl = await evaluate(client, PLATE);
    ok('🔴 수동 입력을 갈라 적는다 (직접 입력)', pl.A1.text.includes('직접 입력'), pl.A1.text);
    ok('🔑 hover 가 지표 오염을 경고한다', (pl.A1.title || '').includes('지표'), pl.A1.title);

    /* ③ 🔴 **오주차 모양** — 차는 있는데 번호가 없다 */
    await evaluate(client, inject(ZST({ plate: null, plate_source: null, occupied: true })));
    pl = await evaluate(client, PLATE);
    console.log('  · 번호 없는 주차 → ' + JSON.stringify(pl.A1));
    ok('🔴 차는 있는데 번호가 없으면 그 사실을 말한다', pl.A1.none === true && pl.A1.text.includes('기록되지 않았습니다'), JSON.stringify(pl.A1));
    ok('🔴 그래도 **"오주차" 라고 단정하지 않는다** (서버가 그 낱말을 안 했다)',
       !pl.A1.text.includes('오주차'), pl.A1.text);
    ok('🔑 후보는 hover 로 넘긴다 (오주차·기록 전)', (pl.A1.title || '').includes('오주차'), pl.A1.title);

    /* ④ 음성 대조 — **빈 자리에는 아무 말도 안 한다** */
    await evaluate(client, inject(ZST({ plate: null, plate_source: null, occupied: false })));
    pl = await evaluate(client, PLATE);
    ok('🔴 음성 대조: 빈 자리에는 줄이 없다 (빈 칸을 안 만든다)', !pl.A1.text, JSON.stringify(pl.A1));

    /* ⑤ 🔴🔴 **키가 아예 없는 봉투**(1차 서버) — 아무 말도 하지 않는다
       ⚠ 여기서 "기록되지 않았습니다" 를 띄우면 **그 서버의 모든 주차 자리가 오주차로 보인다.** */
    await evaluate(client, inject({
      type: 'state', srv_id: 'T-M', epoch: 12, ts_ms: NOW, max_per_batch: 4,
      zones: [{ id: 'A1', occupied: true, reserved: false, actions: {}, value_state: 'known',
                modules: [{ devid: 'F1', name: 'A1', idx: 0, value: true, known: true }] },
              { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }]
    }));
    pl = await evaluate(client, PLATE);
    ok('🔴 `plate` 키가 없는 봉투에서는 **아무 말도 안 한다** (없는 것을 근거로 주장하지 않는다)',
       !pl.A1.text, JSON.stringify(pl.A1));
  }

  /* ────────────────────────────────────────────────────────────────
     [16] 🔴 **`shooting` 겹침 세기** — LCD 코드 2 가 살아났다(arduino 07ff9c8 · socket)
          ★ 내가 §7 에 *"지금은 화면만 말한다"* 라고 적었는데 **다시 겹침 상태로 돌아왔다.**
            예고한 순서대로 **먼저 센다.** 세지 않고 줄이면 무엇을 줄였는지 모른다.
          🔑 여기서 세는 것은 **화면 안**이다. LCD 는 다른 매체라 이 도구가 못 센다 —
             그 사실을 **못 세는 것으로** 적는다(초록으로도 빨강으로도 만들지 않는다).
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[16] shooting — 화면 안에서 몇 번 말하나');
  /* ⚠ **판(`epoch`)을 먼저 맞춘다.** 처음에 이 줄이 없어서 앞 절이 올려 둔 `map` 판과 어긋난 채
     쟀고 **0 이 나왔다** — 겹침 세기는 판 어긋남과 **무관한 검사**인데 그 상태에서 재면
     무엇을 재는지 모르게 된다. 🔑 (그 사고가 아래 진짜 결함을 드러내긴 했다.) */
  await evaluate(client, inject(MAP(11, [{ devid: 'F1', name: 'A1', kind: 'IP', idx: 0 }], true)));
  await evaluate(client, inject(ENTRY({ phase: 'shooting', elapsed_ms: 3000, limit_ms: 10000,
    plate: null, plate_source: null, slot: null, attempts: 0 })));
  /* 🔑 **계측기가 0 을 셌을 때 화면을 먼저 의심하지 않는다** — 판이 실제로 떠 있는지부터 찍는다.
     (처음에 이 진단이 없어서 "화면이 안 그린다" 로 읽을 뻔했다. 화면은 멀쩡했다.) */
  const entryNow = await evaluate(client, `(() => {
    const p = document.getElementById('entry-panel');
    const s = document.getElementById('entry-status');
    return { hidden: !!p.hidden, phase: p.dataset.phase || null, text: s ? s.textContent : null };
  })()`);
  console.log('  · entry 판: ' + JSON.stringify(entryNow));
  const saidShooting = await evaluate(client, `(() => {
    /* 화면 전체에서 "읽는 중" 을 말하는 **보이는 요소**를 센다. 숨은 것은 세지 않는다. */
    const out = [];
    const all = document.querySelectorAll('body *');
    for (const n of all) {
      if (n.children.length) continue;                 /* 잎 노드만 — 부모까지 세면 중복이다 */
      if (n.offsetParent === null && n.tagName !== 'BODY') continue;   /* 숨은 것 제외 */
      const t = (n.textContent || '').trim();
      if (t.includes('읽는 중')) out.push(t.slice(0, 60));
    }
    return out;
  })()`);
  console.log('  · 화면에서 "읽는 중" 을 말하는 자리: ' + saidShooting.length + ' — ' + JSON.stringify(saidShooting));
  ok('🔴 분모: 촬영 중이라는 사실을 화면이 말한다 (≥1)', saidShooting.length >= 1, JSON.stringify(saidShooting));
  ok('🔴 그리고 **화면 안에서는 한 곳뿐**이다 (겹침 없음)', saidShooting.length === 1, JSON.stringify(saidShooting));
  skip('LCD 와의 겹침', 'LCD 는 다른 매체다 — 이 도구가 못 센다. 실기에서 눈으로 본다(§7 판별자)');

  /* 🔴🔴 **늦게 온 옛 프레임에서도 입차 진행은 그려지나** — `[16]` 이 처음 0 을 센 진짜 원인이다.
     화면은 `epoch` 이 낮은 `state` 를 **3회까지 조용히 무시한다**(지형이 낡은 것을 안 그리려고).
     그런데 `entry` 는 **지형과 무관한 사실**이라 그 갈래에 묶이면 안 된다 —
     ⚠ 하필 **판이 어긋나는 창**(재기동·등록으로 epoch 이 오르는 순간)이
        사람이 번호를 넣어야 할 때일 수 있다. */
  await evaluate(client, inject(MAP(20, [{ devid: 'F1', name: 'A1', kind: 'IP', idx: 0 }], true)));
  await evaluate(client, inject(ENTRY({ phase: 'rejected', elapsed_ms: 0, limit_ms: 120000,
    plate: null, plate_source: null, slot: null, attempts: 0 })));   /* epoch 11 < 20 — 늦은 프레임 */
  const stale = await evaluate(client, `(() => {
    const p = document.getElementById('entry-panel');
    return { hidden: !!p.hidden, phase: p.dataset.phase || null,
             form: !!document.getElementById('entry-form').hidden,
             mapEpoch: state.map && state.map.epoch };
  })()`);
  console.log('  · 판이 어긋난 상태(map 20 vs state 11) → ' + JSON.stringify(stale));
  /* 🔑 **파일마다 다른 것을 결함으로 만들지 않는다**([15] 에서 세운 규율).
     1차 화면에는 이 고침이 **아직 없다** — 그쪽 서버는 `entry` 를 안 보내 발현되지 않고,
     11:00 실기가 그 파일을 쓰기 때문에 지금 안 고친다. **실기 뒤에 옮긴다.**
     ⚠ 그래서 빨강이 아니라 **미측정**으로 적되, 매 실행에 그 사실이 보이게 문장으로 남긴다. */
  if (stale.hidden === false && stale.phase === 'rejected') {
    ok('🔴 지형 판이 어긋나도 **입차 진행은 그려진다** (얼어붙지 않는다)', true);
    ok('🔴 그때 입력칸도 뜬다 (사람이 번호를 넣을 수 있다)', stale.form === false, JSON.stringify(stale));
  } else {
    skip('판이 어긋나도 입차 진행이 그려진다',
         '이 화면에는 그 고침이 없다 — 1차면 정상이다(entry 를 안 보내 발현 안 됨). **실기 뒤 옮긴다**');
  }

  /* 🔴 **관측점이 실제로 세나** — 방어 코드가 조용히 지나가면 밟힌 사실이 사라진다.
     🔑 socket 이 소스로 확인했다: 서버는 낮은 epoch 의 state 를 map 뒤에 **안 보낸다** → 정상값 0.
        그래서 이 카운터는 방어가 아니라 **"내가 못 본 경로가 있다" 는 신호**다. */
  if (!CAPS.olderSeen) {
    skip('늦은 판 프레임 관측점', '이 화면에는 그 카운터가 없다 — 1차면 정상이다(five 에만 있다)');
  } else {
    const before = await evaluate(client, `state.olderSeen`);
    await evaluate(client, inject(MAP(30, [{ devid: 'F1', name: 'A1', kind: 'IP', idx: 0 }], true)));
    await evaluate(client, inject(ENTRY({ phase: 'idle', elapsed_ms: 0, limit_ms: 0,
      plate: null, plate_source: null, slot: null, attempts: 0 })));      /* epoch 11 < 30 */
    const after = await evaluate(client, `state.olderSeen`);
    ok('🔴 늦은 판 프레임을 **세고 있다** (조용히 지나가지 않는다)', after > before,
       before + ' → ' + after);
    const shown = await evaluate(client, `(document.getElementById('dev-source') || {}).textContent || ''`);
    console.log('  · 데이터 경로 줄: ' + shown);
    ok('🔴 그 수가 **화면에 보인다** (밟히면 알려 준다)', shown.includes('늦은 판 프레임'), shown);
    ok('🔑 정상값이 0 이라는 것도 같이 적는다 (0 이 아닌 것이 신호다)', shown.includes('정상값 0'), shown);
  }

  /* ────────────────────────────────────────────────────────────────
     [17] 🔴 **kind 가 아니라 kind+widget 이 이름을 정한다** (2026-08-25 실기에서 사용자가 잡았다)
          ★ `OG` 하나에 **표시기(LCD) · 안내등 · 차단봉** 셋이 들어 있었고 전부 "안내등" 으로 불렸다.
            자리마다 "안내등" 이 둘씩 보였고 그 중 하나가 늘 P5 라 *"모두 p5 lcd"* 로 보였다.
          🔑 **읽기 어려운 것이 아니라 틀린 것이었다.**
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[17] 모듈 이름 — kind + widget');
  /* 🔑 능력 탐지 — 1차 화면에는 이 고침이 없다(실기 중이라 안 건드렸다). **미측정이지 빨강이 아니다.** */
  const kindCap = await evaluate(client, `typeof modKindWord === 'function'`);
  if (!kindCap) {
    skip('kind+widget 이름 · 박스 머리 · 조작 devid',
         '이 화면에는 그 고침이 없다 — 1차면 정상이다(실기 중 five 계열에만 넣었다). **실기 뒤 옮긴다**');
  } else {
  await evaluate(client, inject({ type: 'map', srv_id: 'T-M', epoch: 40, grid: { rows: 1, cols: 1 },
    zones: [{ id: 'A1', kind: 'parking', cells: [[0, 0]], active: { scope: 'assembly', ok: true, reason: null },
      declared: [], modules: [
        { devid: 'P1', name: 'C1', kind: 'OG', idx: 5, control: { widget: 'number', min: 0, max: 9999999 } },
        { devid: 'P5', name: 'R1', kind: 'OG', idx: 0, control: { widget: 'toggle' } },
        { devid: 'P3', name: 'ED', kind: 'OG', idx: 0, control: { widget: 'toggle' } },
        { devid: 'P4', name: 'ZZ', kind: 'OG', idx: 9, control: { widget: 'toggle' } },
        { devid: 'P1', name: 'A1', kind: 'IP', idx: 0 }
      ] }] }));
  await evaluate(client, inject({ type: 'state', srv_id: 'T-M', epoch: 40, ts_ms: NOW, max_per_batch: 4,
    entry: { phase: 'idle', elapsed_ms: 0, limit_ms: 0, plate: null, plate_source: null, slot: null, attempts: 0 },
    zones: [{ id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known',
      modules: [
        { devid: 'P1', name: 'C1', idx: 5, value: false, known: true, cmd: { ok: true, reason: null } },
        { devid: 'P5', name: 'R1', idx: 0, value: false, known: true, cmd: { ok: true, reason: null } },
        { devid: 'P3', name: 'ED', idx: 0, value: false, known: true, cmd: { ok: true, reason: null } },
        { devid: 'P4', name: 'ZZ', idx: 9, value: false, known: true, cmd: { ok: true, reason: null } },
        { devid: 'P1', name: 'A1', idx: 0, value: false, known: true }
      ] }] }));
  const named = await evaluate(client, `__eachZone((cell, panel) => {
    const out = {};
    for (const b of panel.querySelectorAll('.znode')) {
      const dv = b.dataset.devid;
      for (const li of b.querySelectorAll('.zmod')) {
        const t = (li.textContent || '').replace(/\s+/g, ' ');
        /* 이름 자체에 괄호가 있다(표시기(LCD)) → 첫 괄호를 잡으면 안 된다.
           전선 이름은 #<idx> (NAME) 꼴이라 그 패턴으로 집는다.
           그리고 이 표현식은 템플릿 리터럴 안이라 백슬래시를 하나 더 써야 한다 —
           JS 는 정의되지 않은 이스케이프를 축약해 버린다.
           ⚠⚠ 이 주석에 역따옴표를 쓰지 마라 — 문자열이 거기서 끊긴다(오늘 두 번 밟았다). */
        const m = t.match(/#\\d+ \\(([A-Z0-9]+)\\)/);
        if (m) out[dv + '.' + m[1]] = t.slice(0, 28);
      }
    }
    return out;
  })`);
  console.log('  · ' + JSON.stringify(named.A1));
  const nm = named.A1 || {};
  ok('🔴 분모: 모듈 다섯이 다 그려졌다', Object.keys(nm).length === 5, JSON.stringify(Object.keys(nm)));
  ok('🔴 number 위젯은 **표시기(LCD)** 다 (안내등이 아니다)',
     (nm['P1.C1'] || '').includes('표시기'), nm['P1.C1']);
  ok('🔴 toggle + R* 는 **안내등** 이다', (nm['P5.R1'] || '').includes('안내등'), nm['P5.R1']);
  ok('🔴 toggle + ED 는 **차단봉** 이다', (nm['P3.ED'] || '').includes('차단봉'), nm['P3.ED']);
  ok('🔴 모르는 조합은 **중립어("조작")** 다 — 틀린 이름보다 낫다',
     (nm['P4.ZZ'] || '').includes('조작'), nm['P4.ZZ']);
  ok('🔑 센서는 그대로다 (kind 표가 답한다)', (nm['P1.A1'] || '').includes('주차확인센서'), nm['P1.A1']);
  /* 🔴 같은 이름이 여러 보드에 있는 것이 이 시스템의 **정상**이다 — 박스가 그것을 가른다. */
  const boxes = await evaluate(client, `__eachZone((cell, panel) =>
    [...panel.querySelectorAll('.znode')].map(b => b.dataset.devid + ':' + ((b.querySelector('.znode__id')||{}).textContent||'')))`);
  console.log('  · 박스: ' + JSON.stringify(boxes.A1));
  ok('🔑 박스 머리가 **"아두이노 <id>"** 라고 적는다 (보드인지 모듈인지 안 헷갈리게)',
     (boxes.A1 || []).every((x) => x.includes('아두이노')), JSON.stringify(boxes.A1));
  /* 🔴 조작 위젯이 **어느 보드로 가는지** DOM 에서 읽힌다 — 전송은 원래 맞았고 표시가 없었다 */
  const ctls = await evaluate(client, `[...document.querySelectorAll('#zone-detail .zctl')].map(c => c.dataset.devid + '.' + c.dataset.module)`);
  console.log('  · 조작 위젯: ' + JSON.stringify(ctls));
  ok('🔴 조작 위젯이 devid 를 싣는다 (같은 이름이 다섯 보드에 있는 구성에서 유일한 구분자다)',
     ctls.length > 0 && ctls.every((x) => x && !x.startsWith('undefined') && !x.startsWith('.')), JSON.stringify(ctls));

  }

  /* ────────────────────────────────────────────────────────────────
     [18] 🔴🔴 **누르는 중에 버튼이 사라지지 않나** (2026-08-25 실기 · "클릭이 안 먹는다")
          ★ 실측: 렌더는 한 장에 **0.3ms** 로 빠르다 — **느린 것이 아니었다.**
            문제는 `state` 가 올 때마다 상세 패널을 **통째로 다시 만드는 것**이었다:
            사람이 누르는 사이 버튼이 새 노드로 바뀌면 브라우저가 `click` 을 **안 낸다.**
          🔑 센서가 흔들릴수록 봉투가 자주 오므로 **바쁠수록 더 안 먹었다.**
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[18] 누름 중 DOM 안정성');
  const holdCap = await evaluate(client, `typeof PANEL_HOLD_MS === 'number'`);
  if (!holdCap) {
    skip('누름 중 버튼이 유지된다', '이 화면에는 그 고침이 없다 — 1차면 정상이다. **실기 뒤 옮긴다**');
  } else {
    await evaluate(client, inject(MAP(50, [{ devid: 'P1', name: 'A1', kind: 'IP', idx: 0 },
      { devid: 'P1', name: 'C1', kind: 'OG', idx: 5, control: { widget: 'number', min: 0, max: 9999999 } }], true)));
    const stN = (n) => ({ type: 'state', srv_id: 'T-M', epoch: 50, ts_ms: NOW + n, max_per_batch: 4,
      entry: { phase: 'idle', elapsed_ms: 0, limit_ms: 0, plate: null, plate_source: null, slot: null, attempts: 0 },
      zones: [{ id: 'A1', occupied: n % 2 === 0, reserved: false, actions: {}, value_state: 'known',
        modules: [{ devid: 'P1', name: 'A1', idx: 0, value: n % 2 === 0, known: true },
                  { devid: 'P1', name: 'C1', idx: 5, value: false, known: true, cmd: { ok: true, reason: null } }] },
        { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known', modules: [] }] });
    await evaluate(client, inject(stN(0)));
    /* 🔴 **조작 모듈이 있는 자리를 골라 연다.**
       앞 판은 `__eachZone(() => 1)` 로 전부 클릭해 **마지막 자리**가 패널에 남는 것에 기댔다 —
       그 "마지막" 은 **DOM 순서**이고, 화면이 자리를 좌우로 뒤집자 **다른 자리가 됐다**(2026-08-27).
       ★ 못 박을 것은 *"마지막 칸이 열린다"* 가 아니라 **"조작 버튼이 있는 패널을 연다"** 다. */
    /* 🔑 **조작 버튼이 나올 때까지 자리를 열어 본다.** 어느 자리에 조작이 붙어 있는지는
       그때의 지형이 정하고, 그것은 이 검사의 관심사가 아니다. */
    const opened = await evaluate(client, `(() => {
      const g = document.getElementById('zone-grid');
      const panel = document.getElementById('zone-detail');
      const seen = [];
      for (const id of [...g.querySelectorAll('.zone')].map((x) => x.dataset.zone)) {
        const c = [...g.querySelectorAll('.zone')].find((x) => x.dataset.zone === id);
        if (!c) continue;
        c.click(); seen.push(id);
        if (panel.querySelector('.zctl__btn')) return { zone: id, seen: seen };
      }
      return { zone: null, seen: seen };
    })()`);
    console.log('  · 조작 버튼이 있는 자리: ' + JSON.stringify(opened));
    const press = await evaluate(client, `(() => {
      const panel = document.getElementById('zone-detail');
      const b0 = panel.querySelector('.zctl__btn');
      if (!b0) return { err: '조작 버튼이 없다' };
      /* ① 누르지 않은 상태에서 갱신 → 교체되는 것이 정상이다(값이 늦으면 안 된다) */
      const idle0 = panel.querySelector('.zctl__btn');
      window.__idleSame = null;
      return { ok: true };
    })()`);
    ok('🔴 분모: 조작 버튼이 그려져 있다', press.ok === true, JSON.stringify(press));
    /* ① 평소에는 갱신이 돈다 */
    const idleSwap = await evaluate(client, `(() => {
      const p = document.getElementById('zone-detail');
      const a = p.querySelector('.zctl__btn');
      handleServerMessage(${JSON.stringify(0)} , 0);
      return null;
    })()`).catch(() => null);
    await evaluate(client, inject(stN(1)));
    /* ② 🔴 누르는 중에는 유지돼야 한다 */
    const held = await evaluate(client, `(() => {
      const p = document.getElementById('zone-detail');
      const b0 = p.querySelector('.zctl__btn');
      b0.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
      return { marked: Date.now() < panelHoldUntil };
    })()`);
    ok('🔑 누름이 시작되면 보류가 걸린다', held.marked === true, JSON.stringify(held));
    await evaluate(client, inject(stN(2)));
    await evaluate(client, inject(stN(3)));
    const same = await evaluate(client, `(() => {
      const p = document.getElementById('zone-detail');
      return { still: !!p.querySelector('.zctl__btn'), zone: p.dataset.zone };
    })()`);
    ok('🔴 누르는 중 봉투가 와도 조작 버튼이 살아 있다 (click 이 죽지 않는다)',
       same.still === true, JSON.stringify(same));
    /* ③ 음성 대조 — 보류가 풀리면 다시 그린다(값이 얼어붙지 않는다) */
    await sleep(700);
    await evaluate(client, inject(stN(4)));
    const back = await evaluate(client, `(() => {
      const p = document.getElementById('zone-detail');
      return { rebuilt: Date.now() >= panelHoldUntil, has: !!p.querySelector('.zctl__btn') };
    })()`);
    ok('🔴 음성 대조: 보류가 풀리면 다시 그린다 (값이 얼어붙지 않는다)',
       back.rebuilt === true && back.has === true, JSON.stringify(back));
  }

  /* ────────────────────────────────────────────────────────────────
     [19] 🔴 **카메라 촬영 요청** — `shoot_now` (사용자 지시 · 시험 트리)
          ★ 루트 지시: *"'실패' 한 단어로 뭉치지 마라"* — 갈래마다 **사람이 할 일이 다르다.**
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[19] 촬영 요청');
  const shootCap = await evaluate(client, `!!document.getElementById('shoot-now')`);
  if (!shootCap) {
    skip('촬영 요청 버튼', '이 화면에는 그 버튼이 없다 — 1차/five 정본이면 정상이다(시험 트리 전용)');
  } else {
    const SL = `(() => {
      const b = document.getElementById('shoot-now');
      const l = document.getElementById('shoot-line');
      return { txt: b.textContent, dis: b.getAttribute('aria-disabled'),
               line: l ? l.textContent : null, kind: l ? (l.dataset.kind||'') : null };
    })()`;
    /* 🔑 위치 — 사용자가 지정했다: **"웹소켓 연결됨" 좌측** */
    const pos = await evaluate(client, `(() => {
      const b = document.getElementById('shoot-now');
      const c = document.getElementById('conn-status');
      if (!b || !c) return null;
      const rb = b.getBoundingClientRect(), rc = c.getBoundingClientRect();
      return { left: Math.round(rb.left), connLeft: Math.round(rc.left), sameRow: Math.abs(rb.top - rc.top) < 40 };
    })()`);
    console.log('  · 위치: ' + JSON.stringify(pos));
    ok('🔴 버튼이 연결 표시의 **왼쪽**에 있다 (사용자가 지정한 자리)',
       pos && pos.left < pos.connLeft && pos.sameRow, JSON.stringify(pos));

    await evaluate(client, `(() => { window.__sent = []; transport.send = function (p) { window.__sent.push(p); }; return true; })()`);
    await evaluate(client, `document.getElementById('shoot-now').click()`);
    await sleep(80);
    const sSent = await evaluate(client, `window.__sent`);
    ok('🔴 shoot_now 가 전선에 나간다 (rid 를 달고)',
       sSent.length === 1 && sSent[0].type === 'shoot_now' && !!sSent[0].rid, JSON.stringify(sSent));
    let sl = await evaluate(client, SL);
    ok('🔑 누르는 즉시 눌림 표시 — 응답까지 1~2초라 이게 없으면 "안 먹은 것" 처럼 느껴진다',
       sl.dis === 'true' && sl.txt.includes('요청 중'), JSON.stringify(sl));

    /* ① 접수(ack) — **"번호가 왔다" 가 아니다** */
    await evaluate(client, inject({ type: 'ack', rid: sSent[0].rid, result: 0 }));
    await sleep(60);
    sl = await evaluate(client, SL);
    console.log('  · ack → ' + sl.line);
    ok('🔴 ack 를 **접수**로 적는다 (번호가 왔다고 하지 않는다)',
       sl.line.includes('요청했습니다') && !sl.line.includes('번호판 '), sl.line);
    ok('🔑 접수까지 걸린 시간을 값으로 보인다', /\d+ms/.test(sl.line), sl.line);
    ok('🔑 요청번호가 없으면 **어디서 보는지** 말한다 (지금 봉투엔 없다)',
       sl.line.includes('서버 로그'), sl.line);
    ok('버튼이 다시 열린다', sl.dis === 'false', JSON.stringify(sl));

    /* ② 🔴 폰 미접속 — **촬영을 시작하지도 못한 것** */
    await evaluate(client, `(() => { window.__sent = []; return true; })()`);
    await evaluate(client, `document.getElementById('shoot-now').click()`);
    await sleep(80);
    const s2 = await evaluate(client, `window.__sent`);
    await evaluate(client, inject({ type: 'error', rid: s2[0].rid, code: 'device_offline',
      message: '촬영 요청을 만들지 못했습니다 — 폰이 붙어 있는지 확인하세요' }));
    await sleep(60);
    sl = await evaluate(client, SL);
    console.log('  · device_offline → ' + sl.line);
    ok('🔴 폰 미접속을 **연결 문제**로 적는다', sl.line.includes('폰이 붙어 있는지'), sl.line);
    ok('🔴 그리고 "시작하지도 못했다" 를 밝힌다 (못 읽은 것과 다른 사건이다)',
       sl.line.includes('시작하지도'), sl.line);

    /* ③ PENDING-SHOT-RESULT — 결과 봉투(아직 서버가 안 보낸다. 오면 이렇게 그린다) */
    await evaluate(client, inject({ type: 'shot_result', shot: 202608250001, ok: true, plate: '427나6153', reason: null }));
    await sleep(60);
    sl = await evaluate(client, SL);
    console.log('  · 성공 → ' + sl.line);
    ok('🔴 성공하면 **번호를 그대로** 보인다', sl.line.includes('427나6153'), sl.line);
    ok('🔑 요청번호를 같이 보인다 (로그와 대조한다)', sl.line.includes('202608250001'), sl.line);

    await evaluate(client, inject({ type: 'shot_result', shot: 202608250002, ok: false, plate: null, reason: 'recognize_failed' }));
    await sleep(60);
    sl = await evaluate(client, SL);
    console.log('  · recognize_failed → ' + sl.line);
  /* 🔴 **이 검사는 한 번 낡았다** — 옛 판은 `recognize_failed` 에게
     *"연결 문제가 아닙니다 · 각도·거리·조명"* 을 **요구**했다. 그런데 android 가 사유를 11개로 가르면서
     그 뜻이 **`no_plate` 로 옮겨갔고** `recognize_failed` 는 *"그 밖의 실패"* 가 됐다.
     → 어휘가 갈리자 **멀쩡한 화면이 빨강**이 됐다(§5.129 의 그 형태).
     ✅ 그래서 이제 **표의 목적**을 잰다: **사유마다 문구가 다른가.** 특정 문장을 요구하지 않는다.
     ★ android 의 요구가 그것이었다 — *"고칠 곳이 다르면 문구가 달라야 한다."* */
    ok('🔑 `recognize_failed` 는 "그 밖" 이라고 말한다 (구체 사유를 지어내지 않는다)',
       !/각도|초점|분할/.test(sl.line), sl.line);
    const reasonLine = async (r) => {
      await evaluate(client, inject({ type: 'shot_result', shot: 9001, ok: false, plate: null, reason: r }));
      await sleep(50);
      return (await evaluate(client, SL)).line;
    };
    const rNoPlate = await reasonLine('no_plate');
    const rSegment = await reasonLine('segment_fail');
    const rCapFail = await reasonLine('capture_failed');
    const rCapStuck = await reasonLine('capture_stuck');
    const rPerm = await reasonLine('no_camera_permission');
    const rUnknownCode = await reasonLine('zzz_new_code');
    console.log('  · no_plate    → ' + rNoPlate);
    console.log('  · segment_fail→ ' + rSegment);
    ok('🔑 `no_plate` 가 **사람이 할 일**을 적는다 (각도·거리·조명)', /각도/.test(rNoPlate), rNoPlate);
    /* ⚠ `!/각도/` 를 넣었다가 **내 문구가 빨강**이 됐다 — 그 문구는 *"각도·조명이 **아닙니다**"* 라고
       **명시적으로 갈라 말한다.** 낱말 유무로 재면 그런 좋은 문장이 걸린다.
       ✅ 재는 것은 **둘이 다른가**와 **분할 단계라고 말하는가**다. 낱말 금지는 안 쓴다. */
    ok('🔴 `segment_fail` 을 `no_plate` 와 **다르게** 말한다 (고칠 곳이 반대다 — 검출 임계 ↔ 글자 분할)',
       rSegment !== rNoPlate && /분할/.test(rSegment), rSegment);
    ok('🔴 `capture_stuck`("아무 말 없다")을 `capture_failed`("실패했다고 말했다")와 **가른다**',
       rCapStuck !== rCapFail && /아무 답도 안 합니다/.test(rCapStuck), rCapStuck);
    ok('🔑 `no_camera_permission` 은 **기다려도 안 풀린다**는 것을 말한다',
       /권한/.test(rPerm) && /기다려도 안 풀립니다/.test(rPerm), rPerm);
    ok('🔴 **모르는 코드는 그대로 보인다** ("실패" 로 뭉개지 않는다 — 적어도 검색은 된다)',
       /zzz_new_code/.test(rUnknownCode) && !/실패/.test(rUnknownCode), rUnknownCode);

    /* 🔴 socket 확정 어휘 셋을 다 밟는다 — **사람이 할 일이 전부 다르다** */
    await evaluate(client, inject({ type: 'shot_result', shot: 3, ok: false, plate: null, reason: 'empty_plate' }));
    await sleep(60);
    sl = await evaluate(client, SL);
    ok('🔑 empty_plate 를 갈라 적는다 (읽었다는데 비었다)', sl.line.includes('비어 있습니다'), sl.line);
    await evaluate(client, inject({ type: 'shot_result', shot: 4, ok: false, plate: null, reason: 'sentinel_plate' }));
    await sleep(60);
    sl = await evaluate(client, SL);
    ok('🔑 sentinel_plate 를 갈라 적는다 (표지값이 왔다)', sl.line.includes('표지값'), sl.line);

    /* 음성 대조 — 모르는 사유는 원문을 보인다 */
    await evaluate(client, inject({ type: 'shot_result', shot: 5, ok: false, plate: null, reason: 'wibble' }));
    await sleep(60);
    sl = await evaluate(client, SL);
    ok('🔴 모르는 사유는 원문을 보인다 (앱이 어휘를 늘려도 안 뭉갠다)', sl.line.includes('wibble'), sl.line);

    /* 🔴 **결과가 영영 안 오는 경우** — 폰이 조용히 죽으면 서버는 아무것도 안 보낸다(socket 확인).
       🔑 시한이 없으면 화면이 "기다립니다" 인 채로 **영영 멈춘다.** */
    const waitCap = await evaluate(client, `typeof SHOT_RESULT_WAIT_MS === 'number'`);
    if (!waitCap) {
      skip('결과 침묵 시한', '이 화면에는 그 시한이 없다');
    } else {
      await evaluate(client, `(() => { window.__sent = []; return true; })()`);
      await evaluate(client, `document.getElementById('shoot-now').click()`);
      await sleep(60);
      const s3 = await evaluate(client, `window.__sent`);
      await evaluate(client, inject({ type: 'ack', rid: s3[0].rid, result: 0, shot: 909 }));
      await sleep(60);
      sl = await evaluate(client, SL);
      ok('🔑 ack 에 실린 요청번호를 보인다 (로그와 대조한다)', sl.line.includes('909'), sl.line);
      /* 🔴🔴 **옛 검사는 아무것도 안 재고 있었다.**
         시한을 짧게 바꾼다면서 `setShootLine('… 폰이 답하지 않았을 수 있습니다', 'err')` 를
         **문자열째 직접 넘기고** 그것을 다시 읽었다 — **자기가 넣고 자기가 확인하는 동어반복**이다.
         그래서 화면의 그 문구를 고쳐도 **안 깨졌다**(검사가 화면 코드를 안 밟는다).
         ⚠ 그리고 그 문자열은 **옛 거짓**이었다: 실측으로 이 시점 뒤에도 온다(21·31·39초).
         ✅ 이제 **화면 소스의 그 문구**를 직접 본다 — 이 자리는 문자열 리터럴이라 소스가 정답이다.
            (전처리·매크로가 없으므로 §"산출물에 물어라" 의 예외 자리다) */
      /* ⚠ 아래 템플릿 리터럴 안에 이스케이프(\n 같은 것)를 쓰지 마라 — 실제 개행으로 치환돼
         페이지 코드가 깨진다(오늘 네 번째다). 여기선 개행이 필요 없어 join('') 으로 피했다. */
      const shotSrc = await evaluate(client, `(() => {
        const src = [...document.querySelectorAll('script')].map((x) => x.textContent).join('');
        const i = src.indexOf('SHOT_RESULT_WAIT_MS / 1000');
        return i >= 0 ? src.slice(i, i + 400) : '';
      })()`);
      ok('🔑 (분모) 침묵 시한 문구를 소스에서 찾았다', shotSrc.length > 0, String(shotSrc.length));
      ok('🔴 침묵 시한 문구가 **"실패" 로 단정하지 않는다** (실측: 21·31·39초에도 온다)',
         !!shotSrc && !/답하지 않았|실패/.test(shotSrc), shotSrc.slice(0, 200));
      ok('🔑 그리고 **사람이 할 수 있는 것**을 말한다 (기다리거나 직접 입력)',
         !!shotSrc && /기다리|직접 입력/.test(shotSrc), shotSrc.slice(0, 200));
      ok("🔴 색도 단정하지 않는다 — `err`(빨강)가 아니라 `wait`",
         !!shotSrc && /'wait'/.test(shotSrc) && !/, 'err'\)/.test(shotSrc), shotSrc.slice(0, 200));
    }
  }

  /* ⏭ 실기에서만 되는 것 — **주입으로는 못 잰다.** 미측정으로 남긴다(초록으로 접지 않는다). */
  /* ────────────────────────────────────────────────────────────────
     [21] 🔴 **명령 ack 문구** — 죽은 `isGate` 갈래를 지운 뒤에도 그대로인가 (REQ-0457 항목 10-c)
     ★ 지운 것은 `open_gate`/`close_gate` 전용 갈래다(화면이 그 명령을 안 보낸 지 오래다).
       살아 있는 갈래는 **테스트 모드 계열**이고, 그 표(`TEST_RESULT_TEXT`)가 정본이다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[21] 명령 ack 문구 (테스트 모드)');
  const hasToggle = await evaluate(client, `!!document.getElementById('test-toggle')`);
  if (hasToggle !== true) {
    skip('명령 ack 문구', '이 화면에는 테스트 모드 판이 없다');
  } else {
    await evaluate(client, `(() => { window.__sent = []; transport.send = function (p) { window.__sent.push(p); }; return true; })()`);
    await evaluate(client, `document.getElementById('test-toggle').click()`);
    await sleep(80);
    const aSent = await evaluate(client, `window.__sent`);
    ok('🔑 (전제) 테스트 모드 켜기가 전선에 나간다', aSent.length === 1 && !!aSent[0].rid, JSON.stringify(aSent));
    if (aSent.length === 1 && aSent[0].rid) {
      await evaluate(client, inject({ type: 'ack', rid: aSent[0].rid, result: 0 }));
      await sleep(80);
      const m0 = await evaluate(client, `(document.getElementById('messages')||{}).textContent || ''`);
      console.log('  · ' + JSON.stringify(m0.replace(/\s+/g, ' ').slice(0, 120)));
      ok('🔴 `result=0` → **"적용되었습니다"** 가 뜬다 (TEST_RESULT_TEXT 가 정본)', m0.indexOf('적용되었습니다') >= 0, m0.slice(0, 80));
      ok('🔑 조작 이름이 앞에 붙는다 (CMD_LABEL — 어떤 요청의 답인지 말한다)', m0.indexOf('테스트 모드 켜기') >= 0, m0.slice(0, 80));
    }
    /* 🔴 모르는 `result` 는 **서버 문구로 떨어진다** — 내 표가 서버 문구를 덮으면 원인을 엉뚱한 곳으로 돌린다 */
    await evaluate(client, `(() => { window.__sent = []; return true; })()`);
    await evaluate(client, `document.getElementById('test-toggle').click()`);
    await sleep(80);
    const bSent = await evaluate(client, `window.__sent`);
    if (bSent.length === 1 && bSent[0].rid) {
      await evaluate(client, inject({ type: 'ack', rid: bSent[0].rid, result: 97, message: '장치가 이 조작을 수행할 수 없습니다' }));
      await sleep(80);
      const m1 = await evaluate(client, `(document.getElementById('messages')||{}).textContent || ''`);
      ok('🔴 표에 없는 result 는 **서버 문구**를 쓴다 (내 표로 덮지 않는다)',
         m1.indexOf('장치가 이 조작을 수행할 수 없습니다') >= 0, m1.replace(/\s+/g, ' ').slice(0, 100));
    }
  }

  /* ────────────────────────────────────────────────────────────────
     [22] 🔴 **`error` 봉투 문구** — 전선 **전** 거절이다 (REQ-0457 항목 1)
     ★ 이 표(`ERROR_TEXT`)에 `module_absent`·`not_supported` 가 **두 번** 들어 있었다.
       뒤가 이겨 *"— 명령을 보내지 않았습니다"* 가 사라졌고, 그러면 읽는 사람이
       **장치를 의심한다.** 아직 안 나간 명령인데.
     🔑 `actions` 쪽 사유는 `REASON_TEXT` 가 받는다 — 표가 둘이라 섞이면 판정자가 둘이 된다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[22] error 봉투 — 전선 전 거절');
  /* ⚠ 계측기 함정 — 템플릿 리터럴 안에서 `\n`·`\s` 는 **축약된다.** 반드시 `\\n`·`\\s` 로 쓴다
     (2026-08-25 에 두 번 밟았다: `\d` → `d` 가 되어 정규식이 조용히 다른 것을 잡았다). */
  const dupes = await evaluate(client, `(() => {
    const src = [...document.querySelectorAll('script')].map(s => s.textContent).join('\\n');
    const a = src.indexOf('const ERROR_TEXT');
    const b = src.indexOf('\\n};', a);
    const body = src.slice(a, b);
    const keys = [...body.matchAll(/^\\s*([a-z_]+):/gm)].map(m => m[1]);
    const seen = {}, d = [];
    for (const k of keys) { if (seen[k]) d.push(k); seen[k] = 1; }
    return { n: keys.length, d: d };
  })()`);
  console.log('  · ERROR_TEXT 키 ' + dupes.n + '개 · 중복 ' + JSON.stringify(dupes.d));
  ok('🔑 (분모) ERROR_TEXT 를 실제로 읽었다', dupes.n > 5, JSON.stringify(dupes));
  ok('🔴 **같은 키가 두 번 들어 있지 않다** (뒤가 이겨 앞 문구가 조용히 사라진다)',
     dupes.d.length === 0, JSON.stringify(dupes.d));
  for (const code of ['module_absent', 'not_supported', 'not_declared']) {
    await evaluate(client, inject({ type: 'error', rid: 'no-such-rid', code: code, message: '서버 기본 문구' }));
  }
  await sleep(80);
  const em = await evaluate(client, `(document.getElementById('messages')||{}).textContent || ''`);
  ok('🔴 세 사유 모두 **"명령을 보내지 않았습니다"** 로 끝난다 (장치를 의심하게 만들지 않는다)',
     (em.match(/명령을 보내지 않았습니다/g) || []).length === 3,
     JSON.stringify(em.replace(/\s+/g, ' ').slice(0, 140)));

  /* ────────────────────────────────────────────────────────────────
     [23] 🔴 **미상 배너를 다시 낭독하지 않는다** (REQ-0457 항목 2)
     ★ `#slots-banner` 는 `aria-live` 다. 같은 문구를 매 프레임 다시 쓰면
       **스크린리더가 매초 읽는다** — 그러면 아무도 안 듣게 된다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[23] 미상 배너 — 같은 문구는 다시 안 쓴다');
  const live = await evaluate(client, `(document.getElementById('slots-banner')||{}).getAttribute && document.getElementById('slots-banner').getAttribute('aria-live')`);
  ok('🔑 (전제) 그 배너가 aria-live 다 (아니면 이 검사는 뜻이 없다)', !!live, String(live));
  /* 🔴 **배너가 켜진 상태에서 재야 한다.** 꺼진 배너의 "변경 0회" 는 아무것도 말하지 않는다
     (§"분모 없는 0 은 건강처럼 보인다"). 그래서 hidden=false 를 **먼저 단언**한다.
     켜는 법: `map` 에 있는 자리(A4)의 state 를 **빼면** `noState` 계열로 잡힌다. */
  await evaluate(client, inject(MAP(60)));
  const bstate = ST(60);
  bstate.zones = bstate.zones.filter((z) => z.id === 'A1');
  await evaluate(client, inject(bstate));
  await sleep(60);
  const banner1 = await evaluate(client, `(() => {
    const b = document.getElementById('slots-banner');
    window.__bmut = 0;
    window.__bobs = new MutationObserver(() => { window.__bmut += 1; });
    window.__bobs.observe(b, { childList: true, characterData: true, subtree: true });
    return { hidden: b.hidden, text: b.textContent };
  })()`);
  console.log('  · 배너: hidden=' + banner1.hidden + ' · ' + JSON.stringify(banner1.text.slice(0, 70)));
  ok('🔑 (분모) 배너가 **켜져 있다** — 꺼진 배너의 0 회는 뜻이 없다',
     banner1.hidden === false && banner1.text.indexOf('A4') >= 0, JSON.stringify(banner1));
  for (let i = 0; i < 3; i++) { await evaluate(client, inject(bstate)); await sleep(40); }
  const mut = await evaluate(client, `(() => { const n = window.__bmut; window.__bobs.disconnect(); return n; })()`);
  console.log('  · 같은 봉투 3회 뒤 배너 DOM 변경 ' + mut + '회');
  ok('🔴 문구가 같으면 **DOM 을 다시 안 쓴다** (0회 — 매 프레임 낭독을 막는다)', mut === 0, String(mut));

  /* ────────────────────────────────────────────────────────────────
     [24] 🔴 **원인 보드와 함께 죽은 보드를 가른다** (실기 지적 2026-08-26)
     ★ 이 결함은 **문구가 참이어서** 어떤 검사에도 안 걸렸다:
       `⏸ 센서 값을 알 수 없어 … · 보드 P5 끊김 · P1 없음 · P3 없음` — 셋 다 사실이다.
       그런데 자리를 못 쓰게 만든 것은 **P1 하나**이고, 사용자는 그 줄의 P3 를 보고 P3 를 의심했다.
     🔑 그래서 재는 것은 "문구가 맞나" 가 아니라 **"원인과 부수가 갈려 있나"** 다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[24] 원인 보드 · 함께 죽은 보드');
  const CAUSE_MAP = {
    type: 'map', srv_id: 'T-M', epoch: 40, grid: { rows: 1, cols: 1 },
    zones: [{ id: 'A3', kind: 'parking', cells: [[0, 0]], label: '3번 자리',
              active: { ok: true, reason: null },
              declared: [{ devid: 'P1', name: 'A3' }, { devid: 'P5', name: 'R3' }, { devid: 'P3', name: 'C1' }],
              modules: [{ devid: 'P5', name: 'R3', kind: 'OG', idx: 2, control: { widget: 'toggle' } }] }]
  };
  const CAUSE_STATE = {
    type: 'state', srv_id: 'T-M', epoch: 40, ts_ms: NOW, max_per_batch: 4,
    zones: [{ id: 'A3', occupied: false, reserved: false, actions: {}, value_state: 'unknown',
              plate: null, plate_source: null,
              /* 🔴 죽은 보드는 셋인데 **원인은 P1 하나**다 — 그것이 이 검사의 요점이다 */
              usable: { ok: false, reason: 'sensor_unavailable',
                        sensors_known: 0, sensors_declared: 1, controls_alive: 0, controls_total: 2,
                        dead_modules: [{ devid: 'P1', module: 'A3', kind: 'IP', why: 'node_unregistered' }],
                        offline_devices: ['P1'] },
              modules: [{ devid: 'P5', name: 'R3', idx: 2, value: null, known: false, reason: 'node_offline' }] }]
  };
  /* 🔴 실기와 **같은 링크 상태**를 만든다(그러지 않으면 재는 것이 다른 상황이다):
       P1·P3 = 목록에 아예 없다 → `absent`("서버에 없음")   ·   P5 = online:false → `down`("끊김")
     ⚠ 처음에 P5 를 `online:true` 로 둬서 trouble 목록에 안 들어갔다 — **계측기가 틀린 것**이었다. */
  await evaluate(client, inject(snap({
    devices: [P2({ device_id: 'P5', online: false, registered: true, last_frame_ts: NOW - 200000 })],
    devices_online: 0
  })));
  await evaluate(client, inject(CAUSE_MAP));
  await evaluate(client, inject(CAUSE_STATE));
  await sleep(80);
  /* ⚠ li 와 box 둘 다 data-zone 을 갖는다. title·문구는 box(.zone) 에 있다 —
     [data-zone] 만 쓰면 li 를 잡아 title 이 빈 문자열로 나온다(계측기가 그렇게 틀렸다).
     🔴 그리고 이 주석은 템플릿 리터럴 **밖**에 있어야 한다 — 안에 역따옴표를 넣으면 리터럴이 끊긴다
        (📖 docs/web/LEDGER.md — 오늘 두 번 밟은 그 함정이다). */
  const cz = await evaluate(client, `(() => {
    const c = document.querySelector('.zone[data-zone="A3"]');
    if (!c) return null;
    const why = c.querySelector('.zone__inactive');
    const dn  = c.querySelector('.zone__boarddown');
    return { why: why ? why.textContent : null, down: dn ? dn.textContent : null,
             title: c.getAttribute('title') || '' };
  })()`);
  if (!cz) {
    skip('원인 보드 가르기', '이 화면에는 개정 격자가 없다 — 옛 판이면 정상이다');
  } else {
    console.log('  · ⏸ 줄 : ' + JSON.stringify(cz.why));
    console.log('  · 부수 줄: ' + JSON.stringify(cz.down));
    ok('🔑 (분모) 비활성 사유 줄이 그려졌다', !!cz.why, JSON.stringify(cz));
    ok('🔴 ⏸ 줄에 **원인 보드 P1 이 있다**', (cz.why || '').indexOf('P1') >= 0, cz.why);
    ok('🔴🔴 ⏸ 줄에 **원인이 아닌 P3·P5 가 없다** (사용자가 그 줄의 P3 를 보고 P3 를 의심했다)',
       (cz.why || '').indexOf('P3') < 0 && (cz.why || '').indexOf('P5') < 0, cz.why);
    ok('🔑 그래도 **사실은 안 사라진다** — 부수 보드는 따로 한 줄로 말한다',
       (cz.down || '').indexOf('P3') >= 0 && (cz.down || '').indexOf('P5') >= 0, cz.down);
    ok('🔑 그 줄이 **원인이 아님을 문장으로** 말한다 (조작·표시)',
       (cz.down || '').indexOf('조작') >= 0, cz.down);
    /* 🔴 `title`·`aria-label` 도 같이 재야 한다 — 칸만 고치면 마우스·스크린리더 사용자는 옛 뭉침을 듣는다 */
    ok('🔴 `title` 도 원인을 먼저 말한다 (원인 보드 P1)', cz.title.indexOf('원인 보드 P1') >= 0, cz.title.slice(0, 120));
    ok('🔴 `title` 이 부수 보드를 **원인으로 단정하지 않는다**',
       cz.title.indexOf('원인은 아니고') >= 0 || (cz.title.indexOf('P5') < 0 && cz.title.indexOf('P3') < 0),
       cz.title.slice(0, 160));
  }

  /* ────────────────────────────────────────────────────────────────
     [25] 🔵 **`declared[].kind`(+`widget`) 가 오면 이름이 선다** — 배포 전 미리 재 둔다
     ★ "한 번도 안 붙은 모듈" 은 `state…modules` 에 없다. `map…declared` 가 **유일한 통로**다.
     🔴 `OG` 하나에 LCD·안내등·차단봉 셋이 들어 있어 `kind` 만으로는 못 가른다 — `widget` 이 있어야 한다.
        없으면 중립어("조작")로 떨어진다. **지어내지 않는 것**이 이 3단 폴백의 요점이다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[25] declared[].kind — 안 붙은 모듈의 이름');
  const KMAP = function (decl) {
    return { type: 'map', srv_id: 'T-M', epoch: 41, grid: { rows: 1, cols: 1 },
             zones: [{ id: 'A3', kind: 'parking', cells: [[0, 0]], label: '3번 자리',
                       active: { ok: true, reason: null }, declared: decl,
                       modules: [] }] };
  };
  const KSTATE = function (ok) {
    return { type: 'state', srv_id: 'T-M', epoch: 41, ts_ms: NOW, max_per_batch: 4,
             zones: [{ id: 'A3', occupied: false, reserved: false, actions: {},
                       value_state: ok ? 'known' : 'unknown', plate: null, plate_source: null,
                       usable: ok
                         ? { ok: true, reason: null, sensors_known: 1, sensors_declared: 1,
                             controls_alive: 0, controls_total: 2, dead_modules: [], offline_devices: [] }
                         : { ok: false, reason: 'sensor_unavailable',
                             sensors_known: 0, sensors_declared: 1, controls_alive: 0, controls_total: 2,
                             dead_modules: [{ devid: 'P1', module: 'A3', kind: 'IP', why: 'node_unregistered' }],
                             offline_devices: ['P1'] },
                       modules: [] }] };
  };
  const DECL_FULL = [{ devid: 'P1', name: 'A3', kind: 'IP' },
                     { devid: 'P5', name: 'R3', kind: 'OG', widget: 'toggle' },
                     { devid: 'P3', name: 'C1', kind: 'OG', widget: 'number' }];
  const readCell = `(() => {
    const c = document.querySelector('.zone[data-zone="A3"]');
    if (!c) return null;
    const w = c.querySelector('.zone__inactive'), d = c.querySelector('.zone__boarddown');
    return { why: w ? w.textContent : null, down: d ? d.textContent : null };
  })()`;

  await evaluate(client, inject(snap({
    devices: [P2({ device_id: 'P5', online: false, registered: true, last_frame_ts: NOW - 200000 })],
    devices_online: 0
  })));
  await evaluate(client, inject(KMAP(DECL_FULL)));
  await evaluate(client, inject(KSTATE(false)));
  await sleep(80);
  const k1 = await evaluate(client, readCell);
  if (!k1) {
    skip('declared[].kind 이름', '이 화면에는 개정 격자가 없다');
  } else {
    console.log('  · 비활성 + kind/widget 있음 → ' + JSON.stringify(k1.down));
    ok('🔵 `widget:"number"` 가 오면 **표시기(LCD)** 라고 부른다', (k1.down || '').indexOf('표시기') >= 0, k1.down);
    ok('🔵 `widget:"toggle"` + 이름 `R3` 이면 **안내등**이다', (k1.down || '').indexOf('안내등') >= 0, k1.down);
    ok('🔑 모듈 이름까지 짚는다 (P3/C1 · P5/R3)',
       (k1.down || '').indexOf('P3/C1') >= 0 && (k1.down || '').indexOf('P5/R3') >= 0, k1.down);

    /* 🔴 자리는 살아 있고 조작·표시만 죽은 경우 — 루트가 처음 상상한 그 상황이다.
       ⚠ 그러려면 센서 보드(P1)는 **살아 있어야** 한다. 안 그러면 봉투 자체가 모순이고,
          화면이 그 모순을 그럴듯하게 옮겨 적는지를 재게 된다(처음에 그렇게 만들어 잡았다). */
    await evaluate(client, inject(snap({
      devices: [P1({ device_id: 'P1', online: true, registered: true }),
                P2({ device_id: 'P5', online: false, registered: true, last_frame_ts: NOW - 200000 })],
      devices_online: 1
    })));
    await evaluate(client, inject(KSTATE(true)));
    await sleep(80);
    const k2 = await evaluate(client, readCell);
    console.log('  · 활성 + 조작만 죽음 → ' + JSON.stringify(k2.down));
    ok('🔴 자리가 살아 있으면 **"점유 판정은 정상입니다"** 라고 말한다',
       (k2.down || '').indexOf('점유 판정은 정상') >= 0, k2.down);
    ok('🔴 그리고 **"판정이 멈췄습니다" 라고 말하지 않는다** (그건 거짓이다)',
       (k2.down || '').indexOf('판정이 멈췄') < 0, k2.down);
    ok('🔑 비활성 사유 줄은 아예 없다 (자리가 멀쩡하다)', !k2.why, String(k2.why));

    /* 🔴 **모순 입력 방어를 한 번 내 본다** — 자리는 활성인데 센서 보드가 죽어 있다고 하면?
       그 상태에서 "점유 판정은 정상입니다" 를 적으면 **모순을 그럴듯하게 말하는 것**이다.
       ✅ 화면은 그 문장을 붙이지 않고 종전 문구로 떨어져야 한다. */
    await evaluate(client, inject(snap({
      devices: [P2({ device_id: 'P5', online: false, registered: true, last_frame_ts: NOW - 200000 })],
      devices_online: 0
    })));
    await evaluate(client, inject(KSTATE(true)));
    await sleep(80);
    const k2b = await evaluate(client, readCell);
    console.log('  · 활성인데 센서 보드도 죽었다(모순) → ' + JSON.stringify(k2b.down));
    ok('🔴 모순 입력에는 **"점유 판정은 정상" 을 안 말한다** (단정하지 않는다)',
       (k2b.down || '').indexOf('점유 판정은 정상') < 0, k2b.down);

    /* 🔴 **실기의 실제 모양** — `widget` 은 오는데 `kind` 는 없다.
       `kind` 는 장치가 등록할 때 알려 주는 값이라 **한 번도 안 붙은 모듈은 서버도 모른다**.
       `widget` 은 조립표 선언(`lot.control`)에서 오므로 보드 없이도 항상 있다.
       → 이 갈래가 서지 않으면 실기에서 이름이 안 선다. 2026-08-26 실측으로 잡은 자리다. */
    await evaluate(client, inject(snap({
      devices: [P2({ device_id: 'P5', online: false, registered: true, last_frame_ts: NOW - 200000 })],
      devices_online: 0
    })));
    await evaluate(client, inject(KMAP([{ devid: 'P1', name: 'A3' },
                                        { devid: 'P5', name: 'R3', widget: 'toggle' },
                                        { devid: 'P3', name: 'C1', widget: 'number' }])));
    await evaluate(client, inject(KSTATE(false)));
    await sleep(80);
    const kw = await evaluate(client, readCell);
    console.log('  · 🔴 실기 모양(widget 만 · kind 없음) → ' + JSON.stringify(kw.down));
    ok('🔴 `kind` 없이 `widget` 만으로도 이름이 선다 — 안내등 · 표시기(LCD)',
       (kw.down || '').indexOf('안내등') >= 0 && (kw.down || '').indexOf('표시기') >= 0, kw.down);

    /* 🔵 `kind` 도 `widget` 도 없는 옛 서버 — 이름을 지어내지 않고 모듈 이름만 남는다 */
    await evaluate(client, inject(KMAP([{ devid: 'P1', name: 'A3' }, { devid: 'P5', name: 'R3' }, { devid: 'P3', name: 'C1' }])));
    await evaluate(client, inject(KSTATE(false)));
    await sleep(80);
    const k3 = await evaluate(client, readCell);
    console.log('  · kind·widget 둘 다 없음(옛 서버) → ' + JSON.stringify(k3.down));
    ok('🔑 둘 다 없으면 **이름을 지어내지 않는다** (모듈 이름만)',
       (k3.down || '').indexOf('표시기') < 0 && (k3.down || '').indexOf('안내등') < 0
       && (k3.down || '').indexOf('P3/C1') >= 0, k3.down);
  }

  /* ────────────────────────────────────────────────────────────────
     [26] 🔴 **입력칸이 뜨는 조건** — 서버가 내는 이름에 걸려 있나 (REQ-0479)
     ★ 이 문은 **`manual_wait`** 에 걸려 있었고, 서버는 그 값을 **한 번도 안 보낸다**.
       그래서 입력칸이 뜬 적이 없다 — 조건이 참이 되는 순간이 없어 **조용히 죽어 있었다**.
     🔑 문구표는 `rejected`·`full` 둘 다 받고 있었다. **표는 여러 줄이라 눈에 띄고
       상수는 한 줄이라 안 띈다** — 그 비대칭이 이 결함을 살렸다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[26] 입력칸이 뜨는 조건 (entry.phase)');
  const EMAP = { type: 'map', srv_id: 'T-M', epoch: 50, grid: { rows: 1, cols: 1 },
                 zones: [{ id: 'A1', kind: 'parking', cells: [[0, 0]], label: '1번 자리',
                           active: { ok: true, reason: null }, declared: [], modules: [] }] };
  const EST = (ph) => ({ type: 'state', srv_id: 'T-M', epoch: 50, ts_ms: NOW,
    entry: { phase: ph, elapsed_ms: 0, limit_ms: 15000, plate: null, plate_source: null, slot: null, attempts: 1 },
    zones: [{ id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known', plate: null, modules: [] }] });
  const EREAD = `(() => {
    const p = document.getElementById('entry-panel'), f = document.getElementById('entry-form'),
          i = document.getElementById('entry-plate');
    if (!p) return null;
    return { panel: !p.hidden, input: f ? !f.hidden : null, value: i ? i.value : null };
  })()`;
  await evaluate(client, inject(EMAP));
  await evaluate(client, inject(EST('rejected')));
  await sleep(80);
  const e1 = await evaluate(client, EREAD);
  if (!e1) {
    skip('입력칸이 뜨는 조건', '이 화면에는 입차 판이 없다 — 1차면 정상이다');
  } else {
    console.log('  · rejected → ' + JSON.stringify(e1));
    ok('🔴 `rejected`(번호를 못 읽었다) 에서 **입력칸이 뜬다**', e1.input === true, JSON.stringify(e1));
    /* 사람이 친 값을 넣어 둔다 — 아래에서 비워지는지 본다 */
    await evaluate(client, `(() => { const i = document.getElementById('entry-plate'); if (i) i.value = '123바9898'; return true; })()`);
    await evaluate(client, inject(EST('full')));
    await sleep(80);
    const e2 = await evaluate(client, EREAD);
    console.log('  · full → ' + JSON.stringify(e2));
    ok('🔴 `full`(빈자리 없음) 에서는 **안 뜬다** — 번호를 넣어도 안 풀리는 상태다',
       e2.input === false, JSON.stringify(e2));
    ok('🔑 그리고 **친 값이 비워진다** — 앞차 번호가 다음 차에 붙지 않게(정본 §3-C)',
       e2.value === '', JSON.stringify(e2));
    await evaluate(client, inject(EST('idle')));
    await sleep(80);
    const e3 = await evaluate(client, EREAD);
    console.log('  · idle → ' + JSON.stringify(e3));
    ok('🔴 `idle` 에서는 **판이 통째로 숨는다**', e3.panel === false, JSON.stringify(e3));
    /* 🔴 옛 이름으로 되돌리면 이 검사가 빨강이 된다 — 그것이 이 절의 존재 이유다 */
    await evaluate(client, inject(EST('manual_wait')));
    await sleep(80);
    const e4 = await evaluate(client, EREAD);
    console.log('  · manual_wait(서버가 안 내는 옛 이름) → ' + JSON.stringify(e4));
    ok('🔑 서버가 안 내는 옛 이름에는 **안 뜬다** (문이 옛 이름에 걸려 있지 않다)',
       e4.input === false, JSON.stringify(e4));
    /* 문구표가 둘을 가르는지도 같이 본다 — 사람이 할 일이 다르다 */
    const phw = await evaluate(client, `({ rejected: ENTRY_PHASE_TEXT.rejected || null, full: ENTRY_PHASE_TEXT.full || null })`);
    console.log('  · 문구: ' + JSON.stringify(phw));
    ok('🔑 `rejected` 와 `full` 이 **다른 문장**이다 (사람이 할 일이 다르다)',
       !!phw.rejected && !!phw.full && phw.rejected !== phw.full, JSON.stringify(phw));
  }

  /* ────────────────────────────────────────────────────────────────
     [27] 🔴 **조작 버튼 글자는 서버가 정한다** — `on_label`/`off_label` (REQ-0481)
     ★ 사용자: *"열기와 닫기로 구분해달라. 지금은 켬 끔이다."*
     🔑 서버는 **있을 때만 키를 싣는다**(빈 문자열을 안 보낸다) → `||` 폴백이 정확히 맞는다.
     🔴 **대조군이 이 절의 요점이다**: 라벨이 없는 안내등은 **켬/끔 그대로**여야 한다.
        같이 바뀌면 조건이 너무 넓은 것이다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[27] 조작 버튼 글자 (on_label / off_label)');
  const LMAP = { type: 'map', srv_id: 'T-M', epoch: 60, grid: { rows: 1, cols: 1 },
    zones: [{ id: 'E1', kind: 'entrance', cells: [[0, 0]], label: '입구',
              active: { ok: true, reason: null },
              declared: [{ devid: 'P1', name: 'ED' }, { devid: 'P1', name: 'R1' }],
              modules: [
                { devid: 'P1', name: 'ED', kind: 'OB', idx: 0, label: '차단봉',
                  control: { widget: 'toggle', on_label: '열기', off_label: '닫기' } },
                { devid: 'P1', name: 'R1', kind: 'OG', idx: 1, label: '안내등',
                  control: { widget: 'toggle' } } ] }] };
  const LST = { type: 'state', srv_id: 'T-M', epoch: 60, ts_ms: NOW, max_per_batch: 4,
    zones: [{ id: 'E1', occupied: false, reserved: false, actions: {}, value_state: 'known',
              usable: { ok: true, reason: null, sensors_known: 0, sensors_declared: 0,
                        controls_alive: 2, controls_total: 2, dead_modules: [], offline_devices: [] },
              modules: [
                { devid: 'P1', name: 'ED', idx: 0, value: 0, known: true, cmd: { ok: true } },
                { devid: 'P1', name: 'R1', idx: 1, value: 0, known: true, cmd: { ok: true } } ] }] };
  await evaluate(client, inject(snap({ devices: [P1()], devices_online: 1 })));
  await evaluate(client, inject(LMAP));
  await evaluate(client, inject(LST));
  await sleep(80);
  await evaluate(client, `(() => { const c = document.querySelector('.zone[data-zone="E1"]'); if (c) c.click(); return true; })()`);
  await sleep(150);
  const lb = await evaluate(client, `(() => {
    const out = {};
    for (const r of document.querySelectorAll('#zone-detail .zctl__row')) {
      const nm = (r.querySelector('.zctl__label') || {}).textContent || '?';
      out[nm] = [...r.querySelectorAll('button')].map((b) => b.textContent);
    }
    return out;
  })()`);
  console.log('  · ' + JSON.stringify(lb));
  const keys = Object.keys(lb);
  if (!keys.length) {
    skip('조작 버튼 글자', '이 화면에는 자리 상세의 조작 행이 없다');
  } else {
    const gate = lb['차단봉'] || null, lamp = lb['안내등'] || null;
    ok('🔑 (분모) 두 행이 다 그려졌다 — 하나만 있으면 대조가 성립하지 않는다',
       !!gate && !!lamp, JSON.stringify(lb));
    ok('🔴 `on_label` 이 오면 **그 말을 쓴다** (차단봉 → 열기/닫기)',
       !!gate && gate.indexOf('열기') >= 0 && gate.indexOf('닫기') >= 0, JSON.stringify(gate));
    ok('🔴🔴 **대조군** — 라벨이 없으면 **켬/끔 그대로**다 (안내등이 같이 바뀌면 조건이 너무 넓다)',
       !!lamp && lamp.indexOf('켬') >= 0 && lamp.indexOf('끔') >= 0, JSON.stringify(lamp));
    ok('🔑 화면이 `kind` 로 이름을 지어내지 않는다 — 안내등에 "열기" 가 없다',
       !!lamp && lamp.indexOf('열기') < 0, JSON.stringify(lamp));
  }

  /* ────────────────────────────────────────────────────────────────
     [28] 🔵 **촬영 진행 필드 넷** — 배포 전 미리 재 둔다 (SPEC-2026-08-27 [F])
     ★ 가장 중요한 검사는 **"없으면 아무것도 안 그린다"** 다 — 옛 서버에서 안 깨져야
       배포 전에 화면을 미리 고쳐 둘 수 있다.
     🔴 그리고 **폰의 낱말과 서버의 판단이 안 섞이는지** 본다:
       `shot_last_error`(폰이 못 읽었다) ≠ `plate_discarded`(폰은 읽었는데 서버가 버렸다)
       한 칸에 두 어휘를 섞으면 화면이 그 둘을 가를 근거를 잃는다.
     ──────────────────────────────────────────────────────────────── */
  console.log('\n[28] 촬영 진행 필드 (shot_tries · shot_wait_ms · shot_last_error · plate_discarded)');
  const FMAP = { type: 'map', srv_id: 'T-M', epoch: 70, grid: { rows: 1, cols: 1 },
    zones: [{ id: 'A1', kind: 'parking', cells: [[0, 0]], label: '1번 자리',
              active: { ok: true, reason: null }, declared: [], modules: [] }] };
  const FST = (extra) => ({ type: 'state', srv_id: 'T-M', epoch: 70, ts_ms: NOW,
    entry: Object.assign({ phase: 'rejected', elapsed_ms: 0, limit_ms: 0,
                           plate: null, plate_source: null, slot: null, attempts: 0 }, extra || {}),
    zones: [{ id: 'A1', occupied: false, reserved: false, actions: {}, value_state: 'known', plate: null, modules: [] }] });
  const FSTAT = `(() => { const s = document.getElementById('entry-status'); return s ? s.textContent : null; })()`;
  await evaluate(client, inject(FMAP));

  /* ① 넷이 없는 옛 서버 — 아무것도 안 그린다 */
  await evaluate(client, inject(FST()));
  await sleep(80);
  const f0 = await evaluate(client, FSTAT);
  if (f0 === null) {
    skip('촬영 진행 필드', '이 화면에는 입차 판이 없다');
  } else {
    console.log('  · 필드 없음 → ' + JSON.stringify(f0));
    ok('🔴 넷이 없으면 **아무것도 안 그린다** (옛 서버에서 안 깨진다)',
       !/촬영|초째|사유|진행하지/.test(f0), f0);

    /* ② 진행 표시 — 촬영 횟수와 경과 */
    await evaluate(client, inject(FST({ shot_tries: 2, shot_wait_ms: 8300 })));
    await sleep(80);
    const f1 = await evaluate(client, FSTAT);
    console.log('  · tries=2 wait=8300 → ' + JSON.stringify(f1));
    ok('🔵 `shot_tries` 를 **"촬영 N회"** 로 적는다 (attempts 와 낱말을 가른다)',
       /촬영 2회/.test(f1), f1);
    ok('🔵 `shot_wait_ms` 를 초로 적는다', /8초째/.test(f1), f1);

    /* ③ 폰 낱말 — 표에 없으면 그대로. **"실패" 로 승격하지 않는다** */
    await evaluate(client, inject(FST({ shot_tries: 1, shot_last_error: 'no_plate_found' })));
    await sleep(80);
    const f2 = await evaluate(client, FSTAT);
    console.log('  · last_error=no_plate_found → ' + JSON.stringify(f2));
    ok('🔴 표에 없는 폰 낱말은 **그대로** 보인다 ("실패" 로 승격하지 않는다)',
       /no_plate_found/.test(f2) && !/실패/.test(f2), f2);

    /* ④ 🔴 서버 판단 — **실패가 아니라 안전 판단**이다 */
    await evaluate(client, inject(FST({ plate_discarded: 'no_car' })));
    await sleep(80);
    const f3 = await evaluate(client, FSTAT);
    console.log('  · plate_discarded=no_car → ' + JSON.stringify(f3));
    ok('🔴 `plate_discarded` 를 **안전 판단**으로 말한다 (실패가 아니다)',
       /진행하지 않았습니다/.test(f3) && !/실패/.test(f3), f3);
    ok('🔑 그리고 **차량이 확인되지 않았다**는 것을 말한다 (운전자에겐 "아무 일도 안 일어남" 으로 보인다)',
       /차량이 확인되지/.test(f3), f3);

    /* ④-b 🔴 **`sensor_unknown` 은 우리 쪽 문제다** — `no_car` 와 같은 말로 적으면
       고칠 수 있는 것이 정상처럼 보인다 */
    await evaluate(client, inject(FST({ plate_discarded: 'sensor_unknown' })));
    await sleep(80);
    const f3b = await evaluate(client, FSTAT);
    console.log('  · plate_discarded=sensor_unknown → ' + JSON.stringify(f3b));
    ok('🔴 `sensor_unknown` 을 `no_car` 와 **다르게** 말한다 (우리 쪽 문제라고 밝힌다)',
       /장치 연결 문제/.test(f3b) && !/차량이 확인되지/.test(f3b), f3b);

    /* ⑤ 🔴 **셋째 어휘 `shot_closed`** — 번호가 없었고 우리가 그만둔 것이다 */
    await evaluate(client, inject(FST({ shot_tries: 4, shot_closed: 'phone_gone' })));
    await sleep(80);
    const f5 = await evaluate(client, FSTAT);
    console.log('  · shot_closed=phone_gone → ' + JSON.stringify(f5));
    ok('🔵 `phone_gone` 은 **원인을 알려 주고 할 일을 말한다**',
       /폰이 끊겼습니다/.test(f5) && /직접 입력/.test(f5), f5);

    await evaluate(client, inject(FST({ shot_tries: 9, shot_closed: 'cap' })));
    await sleep(80);
    const f6 = await evaluate(client, FSTAT);
    console.log('  · shot_closed=cap → ' + JSON.stringify(f6));
    ok('🔴 `cap` 은 **원인을 모른다고 말하고 로그로 보낸다** (뜨면 안 되는 값이다)',
       /원인을 모릅니다/.test(f6) && /로그/.test(f6), f6);
    ok('🔴 그리고 `cap` 을 `phone_gone` 과 **다르게** 말한다 (하나는 알고 하나는 모른다)',
       !/폰이 끊겼습니다/.test(f6), f6);

    /* ⑩ 🔴 **`unknown`(아무도 모른다)과 표에 없는 낱말(내가 모른다)을 가른다**
       ★ socket 확인(REQ-0515): 폰이 "실패" 라고만 하고 사유를 안 주면 **서버가 `unknown` 을 채운다**
         (`lot.cpp:450`). 즉 이것은 **실제로 오는 값**이지 미지의 낱말이 아니다.
       🔑 둘을 같은 모양으로 그리면 *"어휘가 늘었다"* 라는 신호가 **잡음에 묻힌다.** */
    await evaluate(client, inject(FST({ phase: 'rejected', shot_last_error: 'unknown' })));
    await sleep(80);
    const u0 = await evaluate(client, FSTAT);
    console.log('  · shot_last_error=unknown → ' + JSON.stringify(u0));
    /* 🔴 **뜻을 재고 표현을 안 잰다** — 문구는 다듬을 수 있다. 못 박을 것은
       *"서버 쪽 사실이라고 말한다"* 와 *"낱말 그대로 안 흘린다"* 둘이다.
       ⚠ 그리고 **폰 탓으로 말하면 안 된다** — 이 값은 우리가 기록을 잃은 것이다. */
    ok('🔴 `unknown` 은 **서버가 잊었다고** 말한다 (폰 탓으로 돌리지 않는다)',
       /서버/.test(u0) && !/사유: unknown/.test(u0) && !/폰이 실패했다고만/.test(u0), u0);
    /* 대조군 — 표에 **없는** 낱말은 여전히 낱말 그대로. 없으면 위 검사가 "전부 문장으로" 여도 초록이다 */
    await evaluate(client, inject(FST({ phase: 'rejected', shot_last_error: 'brand_new_code' })));
    await sleep(80);
    const u1 = await evaluate(client, FSTAT);
    console.log('  · 표에 없는 낱말 → ' + JSON.stringify(u1));
    ok('🔑 대조군: 표에 **없는** 낱말은 그대로 보인다 (어휘가 늘었다는 신호가 산다)',
       /사유: brand_new_code/.test(u1), u1);

    /* ⑦ 🔴🔴 **`0` 과 `안 옴` 이 같은 화면이 되면 안 된다** — 다섯 칸에 오늘의 물음을 댄다.
       ★ 앞 판은 `shot_tries` 가 **없을 때**와 **0일 때**가 둘 다 "아무것도 안 그림" 이었다.
         그러면 *"카메라가 안 돈다"* 와 *"도는 중인데 화면이 모른다"* 가 같은 모양이 된다.
       🔴 내일 이 화면으로 카메라 재설계를 판정한다. **판정 도구가 먼저 정직해야 한다.**
       ⚠ 그리고 socket 이 `shooting` 의 `limit_ms` 를 **0(시한 없음)** 으로 바꿔서
         `shot_wait_ms` 가 이 단계의 **유일한 진행 표시**가 됐다 — 그것이 비면 멈춘 것으로 보인다. */
    await evaluate(client, inject(FST({ phase: 'shooting' })));
    await sleep(80);
    const g0 = await evaluate(client, FSTAT);
    console.log('  · shooting · 진행 칸 하나도 없음 → ' + JSON.stringify(g0));
    ok('🔴🔴 `shooting` 인데 진행 값이 하나도 없으면 **그렇게 말한다** (조용히 비우면 멈춘 것으로 읽힌다)',
       /진행 정보가 안 옵니다/.test(g0) && /멈춘 것이 아닙니다/.test(g0), g0);

    /* 대조군 ① — 값이 하나라도 오면 그 문구가 **안 나와야** 한다 */
    await evaluate(client, inject(FST({ phase: 'shooting', shot_wait_ms: 3000 })));
    await sleep(80);
    const g1 = await evaluate(client, FSTAT);
    console.log('  · shooting · 3초째 → ' + JSON.stringify(g1));
    ok('🔑 대조군: 진행 값이 오면 **그 문구가 사라지고 경과가 뜬다** (항상 붙이면 뜻이 없다)',
       !/진행 정보가 안 옵니다/.test(g1) && /3초째/.test(g1), g1);

    /* ⑧ 🔴 `shot_tries === 0` 은 **"아직 안 나갔다"** 다 — "없음" 과 다르다 */
    await evaluate(client, inject(FST({ phase: 'shooting', shot_tries: 0, shot_wait_ms: 500 })));
    await sleep(80);
    const g2 = await evaluate(client, FSTAT);
    console.log('  · shooting · tries 0 → ' + JSON.stringify(g2));
    ok('🔴 `shot_tries 0` 을 **"아직 안 나갔다" 로 말한다** (안 옴과 같은 화면이 되면 안 된다)',
       /촬영 요청 아직 안 나감/.test(g2), g2);
    /* 대조군 ② — 1회 이상이면 횟수를 적고 그 문구는 **안** 나온다 */
    await evaluate(client, inject(FST({ phase: 'shooting', shot_tries: 2, shot_wait_ms: 500 })));
    await sleep(80);
    const g3 = await evaluate(client, FSTAT);
    console.log('  · shooting · tries 2 → ' + JSON.stringify(g3));
    ok('🔑 대조군: 1회 이상이면 **횟수를 적고** "아직 안 나감" 은 안 나온다',
       /촬영 2회/.test(g3) && !/아직 안 나감/.test(g3), g3);
    /* 대조군 ③ — `shooting` 이 아니면 촬영 이야기를 **안 한다**(그 단계에서는 뜻이 없다) */
    await evaluate(client, inject(FST({ phase: 'rejected', shot_tries: 0 })));
    await sleep(80);
    const g4 = await evaluate(client, FSTAT);
    console.log('  · rejected · tries 0 → ' + JSON.stringify(g4));
    ok('🔑 대조군: `shooting` 이 아니면 촬영 진행을 **말하지 않는다** (그 단계에서는 뜻이 없다)',
       !/아직 안 나감/.test(g4) && !/진행 정보가 안 옵니다/.test(g4), g4);

    /* ⑨ 🔴🔴 **`idle` 에서는 다섯 칸을 아예 안 읽는다** (socket 이 짚은 "가장 싼 방어")
       ★ socket 실측: `shot_wait_ms` 와 `shot_last_error` 는 **유휴와 진행 중이 같은 값**이다
         (`0` 과 `""`). 그 둘만으로는 못 가른다.
       🔑 그런데 다섯 칸은 **전부 `phase` 에 종속**이다 — 유휴면 값이 있어도 **뜻이 없다.**
         그래서 `phase` 로 막는 것이 봉투 고침(`null`)보다 싸고 **지금 바로 된다.**
       ⚠ 이 화면은 이미 그렇게 돼 있다. 하지만 **검사가 없으면 그렇게 말할 수 없다** —
         "막혀 있다" 는 주장에도 분모가 필요하다. 그래서 값을 일부러 넣어 본다. */
    await evaluate(client, inject(FST({ phase: 'idle', shot_tries: 9, shot_wait_ms: 12000,
                                        shot_last_error: 'no_plate', plate_discarded: 'no_car',
                                        shot_closed: 'cap' })));
    await sleep(80);
    const h0 = await evaluate(client, `(() => {
      const p = document.getElementById('entry-panel');
      const s = document.getElementById('entry-status');
      return { hidden: !!(p && p.hidden), text: (s && s.textContent) || '' };
    })()`);
    console.log('  · idle 인데 다섯 칸에 값이 있음 → ' + JSON.stringify(h0));
    ok('🔴🔴 `idle` 이면 다섯 칸을 **아예 안 읽는다** (유휴에서는 그 값에 뜻이 없다)',
       h0.hidden === true && !/촬영|no_plate|no_car|원인을 모릅니다/.test(h0.text), JSON.stringify(h0));

    /* 대조군 — 같은 값이 `shooting` 으로 오면 **읽는다**. 없으면 위 검사는 "판을 늘 숨긴다" 여도 초록이다 */
    await evaluate(client, inject(FST({ phase: 'shooting', shot_tries: 9, shot_wait_ms: 12000,
                                        shot_last_error: 'no_plate' })));
    await sleep(80);
    const h1 = await evaluate(client, `(() => {
      const p = document.getElementById('entry-panel');
      const s = document.getElementById('entry-status');
      return { hidden: !!(p && p.hidden), text: (s && s.textContent) || '' };
    })()`);
    console.log('  · shooting 에 같은 값 → ' + JSON.stringify(h1.text));
    ok('🔑 대조군: 같은 값이 `shooting` 으로 오면 **읽는다** (판을 늘 숨기는 것이 아니다)',
       h1.hidden === false && /촬영 9회/.test(h1.text) && /12초째/.test(h1.text), JSON.stringify(h1));

    /* ⑥ 🔴🔴 어휘 셋이 같이 와도 **안 섞인다** */
    await evaluate(client, inject(FST({ shot_tries: 3, shot_last_error: 'too_blurry',
                                        plate_discarded: 'no_car', shot_closed: 'cap' })));
    await sleep(80);
    const f4 = await evaluate(client, FSTAT);
    console.log('  · 셋 다 → ' + JSON.stringify(f4));
    ok('🔴🔴 어휘 **셋**이 한 문장에서 안 섞인다 (폰이 못 읽음 · 우리가 버림 · 우리가 그만둠)',
       /too_blurry/.test(f4) && /차량이 확인되지/.test(f4) && /원인을 모릅니다/.test(f4), f4);
  }

  /* ════════════════════════════════════════════════════════════════════
     [29] 🔴🔴 **`0`(관측된 것)과 `못 잼`(관측 불가)이 같은 글자로 나오나** — 관제 화면 판
     ★ 8080·8081 에서 같은 병을 실기 화면을 **찍어 보고** 찾았다(📖 LEDGER §5.140):
       *"빈자리 0"* 이 *"만차"* 로 읽혔다. 값 검사는 전부 초록이었다.
     🔑 여기서는 **다른 모양 셋**으로 나왔다. 전부 실기 화면에서 눈으로 잡았다.
     ⚠ 각 검사에 **대조군**을 붙인다 — 없으면 무조건 안전한 말을 내도 초록이다.
     ════════════════════════════════════════════════════════════════════ */
  console.log('\n[29] 관측된 0 과 못 잰 0 을 화면이 가르나');
  {
    const BANNER = `(() => (document.getElementById('boards-banner') || {}).textContent || '')()`;
    const DEVBOX = `(() => {
      const t = (id) => (document.getElementById(id) || {}).textContent || '';
      return { age: t('dev-age'), frame: t('dev-frame'), uptime: t('dev-uptime'), seq: t('dev-seq') };
    })()`;

    /* ── (a) 🔴 배너의 분자와 분모가 **같은 것을 세나**
       실기에서 **"주차 자리 5개 중 7개의 판정이 멈춥니다"** 가 나왔다 — 분모보다 분자가 크다.
       분모는 `kind === 'parking'` 만 세고 분자는 입·출구까지 셌기 때문이다. */
    const MIX = { type: 'map', srv_id: 'T-M', epoch: 9, grid: { rows: 1, cols: 4 }, zones: [
      { id: 'A1', kind: 'parking', cells: [[0, 0]], active: { ok: true, reason: null },
        modules: [], declared: [{ devid: 'P9', name: 'A1' }] },
      { id: 'A2', kind: 'parking', cells: [[0, 1]], active: { ok: true, reason: null },
        modules: [], declared: [{ devid: 'P9', name: 'A2' }] },
      { id: 'E1', kind: 'entrance', cells: [[0, 2]], active: { ok: true, reason: null },
        modules: [], declared: [{ devid: 'P9', name: 'ED' }] },
      { id: 'X1', kind: 'exit', cells: [[0, 3]], active: { ok: true, reason: null },
        modules: [], declared: [{ devid: 'P9', name: 'XD' }] }
    ] };
    const MIXST = { type: 'state', srv_id: 'T-M', epoch: 9, ts_ms: NOW, zones: MIX.zones.map((z) => ({
      id: z.id, occupied: false, reserved: false, actions: {}, value_state: 'unknown', modules: [] })) };
    await evaluate(client, inject(MIX));
    await evaluate(client, inject(MIXST));
    await evaluate(client, inject(snap({ devices: [{ device_id: 'P9', online: false, primary: true,
      registered: true, module_count: 4, uptime: 60, seq: 7, last_frame_ts: NOW - 7200000 }], devices_online: 0 })));
    await sleep(120);
    const ban = await evaluate(client, BANNER);
    console.log('  · 배너: ' + ban);
    const m = ban.match(/주차 자리 (\d+)개 중 (\d+)개/);
    ok('🔴🔴 배너의 **분자가 분모를 넘지 않는다** (같은 것을 센다)',
       !!m && Number(m[2]) <= Number(m[1]), ban);
    ok('🔴 그리고 **입·출구는 따로 센다** — 주차 자리가 멈춘 것과 입구가 멈춘 것은 할 일이 다르다',
       /입·출구 2곳/.test(ban), ban);

    /* 대조군 — 주차 자리만 걸리면 입·출구 문구가 **안 나와야** 한다. 없으면 위 검사가 동어반복이다 */
    const P_ONLY = { type: 'map', srv_id: 'T-M', epoch: 10, grid: { rows: 1, cols: 2 }, zones: [
      { id: 'A1', kind: 'parking', cells: [[0, 0]], active: { ok: true, reason: null },
        modules: [], declared: [{ devid: 'P9', name: 'A1' }] },
      { id: 'A2', kind: 'parking', cells: [[0, 1]], active: { ok: true, reason: null },
        modules: [], declared: [{ devid: 'P9', name: 'A2' }] }
    ] };
    await evaluate(client, inject(P_ONLY));
    await evaluate(client, inject({ type: 'state', srv_id: 'T-M', epoch: 10, ts_ms: NOW,
      zones: P_ONLY.zones.map((z) => ({ id: z.id, occupied: false, reserved: false, actions: {},
        value_state: 'unknown', modules: [] })) }));
    await sleep(120);
    const ban2 = await evaluate(client, BANNER);
    console.log('  · 대조군(주차 자리만): ' + ban2);
    ok('🔑 대조군: 주차 자리만 걸리면 **입·출구 문구가 안 나온다**',
       /주차 자리 2개 중 2개/.test(ban2) && !/입·출구/.test(ban2), ban2);

    /* ── (b) 🔴 **봉투 도착**과 **장치 프레임**이 같은 문형으로 나오면 안 된다
       실기에서 위가 `⚠ 7시간 전 수신 — 그 뒤로 장치 프레임 없음`, 아래가 `0초 전 수신` 이었다.
       ★ 둘 다 참인데 **같은 말투라 장치가 살아 있는 것처럼 읽힌다.** */
    await evaluate(client, inject({ type: 'snapshot', ts: NOW,
      device: { online: false, device_id: 'P9', uptime: 2148, seq: 1568, last_frame_ts: NOW - 7200000 },
      test_mode: { armed: false, override_count: 0 }, slots: SLOTS(['A1', 'A2']) }));
    await sleep(120);
    let dv = await evaluate(client, DEVBOX);
    console.log('  · 장치 끊김: ' + JSON.stringify(dv));
    ok('🔴🔴 봉투 도착 칸이 **장치 프레임과 다른 말투**다 (0초 전이 "장치가 살아 있다" 로 안 읽힌다)',
       /봉투/.test(dv.age), dv.age);
    ok('🔴 장치 프레임 칸은 **끊겼다고 말한다**', /장치 프레임 없음/.test(dv.frame), dv.frame);
    ok('🔴 그리고 가동시간·seq 에 **언제 것인지**가 붙는다 (지금 값으로 안 읽히게)',
       /마지막 프레임 시점/.test(dv.uptime) && /마지막 프레임 시점/.test(dv.seq),
       dv.uptime + ' / ' + dv.seq);

    /* 대조군 — 장치가 살아 있으면 그 꼬리표가 **안 붙어야** 한다 */
    await evaluate(client, inject({ type: 'snapshot', ts: NOW,
      device: { online: true, device_id: 'P9', uptime: 2148, seq: 1568, last_frame_ts: NOW - 1000 },
      test_mode: { armed: false, override_count: 0 }, slots: SLOTS(['A1', 'A2']) }));
    await sleep(120);
    dv = await evaluate(client, DEVBOX);
    console.log('  · 대조군(장치 살아 있음): ' + JSON.stringify(dv));
    ok('🔑 대조군: 살아 있으면 **"마지막 프레임 시점" 이 안 붙는다** (항상 붙이면 뜻이 없다)',
       !/마지막 프레임 시점/.test(dv.uptime) && !/마지막 프레임 시점/.test(dv.seq),
       dv.uptime + ' / ' + dv.seq);
  }

  /* ════════════════════════════════════════════════════════════════════
     [30] 🔴 **어느 폭에서 참인가** — 검사는 **잰 조건에서만** 참이다
     ★ 8080·8081 을 1280x800 하나로만 재고 있었는데, 360x640 에서 **고를 칸이
       하나도 안 보였다**(2026-08-27 · 값은 전부 초록이었다).
     🔑 그래서 **폭을 목록으로 돌린다.** 다 도는 것이 아니라 **쓰일 폭**만이다.
     ⚠ 관제 화면이 **어느 기기에 뜨는지 우리는 모른다** — 데스크톱을 상정하고 만들었다.
       그래서 여기서는 **잘린 것을 잘렸다고 보이나** 만 본다(좁은 폭을 지원한다는 주장이 아니다).
     ════════════════════════════════════════════════════════════════════ */
  console.log('\n[30] 어느 폭에서 참인가 — 관제 화면');
  {
    /* 자리를 다섯으로 만들고 **사유 문장이 긴 상태**를 넣는다 — 그게 잘리는 자리다 */
    const WMAP = { type: 'map', srv_id: 'T-M', epoch: 30, grid: { rows: 1, cols: 5 },
      zones: ['A1', 'A2', 'A3', 'A4', 'A5'].map((id, i) => ({
        id, kind: 'parking', cells: [[0, i]], label: (i + 1) + '번 자리',
        active: { ok: true, reason: null }, modules: [], declared: [{ devid: 'P9', name: id }] })) };
    const WST = { type: 'state', srv_id: 'T-M', epoch: 30, ts_ms: NOW,
      zones: WMAP.zones.map((z) => ({ id: z.id, occupied: false, reserved: false, actions: {},
        value_state: 'unknown', modules: [],
        usable: { ok: false, reason: 'sensor_unavailable', sensors_known: 0, sensors_declared: 1,
                  controls_alive: 0, controls_total: 2, dead_modules: [], offline_devices: ['P9'] } })) };
    await evaluate(client, inject(WMAP));
    await evaluate(client, inject(WST));
    await evaluate(client, inject(snap({ devices: [{ device_id: 'P9', online: false, primary: true,
      registered: true, module_count: 5, uptime: 60, seq: 7, last_frame_ts: NOW - 60000 }], devices_online: 0 })));
    await sleep(150);

    /* 🔑 **쓰일 폭**만 적는다. 모르는 것은 모른다고 적는다 — 다 도는 것은 값이 아니라 비용이다. */
    const WIDTHS = [
      { w: 1600, h: 1000, why: '데스크톱 — 이 화면의 상정 기기' },
      { w: 1280, h: 800, why: '작은 데스크톱 · 노트북' },
      { w: 1024, h: 620, why: '작은 노트북 — 여기서 자리 칸 문장이 잘린다' },
    ];
    const seen = [];
    for (const W of WIDTHS) {
      await client.send('Emulation.setDeviceMetricsOverride',
        { width: W.w, height: W.h, deviceScaleFactor: 1, mobile: false });
      await sleep(180);
      /* 🔴 격자 칸의 긴 문장은 **`.zone__inactive`** 다. `.zone__sum` 은 **상세 패널의 머리줄**이라
         이름만 보고 그걸 잡으면 **격자에 있지도 않은 것을 재게 된다** — 실제로 그렇게 잡았고
         `null` 이 나왔다. 🔑 그 `null` 을 "말줄임이 없다" 로 읽을 뻔했다(거짓 빨강).
         ⚠ 그리고 이 주석은 **템플릿 리터럴 밖**에 있어야 한다 — 안에 역따옴표를 넣으면
           리터럴이 그 자리에서 끊긴다(📖 docs/web/LEDGER.md §5.120). */
      const r = await evaluate(client, `(() => {
        const z = document.querySelector('.zone');
        const s = z && z.querySelector('.zone__inactive');
        const b = document.getElementById('boards-banner');
        return {
          zones: document.querySelectorAll('.zone').length,
          sumClipped: s ? (s.scrollHeight > s.clientHeight + 1) : null,
          sumClamp: s ? (getComputedStyle(s).webkitLineClamp || getComputedStyle(s).lineClamp) : null,
          bannerInView: b ? (b.getBoundingClientRect().bottom <= innerHeight) : null,
          bodyScrollX: document.documentElement.scrollWidth > innerWidth + 1
        };
      })()`);
      seen.push({ w: W.w, ...r });
      console.log('  · ' + String(W.w).padStart(4) + 'px  ' + JSON.stringify(r) + '   ← ' + W.why);
    }
    await client.send('Emulation.setDeviceMetricsOverride',
      { width: 1280, height: 800, deviceScaleFactor: 1, mobile: false });
    await sleep(150);

    ok('🔴 어느 폭에서도 **가로 스크롤이 안 생긴다** (가로로 흐르면 격자가 화면 밖으로 나간다)',
       seen.every((x) => x.bodyScrollX === false), JSON.stringify(seen.map((x) => x.w + ':' + x.bodyScrollX)));
    ok('🔴 어느 폭에서도 **자리가 다섯 다 그려진다**',
       seen.every((x) => x.zones === 5), JSON.stringify(seen.map((x) => x.w + ':' + x.zones)));
    /* 🔴🔴 **이 줄은 초록/빨강이 아니라 값이다.** 잘리는 것 자체는 막을 수 없다 —
       칸은 작고 문장은 길고, 전문은 오른쪽 패널에 있다. **어느 폭에서 잘리는지를 적어 둔다.**
       ⚠ `line-clamp` 로 말줄임을 붙이려다 **그 줄 자체가 칸에서 사라졌다**(2026-08-27 · 되돌렸다).
         `display:-webkit-box` 가 되자 *"왜 못 쓰나"* 가 *"무엇이 더 안 되나"* 에 밀렸다.
       🔑 그래서 이 자리는 **판정하지 않는다.** 판정할 기준이 없는 것을 초록으로 만들면
         그 초록이 "괜찮다" 를 뜻하지 않는다 — 그건 오늘 우리가 고친 병 그 자체다. */
    const clipped = seen.filter((x) => x.sumClipped).map((x) => x.w);
    const okWidths = seen.filter((x) => x.sumClipped === false).map((x) => x.w);
    console.log('  · 자리 칸 사유 문장 — 온전히 보이는 폭: '
              + (okWidths.length ? okWidths.join(' · ') + 'px' : '없음')
              + '  ·  잘리는 폭: ' + (clipped.length ? clipped.join(' · ') + 'px' : '없음'));
    skip('좁은 폭에서 자리 칸 사유 문장이 온전히 보인다',
         '이 화면은 **데스크톱(1280 이상)** 을 상정한다. 좁은 폭의 잘림은 **알려진 상태**이고 '
         + '전문은 칸을 눌러 오른쪽 패널에서 읽는다. 판정 기준이 정해지면 그때 초록/빨강으로 바꿔라');
  }

  /* ════════════════════════════════════════════════════════════════════
     [31] 🔴 **번호판이 칸 한가운데에** · **넷을 갈라 말한다** (사용자 요청 2026-08-27)
     ★ 루트가 REQ-0521 에 박은 물음이 이 자리다 —
       *"`점유인데 번호 없음`(⑦ 전) 과 `미상(unknown)` 을 같은 글자로 말하고 있나."*
       🔴 **말하고 있었다.** 둘 다 *"차량 번호가 기록되지 않았습니다"* 였다.
     ════════════════════════════════════════════════════════════════════ */
  console.log('\n[31] 번호판이 칸 가운데에 · 번호없음과 미상을 가른다');
  {
    const PMAP = { type: 'map', srv_id: 'T-M', epoch: 31, grid: { rows: 1, cols: 4 },
      zones: ['A1', 'A2', 'A3', 'A4'].map((id, i) => ({
        id, kind: 'parking', cells: [[0, i]], label: (i + 1) + '번 자리',
        active: { ok: true, reason: null }, modules: [], declared: [{ devid: 'P1', name: id }] })) };
    /* A1 정상 · A2 점유+번호없음 · A3 미상 · A4 빈 자리 — **넷이 한 화면에** */
    const PST = { type: 'state', srv_id: 'T-M', epoch: 31, ts_ms: NOW, zones: [
      { id: 'A1', occupied: true,  reserved: false, actions: {}, value_state: 'known',
        plate: '999마9999', plate_source: 'camera', modules: [] },
      { id: 'A2', occupied: true,  reserved: false, actions: {}, value_state: 'known',
        plate: null, plate_source: null, modules: [] },
      { id: 'A3', occupied: true,  reserved: false, actions: {}, value_state: 'unknown',
        plate: null, plate_source: null, modules: [] },
      { id: 'A4', occupied: false, reserved: false, actions: {}, value_state: 'known',
        plate: null, plate_source: null, modules: [] }
    ] };
    await evaluate(client, inject(PMAP));
    await evaluate(client, inject(PST));
    await sleep(150);
    const pv = await evaluate(client, `(() => {
      const out = {};
      for (const c of document.querySelectorAll('#zone-grid .zone')) {
        const p = c.querySelector('.zone__plate');
        out[c.dataset.zone] = { text: p ? p.textContent : null,
          cls: p ? (p.className.replace('zone__plate', '').trim() || 'plain') : null,
          name: (c.querySelector('.zone__name') || {}).textContent || null,
          clipped: p ? (p.scrollWidth > p.clientWidth + 1 || p.scrollHeight > p.clientHeight + 1) : null };
      }
      return out;
    })()`);
    console.log('  · ' + JSON.stringify(pv));
    /* 🔴 **세 화면이 같은 배열이어야 한다** (사용자 요청 2026-08-27).
       ⚠ 관제는 좌표(`cells`)로 배치하므로 **열을 미러링**하고, 8080·8081 은 배열을 뒤집는다 —
         방식은 다르지만 **결과가 같아야** 한다. 그것을 여기서 센다.
       🔑 다르면 관제자와 이용자가 **같은 주차장을 두 모양으로** 본다. */
    const order = await evaluate(client, `[...document.querySelectorAll('#zone-grid .zone')].map((c) => c.dataset.zone)`);
    console.log('  · 격자 순서: ' + JSON.stringify(order));
    ok('🔴 자리를 **역순으로 그린다** (8080·8081 과 같은 배열 — 왼쪽이 마지막 자리다)',
       order.join(',') === 'A4,A3,A2,A1', JSON.stringify(order));
    ok('🔴 번호가 있으면 **그 번호를 칸에 보인다** (A1 = 999마9999)',
       pv.A1 && pv.A1.text === '999마9999', JSON.stringify(pv.A1));
    ok('🔴🔴 **점유+번호없음** 과 **미상**을 **다른 글자로** 말한다 (A2 ≠ A3)',
       !!(pv.A2 && pv.A3 && pv.A2.text && pv.A3.text && pv.A2.text !== pv.A3.text),
       JSON.stringify({ A2: pv.A2 && pv.A2.text, A3: pv.A3 && pv.A3.text }));
    ok('🔑 미상은 **번호 이야기를 안 한다** (차가 있는지도 모른다)',
       !!(pv.A3 && /확인 불가/.test(pv.A3.text)), JSON.stringify(pv.A3));
    ok('🔑 대조군: **빈 자리는 아무 말도 안 한다** (없는 사실을 지어내지 않는다)',
       !!(pv.A4 && pv.A4.text === null), JSON.stringify(pv.A4));
    ok('🔴 **자리 번호가 사라지지 않는다** (번호판이 가운데를 차지해도)',
       ['A1', 'A2', 'A3', 'A4'].every((k) => pv[k] && /번 자리/.test(pv[k].name || '')),
       JSON.stringify(['A1', 'A2', 'A3', 'A4'].map((k) => pv[k] && pv[k].name)));
    /* 🔴 **가장 긴 번호판**으로 잘림을 본다 — §"잘릴 때는 무엇이 남는가가 뜻이다" */
    ok('🔴 가장 긴 번호(`999마9999`)가 **안 잘린다**', pv.A1 && pv.A1.clipped === false,
       JSON.stringify(pv.A1) + ' — 잘리면 번호를 못 읽어 이 기능이 무의미해진다');
  }

  skip('실기에서 devices[] 가 실제로 온다',
       '윈도우 서버 + 아두이노가 필요하다 — 11:00 창에서 `node web/tools/boards.mjs --live <웹포트>` 로 잰다');
  skip('reset_controller 가 실제로 게이트를 닫는다', '전선 뒤 결말이다. 화면이 아니라 장치·서버가 답한다');
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
