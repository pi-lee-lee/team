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
            modules: [{ devid: 'P1', name: 'sensor', kind: 'parking_sensor', idx: 0 }] }] };
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
} catch (e) {
  fail++;
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 결과가 아니다 */ } }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
