/**
 * 🔴 개정 봉투 둘(`map`/`state`)과 **판 번호(`epoch`) 계약**을 측정한다. REQ-0226.
 *
 * socket 명세 §6.8: *"`epoch` 비교는 계약이다. 화면이 비교 안 하면 이 장치는 없는 것과 같다."*
 * → **그 비교가 실제로 도는지 재는 것이 이 도구의 목적이다.**
 *
 * ⚠ **주입이다.** 서버는 아직 `map`/`state` 를 안 보낸다. 전송 계층만 건너뛰고
 *   `handleServerMessage → applyMap/applyZoneState → render` 는 진짜 경로다.
 *   서버·포트·트래픽 없다(`file://` + `?demo=1`).
 *
 * 사용: node web/tools/map-epoch.mjs            (--head 로 창을 본다)
 */
import { launch, evaluate, sleep } from './cdp.mjs';

const HEAD = process.argv.includes('--head');
/* 🔴 `--file <경로>` — 기본은 **지금 쓰는 화면**(서머리)이다.
   동결 트리(§3-D)에 돌리면 **동결된 사실만 확인**하게 되므로 기본으로는 안 간다.
   굳이 옛 화면을 보려면 경로를 명시해라. */
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
const TARGET = fileArg
  ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg))
  : new URL('../../서머리/server/index.html', import.meta.url);
const URL_ = TARGET.href + '?demo=1';

let pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; console.log('  ✅ ' + name); }
  else { fail++; console.log('  ❌ ' + name + (detail ? '  → ' + detail : '')); }
}
const inject = (frame) => `(() => { handleServerMessage(${JSON.stringify(frame)}); return true; })()`;
const peek = `(() => ({
  hasMap: !!state.map, epoch: state.map ? state.map.epoch : null,
  rows: state.map ? state.map.rows : null, cols: state.map ? state.map.cols : null,
  stale: state.mapStale, usable: mapUsable(),
  hasZoneState: !!state.zoneState,
  sent: (window.__sent || []).map(p => p.type)
}))()`;

const MAP7 = { type: 'map', srv_id: 'A-1', epoch: 7, grid: { rows: 5, cols: 5 },
  zones: [{ id: 'A1', kind: 'parking', cells: [[0, 0]],
            modules: [{ devid: 'P1', name: 'sensor', kind: 'IP', idx: 0 }] }] };
const ST = (ep) => ({ type: 'state', srv_id: 'A-1', epoch: ep, ts_ms: 1755500000123,
  zones: [{ id: 'A1', occupied: true, reserved: false, actionable: true, blocked_reason: null,
            completion: 'complete', modules: [{ devid: 'P1', name: 'sensor', value: 1, known: true }] }] });

/* 🔴 §5.18 — 실패 집계는 catch 에서도 올린다. */
let client = null;
try {
  console.log('\n대상: ' + URL_ + '\n(서버 미사용 · 트래픽 0 · 주입)\n');
  client = await launch({ headless: !HEAD });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Page.navigate', { url: URL_ });

  let ready = false;
  for (let i = 0; i < 100; i++) {
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.querySelector('.tile')`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  ok('화면이 떴다', ready === true);
  if (ready !== true) throw new Error('화면 준비 실패');

  /* `get_map` 이 실제로 나가는지 보려면 전송을 가로채야 한다. 전송 계층만 감싼다. */
  await evaluate(client, `(() => {
    window.__sent = [];
    const orig = transport.send.bind(transport);
    transport.send = function (p) { window.__sent.push(p); return orig(p); };
    return true;
  })()`);

  /* [1] map 없이 state 가 먼저 오면 — 그리지 않고 재청한다 */
  console.log('[1] map 을 한 번도 못 받은 채 state 가 온다');
  await evaluate(client, inject(ST(7)));
  await sleep(150);
  let v = await evaluate(client, peek);
  console.log('  · ' + JSON.stringify(v));
  ok('그리지 않는다(zoneState 를 안 담는다)', v.hasZoneState === false);
  ok('낡음으로 표시한다', v.stale === true);
  ok('get_map 을 보냈다', v.sent.filter(t => t === 'get_map').length === 1, JSON.stringify(v.sent));

  /* [2] 🔴 무한 재요청 차단 — 같은 창에서 두 번째는 안 보낸다 */
  console.log('[2] 곧바로 또 불일치가 와도 재요청을 반복하지 않는다');
  await evaluate(client, inject(ST(9)));
  await sleep(150);
  v = await evaluate(client, peek);
  ok('get_map 이 여전히 1회다(최소 간격 차단)', v.sent.filter(t => t === 'get_map').length === 1,
     JSON.stringify(v.sent) + ' — 반복되면 epoch 오구현이 서버를 때린다');

  /* [3] map 을 받으면 그릴 수 있게 된다 */
  console.log('[3] map 수신');
  await evaluate(client, inject(MAP7));
  await sleep(150);
  v = await evaluate(client, peek);
  console.log('  · ' + JSON.stringify(v));
  ok('지형을 담았다 (epoch 7 · 5x5)', v.hasMap && v.epoch === 7 && v.rows === 5 && v.cols === 5, JSON.stringify(v));
  ok('낡음이 풀렸다', v.stale === false);
  ok('그릴 수 있다', v.usable === true);

  /* [4] epoch 이 맞으면 상태를 담는다 */
  console.log('[4] epoch 일치 state');
  await evaluate(client, inject(ST(7)));
  await sleep(150);
  v = await evaluate(client, peek);
  ok('상태를 담았다', v.hasZoneState === true);
  ok('여전히 그릴 수 있다', v.usable === true);

  /* [5] 🔴 계약의 본체 — epoch 이 다르면 그리지 않는다 */
  console.log('[5] 🔴 epoch 불일치 state (지형이 바뀐 뒤 옛 사본으로 그리면 안 된다)');
  await sleep(2100);                                  // 재청 최소 간격을 넘긴다
  await evaluate(client, inject(ST(8)));
  await sleep(150);
  v = await evaluate(client, peek);
  console.log('  · ' + JSON.stringify(v));
  ok('담아 둔 상태를 버린다', v.hasZoneState === false, '남겨 두면 옛 사본으로 그리게 된다');
  ok('낡음으로 표시한다', v.stale === true);
  ok('그릴 수 없다고 판정한다', v.usable === false);
  ok('get_map 을 다시 보냈다(2회)', v.sent.filter(t => t === 'get_map').length === 2, JSON.stringify(v.sent));

  /* [6] 🔴 짐작하지 않는다 — grid 가 빠진 map 은 받지 않는다 */
  console.log('[6] 🔴 grid.rows 가 빠진 map — 5x5 로 가정하면 안 된다');
  const before = await evaluate(client, peek);
  await evaluate(client, inject({ type: 'map', epoch: 11, grid: { cols: 5 }, zones: [] }));
  await sleep(150);
  v = await evaluate(client, peek);
  ok('빠진 격자 크기를 기본값으로 메우지 않는다', v.epoch === before.epoch,
     JSON.stringify([before.epoch, v.epoch]) + ' — epoch 이 11 로 바뀌었으면 짐작한 것이다');
  /* ══ [7] 격자 렌더링 — 빈 칸 / 미상 자리 / 정상 자리 셋이 갈리는가 ═════════ */
  /* 🔴 **이 사례를 고치기 전에 읽어라.** 아래 첫 단언(*"새 격자가 보이고 옛 격자가 숨는다"*)이
     `e2e.mjs`·`queued-live.mjs`·`queue-contract.mjs` 의 격자 가드가 **발화하는 조건과 같다.**
     그 셋은 자기 힘을 여기서 빌리고 있으므로, **이 단언을 없애면 가드 검증이 조용히 사라진다.**
     (중복 측정을 안 만든 대가다 — 없애려면 그쪽에 대체 측정을 먼저 넣어라.) */
  console.log('\n[7] 격자 렌더링 (3x3 · 자리 둘 · 하나는 상태 없음)');
  const MAP3 = { type: 'map', epoch: 20, grid: { rows: 3, cols: 3 },
    zones: [
      { id: 'A1', kind: 'parking', cells: [[0, 0]],
        modules: [{ devid: 'P1', name: 'sensor', kind: 'IP', idx: 0 },
                  { devid: 'P2', name: 'sign', kind: 'OG', idx: 1 }] },
      { id: 'E1', kind: 'entrance', cells: [[2, 2]],
        modules: [{ devid: 'P3', name: 'gate', kind: 'OB', idx: 0 }] }
    ] };
  const ST20 = { type: 'state', epoch: 20, ts_ms: 1,
    zones: [{ id: 'A1', occupied: false, reserved: false, completion: 'complete',
              actions: { reserve: { ok: true, reason: null },
                         cancel: { ok: false, reason: 'node_offline' },
                         set_sign: { ok: true, reason: null } },   // ← 닫힌 집합 밖: 무시돼야 한다
              modules: [{ devid: 'P1', name: 'sensor', value: 0, known: true },
                        { devid: 'P2', name: 'sign', value: 1, known: false }] }] };
  await evaluate(client, inject(MAP3));
  await evaluate(client, inject(ST20));
  await sleep(250);
  /* 🔴 모듈 행과 조작 버튼은 **우측 패널**에 있다 (REQ-0288: 칸은 요약만).
     그래서 자리마다 칸을 눌러 패널을 그린 뒤 읽는다 — 격자에서 찾으면 영원히 `[]` 다. */
  const dom = await evaluate(client, `(() => {
    const g = document.getElementById('zone-grid');
    const panel = document.getElementById('zone-detail');
    if (!panel) throw new Error('#zone-detail 이 없다 - REQ-0288 이전 판이다');
    const ids = [...g.querySelectorAll('.zone')].map(z => z.dataset.zone);
    const zones = [];
    for (const id of ids) {
      const z = [...g.querySelectorAll('.zone')].find(e => e.dataset.zone === id);
      if (!z) continue;
      z.click();
      zones.push({
        id: z.dataset.zone, kind: z.dataset.kind, usable: z.dataset.usable,
        /* 🔴 REQ-0297: 상태 요약은 패널로 갔다(칸에는 색·테두리가 그 말을 한다).
           칸에서 읽으면 null 이라 textContent 를 바로 붙이면 여기서 TypeError 로 죽는다.
           ⚠ 이 주석은 템플릿 리터럴 안이다 — 역따옴표를 쓰면 문자열이 그 자리에서 끊긴다(오늘 세 번째다). */
        sum: (panel.querySelector('.zone__sum') || {}).textContent || null,
        badge: (z.querySelector('.zone__badge') || {}).textContent || null,
        mods: [...panel.querySelectorAll('.zmod')].map(m => m.textContent + '|loud=' + m.dataset.loud),
        acts: [...panel.querySelectorAll('.zbtn')].map(b => b.dataset.act + ':' + b.getAttribute('aria-disabled')),
      });
    }
    return { hidden: g.hidden, oldHidden: document.getElementById('grid').hidden,
             cols: g.style.getPropertyValue('--zcols'),
             cells: g.children.length,
             empties: g.querySelectorAll('.zcell--empty').length,
             emptyHidden: [...g.querySelectorAll('.zcell--empty')].every(e => e.getAttribute('aria-hidden') === 'true'),
             focusable: g.querySelectorAll('.zcell--empty button, .zcell--empty [tabindex]').length,
             zones };
  })()`);
  console.log('  · ' + JSON.stringify(dom, null, 0));

  ok('새 격자가 보이고 옛 격자가 숨는다', dom.hidden === false && dom.oldHidden === true);
  ok('격자 크기를 맵에서 받는다 (cols=3)', dom.cols === '3', dom.cols);
  ok('칸 수가 rows×cols 다 (9)', dom.cells === 9, String(dom.cells));
  /* 🔴 3×3 에 자리 둘 → 빈 칸 7. 자리 수가 아니라 **칸 수 − zone 칸 수**다(파생값). */
  ok('빈 칸이 7개다', dom.empties === 7, String(dom.empties));
  ok('빈 칸은 보조기술에서 숨는다', dom.emptyHidden === true);
  ok('🔴 빈 칸에 초점받을 것이 없다', dom.focusable === 0,
     '있으면 3x3 에서 7번, 5x5 에서 18번 탭해야 자리에 닿는다');

  const a1 = dom.zones.find(z => z.id === 'A1'), e1 = dom.zones.find(z => z.id === 'E1');
  ok('상태가 온 자리는 정상으로 그린다', a1 && a1.usable === '1');
  ok('🔴 상태가 안 온 자리는 미상으로 그린다(빈 칸과 다르다)', e1 && e1.usable === '0' && /미상/.test(e1.sum),
     JSON.stringify(e1));
  ok('parking 요약이 예약 어휘다', a1 && a1.sum === '빈 자리', a1 && a1.sum);
  ok('entrance 요약에 예약 어휘가 없다', e1 && !/예약/.test(e1.sum), e1 && e1.sum);

  ok('모듈 행이 다 보인다 (표시량 A · 2개)', a1 && a1.mods.length === 2, JSON.stringify(a1 && a1.mods));
  /* ✅ 확정 코드(IP·OG·OB)를 한글 표시 이름으로 **화면이 한 번만** 사상한다(서버는 번역 안 한다). */
  ok('kind 를 한글 표시 이름으로 사상한다', !!(a1 && a1.mods.some(t => /주차확인센서/.test(t))), JSON.stringify(a1.mods));
  /* 🔴 출력 모듈 판정이 **표가 아니라 `kind[0]==='O'` 규칙**이다 → OG 에 `*` 가 붙는다. */
  /* ⚠ 검사의 뜻은 *"출력 모듈 행에 별표가 있다"* 이므로 **그 행 안**에서 찾는다(행 구분자는 `|`).
     🔴 **라벨 이름으로 행을 고르지 마라.** 예전에는 `안내등[^|]*\*` 로 찾았는데,
        화면이 `OG` 를 무조건 "안내등" 이라 부르지 않게 되면서(같은 kind 에 LCD·안내등·차단봉이
        섞여 있어 `widget` 이 가른다) **widget 이 없는 봉투에서는 중립어 "조작"** 이 된다.
        그러면 이 검사가 **화면 개선 때문에 빨강**이 된다 — 재는 것과 무관한 이름에 묶여 있었다.
     ✅ 전선 이름(`(sign)`)으로 그 행을 고른다. 표시 이름이 무엇으로 바뀌어도 안 깨진다. */
  ok('출력 모듈(OG)에 * 가 붙는다', !!(a1 && a1.mods.some(t => /\(sign\)[^|]*\*/.test(t))), JSON.stringify(a1.mods));
  ok('🔴 관측 모듈(IP)에는 * 가 없다', !!(a1 && a1.mods.some(t => /주차확인센서[^*]/.test(t))), JSON.stringify(a1.mods));
  /* ⚠ 같은 이유로 라벨과 값 기호 사이에 `#0 (sensor)` 가 들어온다. 공백만 지워서는 안 붙는다. */
  ok('아는 값은 채움/빔 기호로', !!(a1 && a1.mods.some(t => /주차확인센서[^|]*○/.test(t))), JSON.stringify(a1.mods));
  ok('모르는 값은 ⏱ 로', !!(a1 && a1.mods.some(t => /⏱/.test(t))), JSON.stringify(a1.mods));
  /* 🔑 강조축: 이 자리의 `cancel` 이 `node_offline`(미상 계열)로 막혔으므로 미상 모듈이 **진하다.** */
  ok('막는 미상은 진하다(강조축이 파생으로 나온다)', !!(a1 && a1.mods.some(t => /⏱.*loud=1/.test(t))), JSON.stringify(a1.mods));

  ok('닫힌 집합 밖 조작(set_sign)은 안 그린다', !!(a1 && !a1.acts.some(t => /set_sign/.test(t))), JSON.stringify(a1.acts));
  ok('ok:true 는 누를 수 있다', !!(a1 && a1.acts.includes('reserve:false')), JSON.stringify(a1.acts));
  /* 🔑 `disabled` 가 아니라 `aria-disabled` 다 — 이유를 들을 수 있어야 한다(§3.2). */
  ok('ok:false 는 aria-disabled 로 막는다', !!(a1 && a1.acts.includes('cancel:true')), JSON.stringify(a1.acts));
  ok('🔴 actions 에 없는 조작은 버튼이 없다(E1 은 상태가 없다)', !!(e1 && e1.acts.length === 0), JSON.stringify(e1.acts));

  /* ══ [8] 미상 집계 배너 — **정상에서 침묵하는가**가 이 항목의 본체다 ═════════ */
  console.log('\n[8] 미상 집계 배너');
  const bnr = `(() => { const b = document.getElementById('slots-banner');
                        return { hidden: b.hidden, text: b.textContent }; })()`;
  let b = await evaluate(client, bnr);
  console.log('  · 지금 → ' + JSON.stringify(b));
  /* 위 [7] 상태에서 E1 은 상태가 안 왔고 A1 은 cancel 이 node_offline(미상 계열)로 막혔다 → 둘 다 센다. */
  ok('둘을 센다(상태 없음 + 미상으로 막힘)', b.hidden === false && /2자리/.test(b.text), JSON.stringify(b));
  ok('원인을 갈라 말한다', /상태가 아직 오지 않았습니다/.test(b.text) && /장치의 상태를 모릅니다/.test(b.text), b.text);
  ok('🔴 빈 칸(7개)은 세지 않는다', !/9자리|7자리/.test(b.text), b.text);

  /* 🔴 정상에서 침묵하는가 — 이게 §3.1 이 배너를 만든 이유이자 이 규칙의 시험이다. */
  console.log('  · 모두 정상인 상태를 넣는다');
  await evaluate(client, inject({ type: 'state', epoch: 20, ts_ms: 2, zones: [
    { id: 'A1', occupied: false, reserved: false, completion: 'complete',
      actions: { reserve: { ok: true, reason: null } },
      modules: [{ devid: 'P1', name: 'sensor', value: 0, known: true },
                { devid: 'P2', name: 'sign', value: 1, known: true }] },
    { id: 'E1', kind: 'entrance', completion: 'complete',
      actions: { open_gate: { ok: true, reason: null } },
      modules: [{ devid: 'P3', name: 'gate', value: 0, known: true }] } ] }));
  await sleep(200);
  b = await evaluate(client, bnr);
  console.log('  · 정상 → ' + JSON.stringify(b));
  ok('🔴 정상에서 침묵한다', b.hidden === true, JSON.stringify(b) + ' — 상시 켜진 경고는 아무도 안 읽는다');

  /* `busy` 는 세지 않는다 — 곧 풀리는 정상 상태다. */
  console.log('  · busy 계열(pending)로 막힌 상태를 넣는다');
  await evaluate(client, inject({ type: 'state', epoch: 20, ts_ms: 3, zones: [
    { id: 'A1', occupied: false, reserved: false, completion: 'complete',
      actions: { reserve: { ok: false, reason: 'pending' } },
      modules: [{ devid: 'P1', name: 'sensor', value: 0, known: true },
                { devid: 'P2', name: 'sign', value: 1, known: true }] },
    { id: 'E1', kind: 'entrance', completion: 'complete',
      actions: { open_gate: { ok: true, reason: null } },
      modules: [{ devid: 'P3', name: 'gate', value: 0, known: true }] } ] }));
  await sleep(200);
  b = await evaluate(client, bnr);
  ok('🔴 busy 는 세지 않는다(곧 풀린다)', b.hidden === true, JSON.stringify(b));

  /* ══ [9] 조작 버튼 — 실제로 요청이 나가는가 / 못 보내는 것을 말하는가 ═════════ */
  console.log('\n[9] 조작 버튼 (칸=컨테이너 · 조작=버튼)');
  await evaluate(client, inject({ type: 'state', epoch: 20, ts_ms: 4, zones: [
    { id: 'A1', occupied: false, reserved: false, completion: 'complete',
      actions: { reserve: { ok: true, reason: null } },
      modules: [{ devid: 'P1', name: 'sensor', value: 0, known: true },
                { devid: 'P2', name: 'sign', value: 1, known: true }] },
    { id: 'E1', kind: 'entrance', completion: 'complete',
      /* 🔴 서버는 가능하다고 하지만 **요청 형식이 계약에 없다** → 화면이 못 보낸다 */
      actions: { open_gate: { ok: true, reason: null } },
      modules: [{ devid: 'P3', name: 'gate', value: 0, known: true }] } ] }));
  await sleep(200);
  /* 🔴 조작 버튼은 **선택한 자리 하나**의 것만 존재한다 (REQ-0288: 칸은 요약만 · 조작은 우측 패널).
     예전에는 모든 칸의 버튼이 격자에 다 있어서 한 번에 걷을 수 있었다. 지금은 **자리마다 눌러서** 읽는다.
     🔑 이것이 사용자가 실제로 밟는 순서다 - 칸을 눌러 상세를 띄우고 거기서 조작한다. */
  const btnsOf = async (zid) => await evaluate(client,
    '(() => {'
    + '  const want = ' + JSON.stringify(zid) + ';'
    + '  const cell = [...document.querySelectorAll("#zone-grid .zone")].find(z => z.dataset.zone === want);'
    + '  if (!cell) return [];'
    + '  cell.click();'                          /* 이 자리를 패널에 띄운다 */
    + '  return [...document.querySelectorAll("#zone-detail .zbtn")].map(b => ({'
    + '    zone: b.dataset.zone, act: b.dataset.act, dis: b.getAttribute("aria-disabled"), t: b.title }));'
    + '})()');
  const btns = [...(await btnsOf('A1')), ...(await btnsOf('E1'))];
  console.log('  · ' + JSON.stringify(btns));
  const rv = btns.find(x => x.act === 'reserve'), og = btns.find(x => x.act === 'open_gate');
  ok('보낼 수 있는 조작은 누를 수 있다', !!(rv && rv.dis === 'false'), JSON.stringify(rv));
  /* 🔴 **기대가 뒤집혔다** — 사용자 확정 2026-08-20: *"특정 모듈 gate 같은 기능은 제거하라. control 로 통일한다."*
     ~~"차단봉 조작도 누를 수 있다(형식 확정)"~~ 는 그 결정 **전** 의 기대다. 지금은 **안 그리는 것이 정답**이다.
     근거는 화면 코드의 ACTION_LABEL 주석에 있다: 서버는 닫기=0, 장치는 닫기=2 로 갈렸다.
     🔑 옛 기대를 지우지 않고 뒤집힌 사실을 적는다 — 지우면 다음 사람이 "왜 안 그리나"를 다시 쫓는다. */
  ok('🔴 차단봉 조작은 아예 그려지지 않는다 (control 로 통일)', !og, JSON.stringify(og));

  /* ══ 🔴 예약이 **전선까지 나가는가** — REQ-0290 이 이 자리에서 났다 ══════════════
     사용자 보고: *"예약해도 테두리 등 변경사항 없다."* 서버 로그에 예약 요청 **0건**.
     기전: 확인 대화상자 뒤 재검증이 **옛 격자의 `snapshot.slots`** 를 보고 있었다.
           이 하니스는 `map`/`state` 만 주입한다 — 즉 **`snapshot` 이 없는 실기와 같은 세계**다.
           그래서 이 검사가 있었다면 그날 잡혔다. 지금 넣는다.
     🔑 여는 판정자(서버 `actions`)와 보내는 판정자가 같아야 한다(원장 §5.52 의 그 형태). */
  const clickReserve = `(() => {
    const cell = [...document.querySelectorAll('#zone-grid .zone')].find(z => z.dataset.zone === 'A1');
    if (!cell) throw new Error('A1 칸이 없다');
    cell.click();
    const b = document.querySelector('#zone-detail .zbtn[data-act="reserve"]');
    if (!b) throw new Error('A1 을 선택했는데 패널에 예약 버튼이 없다');
    b.click();
    return true;
  })()`;
  const waitDlg = async () => {
    for (let i = 0; i < 40; i++) {
      if (await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false) === true) return true;
      await sleep(50);
    }
    return false;
  };
  await evaluate(client, `(() => { window.__sent = []; return true; })()`);
  await evaluate(client, clickReserve);
  ok('예약이 확인을 묻는다', (await waitDlg()) === true, '확인 대화상자가 안 뜨면 옛 경로를 안 쓰는 것이다');
  /* 🔑 **취소를 먼저 밟는다** — 예약이 성공하면 낙관적 pending 이 붙어 버튼이 바뀌고,
     그 뒤에 취소 검사를 하면 무엇을 재는지 흐려진다. 순서가 검사의 일부다. */
  await evaluate(client, `document.getElementById('confirm-dialog').close('')`);
  await sleep(140);
  const sentCancel = await evaluate(client, `(window.__sent || []).map(p => p.type)`);
  ok('🔴 확인을 취소로 닫으면 아무것도 안 보낸다', !sentCancel.includes('reserve'), JSON.stringify(sentCancel));

  await evaluate(client, clickReserve);
  await waitDlg();
  await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);
  await sleep(220);
  const rsent = await evaluate(client, `(window.__sent || []).slice(-1)[0] || null`);
  console.log('  · 나간 예약 요청 → ' + JSON.stringify(rsent));
  ok('🔴 확인을 누르면 예약이 **실제로 전선에 나간다** (snapshot 없는 세계에서)',
     !!(rsent && rsent.type === 'reserve'),
     '나간 것: ' + JSON.stringify(rsent) + ' — null 이면 확인 뒤 재검증이 또 옛 목록을 보고 있다(REQ-0290)');
  ok('🔴 대상이 자리 하나다 (slot=A1) · 모듈을 지목하지 않는다',
     !!(rsent && rsent.slot === 'A1' && !rsent.devid && !rsent.name), JSON.stringify(rsent));
  ok('rid 를 실어 보낸다', !!(rsent && typeof rsent.rid === 'string' && rsent.rid.length > 0));
  /* 🔴 그리고 **일반 영역(area)** 을 눌렀을 때 조용하지 않은가 — 눌러도 안 되는 것이면 그렇게 보여야 한다. */
  const areaTxt = await evaluate(client, `(() => {
    const cell = [...document.querySelectorAll('#zone-grid .zone')].find(z => z.dataset.kind !== 'parking');
    if (!cell) return null;
    cell.click();
    const p = document.querySelector('#zone-detail .zpanel__empty');
    return { zone: cell.dataset.zone, kind: cell.dataset.kind, text: p ? p.textContent : null,
             btns: document.querySelectorAll('#zone-detail .zbtn').length };
  })()`);
  console.log('  · 일반 영역 → ' + JSON.stringify(areaTxt));
  if (!areaTxt) skip('일반 영역을 누르면 왜 조작이 없는지 말한다', '이 지형에 parking 아닌 자리가 없다');
  else ok('🔴 일반 영역을 누르면 **왜 조작이 없는지** 말한다 (조용하지 않다)',
          areaTxt.btns === 0 && !!areaTxt.text && /예약은 주차 자리|가능한 조작이 없습니다/.test(areaTxt.text),
          JSON.stringify(areaTxt));

  /* 🔴 ~~여기 있던 '예약 버튼이 옛 확인 대화상자를 그대로 쓴다' + '취소로 닫으면 안 보낸다'~~ 는
     바로 위 블록으로 **합쳤다**(REQ-0290). 두 벌로 두면 앞 블록이 만든 낙관적 pending 위에서
     뒤 블록이 돌아 **무엇을 재는지 흐려진다** — 실제로 그래서 빨강이 났다. */

  /* ══ [10] 🔴 `epoch` 단조성 — 늦게 온 옛 프레임은 화면을 흔들지 않는다 ═════════ */
  console.log('\n[10] 늦게 도착한 옛 판의 state (socket: epoch 은 단조 증가)');
  /* 지금 판은 20 이고 정상 상태가 담겨 있다. 옛 판(19) 한 장이 늦게 도착하는 상황을 만든다. */
  const before10 = await evaluate(client, peek);
  const askBefore = before10.sent.filter(t => t === 'get_map').length;
  await evaluate(client, inject({ type: 'state', epoch: 19, ts_ms: 9, zones: [] }));
  await sleep(200);
  const after10 = await evaluate(client, peek);
  console.log('  · ' + JSON.stringify({ before: before10.hasZoneState, after: after10.hasZoneState,
                                        stale: after10.stale, ask: after10.sent.filter(t => t === 'get_map').length }));
  ok('🔴 담아 둔 상태를 안 버린다(화면이 깜빡 비지 않는다)', after10.hasZoneState === before10.hasZoneState,
     '버리면 옛 프레임 한 장 때문에 격자가 빈다');
  ok('🔴 낡음으로 표시하지 않는다(내 지형은 멀쩡하다)', after10.stale === false);
  ok('🔴 맵을 다시 청하지 않는다(이미 새것을 갖고 있다)',
     after10.sent.filter(t => t === 'get_map').length === askBefore,
     '청하면 늦은 프레임마다 서버를 때린다');
  ok('여전히 그릴 수 있다', after10.usable === true);

  /* ══ [11] 🔴🔴 `srv_id` — 재기동으로 판이 되돌아가도 얼지 않는가 (실물 시험 대응) ═══ */
  console.log('\n[11] 서버 재기동 — srv_id 가 바뀌고 epoch 이 1로 되돌아간다');
  /* 지금: srv_id A-1 · 판 20. 재기동을 흉내낸다 — 새 id, 판 1. */
  await evaluate(client, `(() => { window.__sent = []; return true; })()`);
  await evaluate(client, inject({ type: 'state', srv_id: 'B-2', epoch: 1, ts_ms: 50, zones: [] }));
  await sleep(200);
  let v11 = await evaluate(client, peek);
  console.log('  · 새 서버의 state 먼저 → ' + JSON.stringify(v11));
  /* 🔴 srv_id 비교가 없으면 판 1 < 20 이라 "옛 프레임"으로 무시되고 화면이 영영 안 갱신된다. */
  ok('🔴 새 서버의 낮은 판을 옛 프레임으로 취급하지 않는다', v11.stale === true,
     JSON.stringify(v11) + ' — stale 이 아니면 판 비교가 세계를 안 보고 있다');
  /* 🔴 **간격 제한을 통과해야 한다** — 세계 변화는 고리가 아니라 한 번 있는 사건이다.
     하니스가 처음에 이걸 잡았다: 제한에 막혀 재청이 안 나가 최대 2초간 화면이 낡은 채로 남았다. */
  ok('🔴 세계가 바뀌면 간격 제한을 통과해 즉시 청한다',
     v11.sent.filter(t => t === 'get_map').length === 1, JSON.stringify(v11.sent));

  await evaluate(client, inject({ type: 'map', srv_id: 'B-2', epoch: 1, grid: { rows: 2, cols: 2 },
    zones: [{ id: 'A1', kind: 'parking', cells: [[0, 0]], modules: [] }] }));
  await sleep(200);
  v11 = await evaluate(client, peek);
  console.log('  · 새 서버의 map → ' + JSON.stringify(v11));
  ok('🔑 새 서버의 맵을 판 비교 없이 받아들인다 (epoch 1 < 20 인데도)',
     v11.hasMap === true && v11.epoch === 1 && v11.stale === false, JSON.stringify(v11));
  ok('그릴 수 있게 된다', v11.usable === true);

  await evaluate(client, inject({ type: 'state', srv_id: 'B-2', epoch: 1, ts_ms: 51, zones: [
    { id: 'A1', occupied: false, reserved: false, completion: 'unknown',
      actions: { reserve: { ok: false, reason: 'wibble_unknown_code' } }, modules: [] } ] }));
  await sleep(200);
  v11 = await evaluate(client, peek);
  ok('새 세계의 state 를 담는다', v11.hasZoneState === true, JSON.stringify(v11));
  /* 🔴 서버가 **정말 모르는 코드**를 보냈을 때 — 막고, 코드를 그대로 보여야 한다.
     ⚠ 예전엔 이 자리에 `not_supported` 를 넣었는데 **그건 이제 아는 코드다**(SPEC-web-control §5 에 들어왔고
     화면이 한국어 문구를 갖고 있다). 아는 코드로 '모르는 코드'를 재면 검사가 그 순간 뜻을 잃는다. */
  const nb = await evaluate(client, `(() => {
    const cell = document.querySelector('#zone-grid .zone');
    if (cell) cell.click();
    const b = document.querySelector('#zone-detail .zbtn[data-act="reserve"]');
    return b ? { dis: b.getAttribute('aria-disabled'), t: b.title } : null; })()`);
  console.log('  · 모르는 코드(not_supported) → ' + JSON.stringify(nb));
  ok('🔴 모르는 거절 코드도 막고, 코드를 그대로 보여 준다',
     !!(nb && nb.dis === 'true' && /wibble_unknown_code/.test(nb.t)), JSON.stringify(nb));

} catch (e) {
  fail++;
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 결과가 아니다 */ } }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
