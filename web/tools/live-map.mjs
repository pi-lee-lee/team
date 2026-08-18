/**
 * 🔴 **실기 대조** — 서버가 실제로 보내는 `map`/`state` 를 내 가정과 맞춘다. (2026-08-19)
 *
 * 지금까지 개정 화면 검증은 **전부 주입**이었다(`map-epoch.mjs`). 이 도구만이
 * *"서버가 그 봉투를 실제로 그렇게 보낸다"* 를 확인한다 — 루트가 정한 대조 셋이 여기 있다:
 *   ① 서버 `zones` 키가 내 가정과 같은가
 *   ② `epoch` 이 올라가는가 · 올라갈 때 화면이 재청하는가 (+ `srv_id` 범위)
 *   ③ 🔴 옛 `slots[]` 와 새 `state` 가 같은 말을 하는가
 *
 * ⚠ **옛 경로를 살려 둔 것이 안전장치인데, 둘이 다른 말을 하면 그 안전장치가 "무엇이 참인가"를
 *   흐린다**(루트 지적). ③이 그것을 재는 자리다.
 *
 * 사용: node web/tools/live-map.mjs --port 10000
 */
import { launch, evaluate, sleep } from './cdp.mjs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const PORT = arg('--port', null);
if (!PORT) { console.error('--port <시험 인스턴스 포트>'); process.exit(2); }
/* 🔴 운영 포트 거부 — 팀 표준(원장 §5.5). 기본값 없음 + 운영 거부. */
if (['9900', '9991', '5500'].includes(String(PORT))) { console.error('🔴 운영 포트 거부: ' + PORT); process.exit(2); }
const URL_ = 'http://127.0.0.1:' + PORT + '/index.html';

let pass = 0, fail = 0, skipped = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; console.log('  ✅ ' + name); }
  else { fail++; console.log('  ❌ ' + name + (detail ? '  → ' + detail : '')); }
}
/**
 * 🔴 **못 잰 것을 통과로도 실패로도 두지 않는다.**
 * 통과로 넘기면 **안 본 것이 본 것이 되고**(§5.40: 통과 수는 밟은 것의 수다),
 * 실패로 두면 **제품 결함으로 읽히고** 늘 빨강인 검사는 아무도 안 읽는다(§3.1 의 형태).
 * 🔑 **그래서 따로 세고 마지막에 분모로 찍는다** — *"검사 0건은 그 자체로 빨강이어야 한다"* 의
 * 짝이다: **몇 건을 못 쟀는지가 보이면 초록이 거짓말을 못 한다.**
 */
function skip(name, why) { skipped++; console.log('  ⏭ ' + name + '  → 측정 불가: ' + why); }

/* 🔴 §5.18 — 실패 집계는 catch 에서도 올린다. */
let client = null;
try {
  console.log('\n대상: ' + URL_ + '  (서버가 페이지도 WS 도 준다)\n');
  client = await launch({ headless: true });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  /* 🔴 **WS 프레임을 CDP 로 직접 잡는다.** ~~페이지 안에서 `handleServerMessage` 를 감싸는 방식~~ 은
     **접속 시 프레임을 놓친다** — 래퍼는 페이지가 뜬 뒤에 설치되고 `snapshot`·`map`·`state` 는
     그 전에 온다. ⚠ **실측으로 겪었다(2026-08-19)**: `state` 를 못 잡아 *"서버가 안 보낸다"* 로
     읽었는데 ③ 대조는 통과했다 — **상태는 와 있었고 내 기록기가 늦었다.**
     🔑 **하니스가 못 본 것을 서버가 안 한 것으로 읽는 형태**(원장 §5.30)이고, WS 층에서 잡으면
     그 시점 의존이 사라진다. */
  const rx = [];
  client.on((method, p) => {
    if (method === 'Network.webSocketFrameReceived') {
      const d = p.response && p.response.payloadData;
      if (typeof d === 'string' && d.charAt(0) === '{') { try { rx.push(JSON.parse(d)); } catch (e) {} }
    }
  });
  await client.send('Network.enable');
  await client.send('Page.navigate', { url: URL_ });

  let ready = false;
  for (let i = 0; i < 120; i++) {
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.querySelector('.grid, #grid')`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  ok('화면이 떴다', ready === true);
  if (ready !== true) throw new Error('화면 준비 실패');

  /* 🔴🔴 **서빙된 페이지가 내가 만든 그 페이지인가.** 이걸 먼저 묻지 않으면
     **남의 코드를 재고 내 것이라고 보고한다**(원장 §5.30 계열 — 계측 대상이 딴 것이었던 형태).
     ⚠ **실측으로 겪었다(2026-08-19)**: 시험 인스턴스가 `GET /index.html → 200` 을 주는데
     **자기 cwd 의 옛 사본**을 서빙했다(127KB 대 내 177KB · `zone-grid` 0건).
     🔑 **"200 이다"는 "내 코드가 서빙된다"가 아니다.** 404 보다 나쁘다 — 되는 것처럼 보인다. */
  const finger = await evaluate(client, `(() => {
    const has = (id) => !!document.getElementById(id);
    return { zoneGrid: has('zone-grid'),
             hasGetMap: typeof requestMap === 'function',
             hasSrvId: (typeof state === 'object' && state !== null && 'srvId' in state) };
  })()`).catch(() => null);
  console.log('  · 서빙된 판본 지표 → ' + JSON.stringify(finger));
  const mine = !!(finger && finger.zoneGrid && finger.hasGetMap && finger.hasSrvId);
  ok('🔴 [하니스] 서빙된 페이지가 내 현재 판본이다', mine,
     JSON.stringify(finger) + ' — 🔴 **아니면 이 하니스는 남의 코드를 잰다.** '
     + '서버 cwd 의 index.html 이 옛 사본이다. 제품 판정으로 읽지 마라');
  if (!mine) throw new Error('서빙된 판본이 내 것이 아니다 — 대조를 진행하면 잘못된 결론이 난다');


  let link = null;
  for (let i = 0; i < 150; i++) {
    link = await evaluate(client, `state.link`).catch(() => null);
    if (link === 'ws') break;
    await sleep(100);
  }
  ok('실제 WS 로 붙었다 (link=' + link + ')', link === 'ws', '데모/폴백이면 서버 프레임이 아니다');
  if (link !== 'ws') throw new Error('WS 실패');

  await sleep(1500);
  let seen = rx.map(m => m && m.type);
  console.log('  · 접속 직후 받은 타입 → ' + JSON.stringify(seen));

  /* ② `map` 이 접속 직후엔 안 올 수 있다(지형이 기동 때 이미 섰다) → `get_map` 을 실기에서 밟는다. */
  const hadMap = seen.includes('map');
  console.log('  · map 이 접속 때 왔나 → ' + hadMap);
  await evaluate(client, `requestMap(true)`);
  let gotMap = false;
  for (let i = 0; i < 60; i++) {
    gotMap = await evaluate(client, `!!state.map`).catch(() => false);
    if (gotMap === true) break;
    await sleep(100);
  }
  ok('🔑 get_map 왕복이 실기에서 돈다', gotMap === true, 'map 을 못 받았다');

  const mp = (() => {
    const m = rx.filter(x => x && x.type === 'map').slice(-1)[0] || null;
    if (!m) return null;
    const z0 = (m.zones || [])[0] || {};
    return { keys: Object.keys(m).sort(), srv_id: m.srv_id, epoch: m.epoch, grid: m.grid,
             zoneN: (m.zones || []).length, zoneKeys: Object.keys(z0).sort(),
             cells0: z0.cells, kinds: [...new Set((m.zones||[]).map(z => z.kind))].sort(),
             modKeys: Object.keys(((z0.modules || [])[0]) || {}).sort() };
  })();
  console.log('\n  🔑 실기 map → ' + JSON.stringify(mp));
  ok('① map 봉투 키가 가정과 같다', !!mp && ['epoch','grid','srv_id','type','zones'].every(k => mp.keys.includes(k)), JSON.stringify(mp && mp.keys));
  ok('① grid 에 rows·cols 가 있다', !!(mp && mp.grid && mp.grid.rows > 0 && mp.grid.cols > 0), JSON.stringify(mp && mp.grid));
  ok('① 자리 키가 가정과 같다 (id·kind·cells·modules)',
     !!mp && ['cells','id','kind','modules'].every(k => mp.zoneKeys.includes(k)), JSON.stringify(mp && mp.zoneKeys));
  ok('① cells 가 [[row,col]] 목록이다', !!(mp && Array.isArray(mp.cells0) && Array.isArray(mp.cells0[0])), JSON.stringify(mp && mp.cells0));
  ok('① kind 가 셋 안에 있다', !!mp && mp.kinds.every(k => ['parking','entrance','exit'].includes(k)), JSON.stringify(mp && mp.kinds));
  console.log('  · 자리 수 ' + (mp && mp.zoneN) + ' · kind ' + JSON.stringify(mp && mp.kinds));

  const st = (() => {
    const s2 = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
    if (!s2) return null;
    const z0 = (s2.zones || [])[0] || {};
    return { keys: Object.keys(s2).sort(), srv_id: s2.srv_id, epoch: s2.epoch,
             zoneKeys: Object.keys(z0).sort(), actions: z0.actions,
             completion: z0.completion, mod0: ((z0.modules || [])[0]) || null };
  })();
  console.log('  🔑 실기 state → ' + JSON.stringify(st));
  ok('① state 봉투 키가 가정과 같다', !!st && ['epoch','srv_id','ts_ms','type','zones'].every(k => st.keys.includes(k)), JSON.stringify(st && st.keys));
  ok('② 🔑 두 봉투의 srv_id 가 같다', !!(mp && st && mp.srv_id && mp.srv_id === st.srv_id), JSON.stringify([mp && mp.srv_id, st && st.srv_id]));
  ok('② 두 봉투의 epoch 이 같다', !!(mp && st && mp.epoch === st.epoch), JSON.stringify([mp && mp.epoch, st && st.epoch]));
  /* ⚠ 모듈 상태 키는 **장치가 등록돼 모듈이 생겨야** 볼 수 있다. 지금 시험 인스턴스에는
     장치가 안 붙으므로 `zone.modules` 가 비어 있다 — **불일치가 아니라 볼 것이 없는 것**이다. */
  if (st && st.mod0) {
    ok('① 모듈 상태 키가 가정과 같다 (devid·name·value·known)',
       ['devid','known','name','value'].every(k => Object.keys(st.mod0).includes(k)), JSON.stringify(st.mod0));
  } else {
    skip('① 모듈 상태 키 대조', '장치가 안 붙어 zone.modules 가 0개다 — 등록(D)이 와야 생긴다');
  }

  /* 🔴 지형이 비어 있으면 그건 서버 결함이다(socket 확정: "0자리"는 정상이 아니다). */
  ok('🔴 지형이 비어 있지 않다', !!(mp && mp.zoneN > 0), '비어 있으면 서버 결함 — socket 에 알려야 한다');

  /* ③ 🔴 옛 slots[] 와 새 state 가 같은 말을 하는가 */
  const cmp = await evaluate(client, `(() => {
    const snap = state.snapshot, zs = state.zoneState;
    if (!snap || !zs) return { skip: true, hasSnap: !!snap, hasState: !!zs };
    const byId = new Map(zs.zones.map(z => [z.id, z]));
    const diff = [];
    for (const s of snap.slots) {
      const z = byId.get(s.id);
      if (!z) continue;
      if (('occupied' in z) && !!z.occupied !== !!s.occupied) diff.push(s.id + ':occupied ' + s.occupied + '/' + z.occupied);
      if (('reserved' in z) && !!z.reserved !== !!s.reserved) diff.push(s.id + ':reserved ' + s.reserved + '/' + z.reserved);
    }
    return { skip: false, checked: snap.slots.length, diff };
  })()`);
  console.log('\n  🔴 ③ 옛 slots[] 대 새 state → ' + JSON.stringify(cmp));
  ok('③ 두 경로가 같은 말을 한다', !!(cmp && cmp.skip === false && cmp.diff.length === 0), JSON.stringify(cmp));

  /* 화면이 실제로 무엇을 그렸나 — 격자가 켜졌고 자리/빈 칸이 갈리는가 */
  const dom = await evaluate(client, `(() => {
    const g = document.getElementById('zone-grid');
    return { on: !g.hidden, oldHidden: document.getElementById('grid').hidden,
             cells: g.children.length, zones: g.querySelectorAll('.zone').length,
             empties: g.querySelectorAll('.zcell--empty').length,
             banner: (() => { const b = document.getElementById('slots-banner');
                              return { hidden: b.hidden, text: b.textContent.slice(0, 90) }; })(),
             acts: [...g.querySelectorAll('.zbtn')].slice(0, 3).map(b => b.dataset.act + ':' + b.getAttribute('aria-disabled') + ':' + b.title.slice(0, 40)) };
  })()`);
  console.log('  · 화면 → ' + JSON.stringify(dom));
  ok('개정 격자가 켜지고 옛 격자가 숨는다', dom.on === true && dom.oldHidden === true);
  ok('자리와 빈 칸이 갈린다', dom.zones > 0 && dom.cells === dom.zones + dom.empties, JSON.stringify(dom));
  /* ⚠ 장치가 안 붙었으니 전 자리가 막혀 있어야 한다 — 그게 정상이다(socket). */
  ok('🔑 장치 없음이 조작 차단으로 나타난다', dom.acts.every(a => a.split(':')[1] === 'true'), JSON.stringify(dom.acts));
  /* 🔴 **단언을 고쳤다 — 내 기대가 틀렸다.** `module_absent` 는 `fault` 계열이고 내 배너는
     **`unknown` 계열만 센다**(설계서 §6: 조작 판정을 *할 수 없는* 자리). 서버는 모듈이 없다는 것을
     **알고 있으므로** 미상이 아니다 → **침묵이 설계대로다.**
     ⚠ **다만 열린 물음으로 남긴다**: 전 자리(12/12)가 `fault` 인데 집계가 조용하다.
     칸마다 이유는 보이지만 **요약이 없다.** 장치 미접속은 *운영 중 고장*이 아니라 *구성 상태*라
     지금은 침묵이 맞다고 보지만, **`fault` 집계가 따로 필요한지는 사람이 봐야 정한다**(§11). */
  ok('미상 집계가 fault 만으로는 침묵한다(설계대로)', dom.banner.hidden === true,
     JSON.stringify(dom.banner) + ' — module_absent 는 fault 계열이라 미상 계수에 안 든다');
} catch (e) {
  fail++;
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 결과가 아니다 */ } }
}

/* 🔴 **분모를 같이 찍는다.** 미측정을 안 찍으면 `18 pass / 0 fail` 이 "다 봤다"로 읽힌다. */
console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail'
  + (skipped ? ' / ' + skipped + ' **미측정**' : '')
  + (skipped ? '\n⚠ 미측정 ' + skipped + '건이 있다. 이 결과는 "본 것"까지다.' : '') + '\n');
process.exit(fail === 0 ? 0 : 1);
