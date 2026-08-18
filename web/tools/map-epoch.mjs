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
const URL_ = new URL('../../조별과제샘플/index.html', import.meta.url).href + '?demo=1';

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

const MAP7 = { type: 'map', epoch: 7, grid: { rows: 5, cols: 5 },
  zones: [{ id: 'A1', kind: 'parking', cells: [[0, 0]],
            modules: [{ devid: 'P1', name: 'sensor', kind: 'IP', idx: 0 }] }] };
const ST = (ep) => ({ type: 'state', epoch: ep, ts_ms: 1755500000123,
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
  const dom = await evaluate(client, `(() => {
    const g = document.getElementById('zone-grid');
    const zones = [...g.querySelectorAll('.zone')].map(z => ({
      id: z.dataset.zone, kind: z.dataset.kind, usable: z.dataset.usable,
      sum: z.querySelector('.zone__sum').textContent,
      mods: [...z.querySelectorAll('.zmod')].map(m => m.textContent + '|loud=' + m.dataset.loud),
      acts: [...z.querySelectorAll('.zbtn')].map(b => b.dataset.act + ':' + b.getAttribute('aria-disabled'))
    }));
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
  ok('출력 모듈(OG)에 * 가 붙는다', !!(a1 && a1.mods.some(t => /안내등\*/.test(t))), JSON.stringify(a1.mods));
  ok('🔴 관측 모듈(IP)에는 * 가 없다', !!(a1 && a1.mods.some(t => /주차확인센서[^*]/.test(t))), JSON.stringify(a1.mods));
  ok('아는 값은 채움/빔 기호로', !!(a1 && a1.mods.some(t => /주차확인센서○/.test(t.replace(/\s/g, '')))), JSON.stringify(a1.mods));
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
  const btns = await evaluate(client, `[...document.querySelectorAll('#zone-grid .zbtn')].map(b => ({
    zone: b.dataset.zone, act: b.dataset.act, dis: b.getAttribute('aria-disabled'), t: b.title }))`);
  console.log('  · ' + JSON.stringify(btns));
  const rv = btns.find(x => x.act === 'reserve'), og = btns.find(x => x.act === 'open_gate');
  ok('보낼 수 있는 조작은 누를 수 있다', !!(rv && rv.dis === 'false'), JSON.stringify(rv));
  /* 🔴 감추지 않는다. 보여 주고 **왜 못 누르는지 말한다.** */
  ok('🔴 요청 형식이 없는 조작은 보이지만 막힌다', !!(og && og.dis === 'true'), JSON.stringify(og));
  ok('그 이유가 "화면이 보낼 수 없다"로 적힌다', !!(og && /형식이 아직 계약에 없습니다/.test(og.t)), JSON.stringify(og));

  /* 예약 버튼이 **옛 경로를 그대로 다시 쓰는지** — 확인 대화상자가 뜨면 그 증거다. */
  await evaluate(client, `document.querySelector('#zone-grid .zbtn[data-act="reserve"]').click()`);
  let dlg = false;
  for (let i = 0; i < 40; i++) {
    dlg = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false);
    if (dlg === true) break;
    await sleep(50);
  }
  ok('🔑 예약 버튼이 옛 확인 대화상자를 그대로 쓴다', dlg === true,
     '안 뜨면 낙관적 UI·롤백·queued 타이머를 다시 만들어야 한다는 뜻이다');
  await evaluate(client, `document.getElementById('confirm-dialog').close('')`);   // 취소로 닫는다
  await sleep(120);
  const sent2 = await evaluate(client, `(window.__sent || []).map(p => p.type)`);
  ok('취소로 닫으면 아무것도 안 보낸다', !sent2.includes('reserve'), JSON.stringify(sent2));

} catch (e) {
  fail++;
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 결과가 아니다 */ } }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
