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
import { launch, evaluate, sleep, localStamp } from './cdp.mjs';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { compare } from './screen-build.mjs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const PORT = arg('--port', null);
if (!PORT) { console.error('--port <시험 인스턴스 포트>'); process.exit(2); }
/* 🔴 운영 포트 거부 — 팀 표준(원장 §5.5). 기본값 없음 + 운영 거부.
   ⚠ **통합 검증은 장치가 붙은 인스턴스(운영)에서 한다**(사용자 지시 2026-08-19). **가드를 지우지 않는다** —
   지우면 다음에 실수로 붙는다. 대신 **예외를 명시적으로** 만든다: `--allow-prod` 를 손으로 줘야 하고,
   줬다는 사실을 **화면에 크게 찍고 조작을 전부 시각과 함께 기록**한다.
   🔑 **사용자 브라우저가 같은 화면을 볼 수 있으므로 조작은 최소 횟수**여야 한다(루트 지시). */
const ALLOW_PROD = argv.includes('--allow-prod');
if (['9900', '9991', '5500'].includes(String(PORT))) {
  if (!ALLOW_PROD) { console.error('🔴 운영 포트 거부: ' + PORT + ' — 통합 검증이면 --allow-prod 를 명시해라'); process.exit(2); }
  console.log('🔴🔴 운영 인스턴스(' + PORT + ')에 붙는다 — 사용자 지시에 의한 예외.');
  console.log('     사용자 브라우저가 같은 화면을 볼 수 있다. 조작은 최소로, 전부 기록한다.\n');
}
/** 조작을 언제 무엇을 했는지 남긴다 — socket 이 서버 로그와 맞출 수 있게. */
const acted = [];
function actLog(what) {
  /* 🔴 **현지 시각.** UTC 로 찍었더니 서버 로그(KST)와 시(hour)가 어긋나 같은 사건이 15시간
     떨어져 남았다(§5.64). 조작 기록은 **남의 로그와 맞추는 것이 목적**이라 이게 치명적이었다. */
  const t = localStamp();
  acted.push(t + ' ' + what);
  console.log('  🕐 ' + t + ' — ' + what);
}
const URL_ = 'http://127.0.0.1:' + PORT + '/index.html';
/* 🔑 **cwd 가 아니라 이 파일 위치를 기준으로 잡는다.** 이 도구를 어디서 실행하든 같은 원본을 본다 —
   cwd 상대경로가 무엇을 만드는지는 REQ-0240 이 보여 줬다(서버가 바로 그것 때문에 08-17 판을 냈다). */
const SOURCE_HTML = fileURLToPath(new URL('../../조별과제샘플/index.html', import.meta.url));

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
  const rx = [], tx = [];
  client.on((method, p) => {
    if (method === 'Network.webSocketFrameSent') {
      const d = p.response && p.response.payloadData;
      if (typeof d === 'string' && d.charAt(0) === '{') { try { tx.push(JSON.parse(d)); } catch (e) {} }
    }
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

  /* 🔴🔴 **위 지표만으로는 부족하다** — REQ-0240 이 그것을 보여 줬다.
     위 검사는 *기능이 있나*(`zone-grid` 가 존재하나)를 묻는 **존재형**이다. 08-17 사본처럼
     기능이 통째로 없으면 잡지만, **`zone-grid` 를 가진 어제 사본은 그대로 통과한다.**
     🔑 **물어야 할 것은 "있나"가 아니라 "같은가"다.** 그래서 실기가 내준 HTML 을 **그대로 받아**
     내 저장소 원본과 **내용 해시로 대조**한다 — 긍정형 단언이다(원장 §5.68: 부정형은 대상이
     사라져도 참이 된다).
     ⚠ 이 검사는 **DOM 이 아니라 전선을 탄다.** 브라우저가 이미 받은 것과 같은 경로를 한 번 더 긁어
     **계측 경로(WS)와 실기 경로(HTML)가 갈리는 것**을 닫는다 — 그 갈림이 REQ-0240 의 본체였다. */
  let sameScreen = null;
  try {
    const servedHtml = await (await fetch(URL_)).text();
    const sourceHtml = await readFile(SOURCE_HTML, 'utf8');
    const c = compare(servedHtml, sourceHtml);
    sameScreen = c.same;
    console.log('  · 서빙본 해시 ' + c.servedSha + ' / 원본 ' + c.sourceSha
              + '   표지: ' + (c.stamp.present ? c.stamp.raw : '(표지 없음)'));
    ok('🔴 [하니스] 서빙된 HTML 의 내용이 저장소 원본과 같다 (' + c.servedSha + ' == ' + c.sourceSha + ')',
       c.same,
       '서빙 ' + c.servedSha + ' ≠ 원본 ' + c.sourceSha + ' — 🔴 **화면이 낡았다.** '
       + '`node web/tools/deploy-screen.mjs --check --port ' + PORT + '` 로 어느 파일이 서빙되는지 봐라. '
       + '제품 판정으로 읽지 마라.');
  } catch (e) {
    /* 못 잰 것을 통과로 두지 않는다(§5.40). */
    skip('[하니스] 서빙 HTML 대조', 'HTML 을 다시 못 받았다: ' + e.message);
  }

  if (!mine) throw new Error('서빙된 판본이 내 것이 아니다 — 대조를 진행하면 잘못된 결론이 난다');
  if (sameScreen === false) throw new Error('서빙된 HTML 이 내 원본과 다르다 — 낡은 화면을 재면 잘못된 결론이 난다');


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
  /* ⚠ **이 통과는 "둘 다 맞다"가 아니다.** socket 확인(2026-08-19): 서버가 장치의 점유 비트열을
     **hex 를 10진으로** 읽고 있었다 → `occupied` 자체가 틀린 값일 수 있다. 두 경로는 **같은 값**에서
     나오므로 **같이 틀린다.** 🔑 이 검사가 재는 것은 *일치*뿐이고 *정확*이 아니다. 점유가 고쳐지면
     ③은 다시 재야 한다(같은 검사로 충분하다 — 일치는 그때도 필요조건이다). */
  console.log('  ⚠ ③은 두 경로의 *일치*만 잰다 — *정확*은 아래 점유 검사 + socket 의 /ws 확인이 갈라 맡는다');

  /* 화면이 실제로 무엇을 그렸나 — 격자가 켜졌고 자리/빈 칸이 갈리는가 */
  const dom = await evaluate(client, `(() => {
    const g = document.getElementById('zone-grid');
    return { on: !g.hidden, oldHidden: document.getElementById('grid').hidden,
             cells: g.children.length, zones: g.querySelectorAll('.zone').length,
             empties: g.querySelectorAll('.zcell--empty').length,
             banner: (() => { const b = document.getElementById('slots-banner');
                              return { hidden: b.hidden, text: b.textContent.slice(0, 90) }; })(),
             acts: [...g.querySelectorAll('.zbtn')].slice(0, 3).map(b => b.dataset.act + ':' + b.getAttribute('aria-disabled') + ':' + b.title.slice(0, 40)),
             allActs: [...g.querySelectorAll('.zbtn')].map(b => ({ zone: b.dataset.zone, act: b.dataset.act,
                                                                   off: b.getAttribute('aria-disabled') === 'true' })) };
  })()`);
  console.log('  · 화면 → ' + JSON.stringify(dom));
  ok('개정 격자가 켜지고 옛 격자가 숨는다', dom.on === true && dom.oldHidden === true);
  ok('자리와 빈 칸이 갈린다', dom.zones > 0 && dom.cells === dom.zones + dom.empties, JSON.stringify(dom));
  /* 🔴 **세 번째로 같은 실수를 했다.** 여기 원래 단언은 *"장치가 없으니 전 자리가 막혀 있다"* 였고,
     장치가 붙어 `reserve` 가 열리자 **제품이 옳은데 검사가 빨강**이 됐다. 배너에서 두 번 겪고
     규칙으로 바꿨는데 **이 줄만 조건에 박힌 채 남아 있었다**(원장 §5.33 을 옆줄에 적어 두고도 놓쳤다).
     🔑 규칙으로 바꾼다 — **버튼 잠김 == 서버가 그 조작을 `ok:true` 라 하지 않았다.**
     이 불변식은 장치 유무·거절 코드·자리 수와 **무관**하므로 조건이 바뀌어도 안 틀린다.
     ⚠ 화면과 전선을 각각 읽으므로 사이에 새 `state` 가 오면 **거짓 빨강**이 난다 → 재읽기로 가른다
        (흔들리는 검사는 결국 무시당한다 — 빨강을 아끼는 게 아니라 **믿을 수 있게** 만드는 것이다). */
  let actCmp = null;
  for (let attempt = 0; attempt < 3; attempt++) {
    const wire = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
    const byId = new Map((wire ? wire.zones || [] : []).map(z => [z.id, z.actions || {}]));
    /* 🔴 **2026-08-19 갱신.** 판정자를 하나로 모으면서(§5.52 수정) 아이들도 버튼을 잠그게 됐다.
       불변식에 그것을 넣지 않으면 **옳은 동작이 빨강**이 된다 — 검사가 제품을 따라가야 하는 자리다.
       🔑 잠김 == `서버가 ok 라 하지 않았다` **또는** `아이들이 남았다`. 아이들을 **같은 순간에** 읽어야
       한다(따로 읽으면 그 사이에 풀려서 거짓 빨강이 난다). */
    const snap = await evaluate(client, `(() => ({
      cooldown: (function () { try { return cooldownLeftMs(); } catch (e) { return -1; } })(),
      acts: [...document.querySelectorAll('#zone-grid .zbtn')].map(b => ({ zone: b.dataset.zone, act: b.dataset.act, off: b.getAttribute('aria-disabled') === 'true' }))
    }))()`);
    const acts = snap ? snap.acts : [];
    const cool = snap ? snap.cooldown : -1;
    const bad = (acts || []).filter(b => {
      const a = (byId.get(b.zone) || {})[b.act];
      return b.off !== !(a && a.ok === true && cool === 0);
    });
    actCmp = { buttons: (acts || []).length, mismatch: bad.length, sample: bad.slice(0, 3), cooldownLeftMs: cool };
    if (!wire || bad.length === 0) break;
  }
  ok('🔑 버튼 잠김이 서버의 actions.ok 와 일치한다', !!(actCmp && actCmp.buttons > 0 && actCmp.mismatch === 0),
     JSON.stringify(actCmp));

  /* ══ 점유가 화면까지 옳게 오는가 — socket 의 hex 해독 수정(49c07f6) 뒤 재측정 ═══════════
     🔴 **socket 이 준 기댓값(A2·A3·B4)을 박지 않는다.** 다음 프레임이면 값이 달라지고
     그러면 **제품이 옳은데 검사가 빨강**이 된다 — 오늘 세 번 한 실수의 네 번째가 된다.
     대신 규칙을 단언한다: **주차 자리의 요약 문구 == 전선 (occupied, reserved) 조합.**
     🔑 그리고 **층을 갈라 적는다**(원장 §5.34.2):
        이 검사가 재는 것 = "화면이 서버 값을 그대로 그린다"
        이 검사가 **못** 재는 것 = "서버 값이 장치의 비트와 맞다" → socket 이 /ws 로 확인했다
     둘을 합쳐 "점유가 맞다"로 쓰면 내가 확인하지 않은 것을 확인했다고 적는 것이다. */
  const occCmp = await (async () => {
    for (let attempt = 0; attempt < 3; attempt++) {
      const wire = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
      if (!wire) return { skip: 'state 봉투가 없다' };
      const want = new Map((wire.zones || []).map(z => [z.id,
        (z.occupied && z.reserved) ? '예약한 자리에 입차' : z.occupied ? '사용 중' : z.reserved ? '예약됨' : '빈 자리']));
      const seen = await evaluate(client, `[...document.querySelectorAll('#zone-grid .zone')].filter(e => e.dataset.kind === 'parking').map(e => ({ id: (e.querySelector('.zone__id') || {}).textContent, sum: (e.querySelector('.zone__sum') || {}).textContent }))`);
      const bad = (seen || []).filter(s => want.has(s.id) && want.get(s.id) !== s.sum)
                              .map(s => s.id + ' 화면"' + s.sum + '" 전선"' + want.get(s.id) + '"');
      const occN = [...want.values()].filter(v => v !== '빈 자리').length;
      const r = { parking: (seen || []).length, mismatch: bad.length, sample: bad.slice(0, 3),
                  전선점유수: occN, 점유자리: (wire.zones || []).filter(z => z.occupied).map(z => z.id) };
      if (bad.length === 0) return r;
      if (attempt === 2) return r;
    }
  })();
  console.log('  🔴 점유 화면-대-전선 → ' + JSON.stringify(occCmp));
  ok('🔑 점유·예약이 화면에 서버 값 그대로 나온다', !!(occCmp && occCmp.parking > 0 && occCmp.mismatch === 0),
     JSON.stringify(occCmp));
  /* ⚠ **전 자리가 비어 있으면 이 통과는 "빈 자리"만 밟은 것이다.** 분모를 눈에 보이게 적는다 —
     오늘 반복해서 배운 것: 통과 수는 밟은 것의 수이고, 안 밟은 갈래는 통과에 안 나타난다. */
  if (occCmp && occCmp.전선점유수 === 0) {
    skip('점유 "사용 중" 갈래', '지금 전 자리가 비어 있다 — 빈 자리 갈래만 밟았다(주입·실물 변화가 필요하다)');
  }
  /* 🔴 **단언을 두 번 고쳤다. 두 번 다 내 단언이 한 조건에 박혀 있었다.**
     ① 처음: *"전 자리가 막혔으니 배너가 뜬다"* → `module_absent` 는 **`fault`** 라 미상 계수에 안 든다
     ② 다음: *"fault 만이니 침묵한다"* → 등록이 생기자 `node_offline`(**`unknown`**)이 와서 **뜬다**
     🔑 **둘 다 결과를 박은 것이 원인이다.** → **규칙을 단언한다**: **미상 계열 차단이 하나라도 있으면
     뜨고, 없으면 침묵한다.** 그러면 조건이 바뀌어도 이 검사가 안 틀린다.
     (설계서 §6 · 원장 §5.33 — `busy`·`fault` 는 안 센다. `fault` 집계 필요 여부는 열린 물음이다.) */
  const unknownBlocks = (() => {
    const s4 = rx.filter(x => x && x.type === 'state').slice(-1)[0];
    const FAM = { node_offline: 1, node_unregistered: 1 };
    let n = 0;
    for (const z of (s4 && s4.zones) || []) {
      let hit = false;
      for (const k of Object.keys(z.actions || {})) {
        const a = z.actions[k] || {};
        if (a.ok !== true && FAM[a.reason]) hit = true;
      }
      if (hit || !z.actions) n++;
    }
    return n;
  })();
  console.log('  · 미상 계열로 막힌 자리 수 → ' + unknownBlocks);
  ok('미상 집계가 규칙대로다 (미상 차단 ' + unknownBlocks + '건 → 배너 ' + (unknownBlocks > 0 ? '뜸' : '침묵') + ')',
     dom.banner.hidden === (unknownBlocks === 0),
     JSON.stringify(dom.banner) + ' — fault·busy 는 안 센다(설계서 §6)');
  /* ══ ⑥ `fault` 가 두 상태를 갈라 주는가 — **보기만 한다. 고치지 않는다**(루트) ═══ */
  console.log('\n[⑥] 거절 코드가 "구성"과 "고장"을 갈라 주는가 (관찰만)');
  const reasons = (() => {
    const s3 = rx.filter(x => x && x.type === 'state').slice(-1)[0];
    const out = {};
    for (const z of (s3 && s3.zones) || []) for (const k of Object.keys(z.actions || {})) {
      const r = (z.actions[k] || {}).reason; if (r) out[r] = (out[r] || 0) + 1;
    }
    return out;
  })();
  console.log('  · 나온 거절 코드 → ' + JSON.stringify(reasons));
  /* 🔴 원장 §5.44.1: 장치가 한 번도 안 붙어도 `module_absent`(fault)로 온다 — 계약표는 그 상태를
     `node_unregistered`(unknown)로 정의한다. **등록이 생긴 지금 그 코드가 사라지는지**가 관찰점이다. */
  ok('⑥ 등록이 생기면 module_absent 가 줄어든다(관찰)', !reasons.module_absent || reasons.module_absent < 12,
     JSON.stringify(reasons) + ' — 12개 그대로면 등록이 자리에 안 붙었다는 뜻이다');
  if (reasons.node_unregistered) console.log('  🔑 node_unregistered 가 실기에서 나왔다 — 구성/고장 구분이 산다');
  else skip('⑥ node_unregistered 실물 확인', '지금 자료에 그 코드가 없다 — 등록 전 순간을 못 잡았다');

  /* ══ ⑤ 조작이 전선까지 나가는가 — 🔴 **최소 횟수**. 예약 한 번 + 반드시 취소 ═══ */
  console.log('\n[⑤] 조작이 실제로 나가는가 (최소 1회 · 반드시 취소까지)');
  /* 🔴 **한 검사가 두 주장을 덮고 있었다.** ⑤가 재려는 것은 *"예약이 전선까지 나가는가"* 인데,
     그냥 첫 열린 버튼을 고르니 **점유된 자리**(A3)에 닿아서 REQ-0235 결함으로 빨강이 됐다.
     그러면 **예약 왕복 자체는 측정되지 않는다** — 결함 하나가 다른 항목의 측정을 먹는다.
     → ⑤는 **전선이 비었다고 말하는 자리**로 잰다. 모순은 아래에서 **따로** 단언한다.
     🔑 **한 검사에 한 주장.** 그러면 어느 것이 깨졌는지 결과가 바로 말해 준다. */
  const occNow = (() => {
    const w = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
    const s = new Set();
    for (const z of (w ? w.zones || [] : [])) if (z.occupied) s.add(z.id);
    return s;
  })();
  /* 🔴 REQ-0235 — 점유된 자리에 예약이 열려 있으면 화면에 "눌리는데 거부되는" 버튼이 생긴다.
     socket 이 ⓐ(서버 결함)로 확정하고 고쳤다. **배포 전까지 이 검사는 빨강이고 그게 맞다** —
     배포되면 저절로 초록이 된다. ⚠ 규칙으로 쓴다(자리 이름을 박지 않는다). */
  const badResv = (() => {
    const w = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
    return (w ? w.zones || [] : []).filter(z => z.occupied && z.actions && z.actions.reserve
                                                && z.actions.reserve.ok === true).map(z => z.id);
  })();
  ok('🔴 점유된 자리에는 예약이 열려 있지 않다 (REQ-0235)', badResv.length === 0,
     JSON.stringify(badResv) + ' — socket 이 ⓐ 로 확정하고 고쳤다. 배포 전이면 이 빨강이 정상이다');

  /* 🔴 위 초록이 말하는 것은 **"예약이 안 열렸다"뿐이다** — `occupied` 가 **옳게 그려지는지**는
     다른 주장이다(§5.80 한 검사에 한 주장). 그래서 따로 잰다:
     ```
     버튼은 잠기고 · 기호는 **없고**(정상 상태다) · 배너는 **안 센다**
     ```
     🔑 세 번째가 이 계열 결정의 핵심 근거다 — `unknown` 으로 뒀으면 **점유된 자리 수만큼 배너가
     뜬다.** 주차장에 차가 있는 것은 정상이므로 그건 *"정상에서 침묵한다"* 를 깨뜨린다(설계서 §6). */
  {
    const w = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
    const occZones = (w ? w.zones || [] : []).filter(z => z.actions && z.actions.reserve
                                                    && z.actions.reserve.reason === 'occupied').map(z => z.id);
    if (!occZones.length) skip('점유 사유(occupied) 표시', '이번 자료에 reason=occupied 가 없다 — 전 자리가 비어 있다');
    else {
      const view = await evaluate(client, `(() => {
        const out = {};
        for (const b of document.querySelectorAll('#zone-grid .zbtn[data-act="reserve"]')) {
          out[b.dataset.zone] = { off: b.getAttribute('aria-disabled') === 'true',
                                  mark: (b.textContent.match(/[⏱⚠⏳]/) || [''])[0],
                                  title: (b.title || '').slice(0, 40) };
        }
        const ban = document.getElementById('slots-banner');
        return { btns: out, bannerHidden: ban ? ban.hidden : null };
      })()`);
      const bad = occZones.filter(id => !view.btns[id] || view.btns[id].off !== true || view.btns[id].mark !== '');
      console.log('  🔑 점유 사유 표시 → ' + JSON.stringify({ zones: occZones, view: occZones.map(id => view.btns[id]), bannerHidden: view.bannerHidden }));
      ok('🔑 점유 사유는 잠기지만 기호를 붙이지 않는다 (계열 normal)', bad.length === 0, JSON.stringify(bad));
      ok('🔑 🔴 점유는 미상 배너를 띄우지 않는다 (정상에서 침묵한다)', view.bannerHidden === true,
         JSON.stringify({ occ: occZones.length, bannerHidden: view.bannerHidden })
           + ' — unknown 계열로 뒀으면 여기서 배너가 떴다');
    }
  }

  const target = await evaluate(client, `(() => {
    const occ = ${JSON.stringify([...occNow])};
    const b = [...document.querySelectorAll('#zone-grid .zbtn[data-act="reserve"]')]
      .find(x => x.getAttribute('aria-disabled') === 'false' && occ.indexOf(x.dataset.zone) < 0);
    return b ? b.dataset.zone : null;
  })()`);
  if (!target) {
    skip('⑤ 예약 왕복', '누를 수 있는 자리가 없다 — 전 자리가 막혀 있다(장치·등록 상태를 먼저 봐라)');
  } else {
    /* 🔑 아이들이 **클릭 때문에** 켜졌는지 가른다. 클릭 뒤에만 찍으면 그 구분이 영영 안 된다 —
       "직전"과 "직후"를 같이 찍는 것이 원인 귀속의 최소 조건이다. */
    /* 🔑 get_map 이 왜 나갔는지는 **에폭 불일치가 되풀이되는가**에 달렸다 — 그건 서버 쪽 질문일 수
       있으므로(내 화면 결함과 별개) 같이 찍는다. 화면 안에서만 원인을 찾다가 남의 축을 놓치지 않게. */
    const beforeClick = await evaluate(client, `(() => { try { return { cooldown: cooldownLeftMs(), link: state.link,
      mapEpoch: state.map && state.map.epoch, zsEpoch: state.zoneState && state.zoneState.epoch,
      mapStale: state.mapStale, olderRun: state.olderRun, heldState: !!state.heldState }; } catch (e) { return { err: e.message }; } })()`).catch(e => ({ err: String(e) }));
    console.log('  🔑 클릭 직전 → ' + JSON.stringify(beforeClick));
    /* ⓐ 수정 ①의 확인 — 읽기 질의는 아이들을 켜지 않는다.
       ⚠ **`get_map` 이 실제로 나갔을 때만** 이 검사가 무엇을 잰다. 안 나갔으면 통과가 공짜다
          → 분모 밖으로 내보낸다(오늘 반복해서 배운 것: 통과 수는 밟은 것의 수다). */
    const sentQuery = tx.some(m => m && m.type === 'get_map');
    if (!sentQuery) skip('읽기 질의가 아이들을 켜지 않는다', 'get_map 이 이번 실행에서 안 나갔다 — 밟을 것이 없었다');
    else ok('🔑 읽기 질의(get_map)가 아이들을 켜지 않는다', beforeClick && beforeClick.cooldown === 0,
            JSON.stringify(beforeClick) + ' — 979~987 이면 §5.51 재발이다');

    /* ⓒ 다시 그려도 포커스가 남는가. 격자는 매 렌더 통째로 다시 만들어지므로(textContent='')
       `state` 한 장만 와도 포커스가 날아갔다. 누른 직후가 정확히 그 순간이라 이게 없으면
       스크린리더 사용자는 이유를 듣는 중에 초점을 잃는다. */
    const focusKeep = await evaluate(client, `(async () => {
      const b = [...document.querySelectorAll('#zone-grid .zbtn')][0];
      if (!b) return { skip: 'zbtn 이 없다' };
      const want = b.dataset.zone + '/' + b.dataset.act;
      b.focus();
      const before = document.activeElement === b;
      await new Promise(r => setTimeout(r, 1700));   // state 한 장 이상 지나가게 둔다
      const a = document.activeElement;
      return { want: want, before: before, sameNode: a === b,
               after: a && a.dataset ? (a.dataset.zone || '') + '/' + (a.dataset.act || '') : (a ? a.tagName : 'null') };
    })()`).catch(e => ({ err: String(e) }));   /* evaluate 는 awaitPromise:true 다(cdp.mjs:148) */
    console.log('  🔑 다시 그린 뒤 포커스 → ' + JSON.stringify(focusKeep));
    if (focusKeep && focusKeep.skip) skip('다시 그려도 포커스가 남는다', focusKeep.skip);
    else ok('🔑 다시 그려도 포커스가 남는다',
            !!(focusKeep && focusKeep.before === true && focusKeep.after === focusKeep.want),
            JSON.stringify(focusKeep) + ' — 격자는 매 렌더 통째로 다시 만들어진다');
    actLog('예약 클릭 — 자리 ' + target);
    await evaluate(client, `document.querySelector('#zone-grid .zbtn[data-act="reserve"][data-zone=' + ${JSON.stringify(JSON.stringify(target))} + ']').click()`);
    let d = false;
    for (let i = 0; i < 40; i++) { d = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false); if (d) break; await sleep(50); }
    ok('⑤ 확인 대화상자가 열린다', d === true);
    /* 🔴 **사후 진단이 나를 속였다.** 확인 뒤 2.5초에 읽은 oldAction 은 "reserve"(통과 조건)인데
       화면은 그 순간 거부 문구를 띄웠다 → **판정은 클릭 순간에 났고 그 뒤에 조건이 회복됐다.**
       🔑 그래서 **누르기 직전**을 따로 찍는다. 시간에 따라 갈리는 것은 사후에 못 잡는다. */
    const pre = await evaluate(client, `(() => {
      const s = (typeof state !== 'undefined' && state.snapshot) || null;
      const os = s ? (s.slots || []).find(x => x.id === ${JSON.stringify(target)}) : null;
      let act = os ? '?' : 'no-old-slot';
      try { if (os) act = (tileView(os) || {}).action; } catch (e) { act = 'throw:' + e.message; }
      let ca = '?'; try { ca = canAct(); } catch (e) { ca = 'throw:' + e.message; }
      /* 🔑 canAct() 의 거짓 조건은 **둘뿐**이다(index.html:1240) — 아이들 잔여 또는 link 가 ws/demo 가 아님.
         둘을 같이 찍어야 어느 쪽인지 갈린다. 하나만 찍으면 또 추정이 된다. */
      let cd = '?'; try { cd = cooldownLeftMs(); } catch (e) { cd = 'throw:' + e.message; }
      return { oldAction: act, canAct: ca, cooldownLeftMs: cd, link: (typeof state !== 'undefined' ? state.link : null),
               hasSnap: !!s, ageMs: s && s.receivedAt ? Date.now() - s.receivedAt : null,
               deviceOnline: s ? s.device && s.device.online : null };
    })()`).catch(e => ({ err: String(e) }));
    console.log('  🔑 누르기 직전 → ' + JSON.stringify(pre));
    /* 🔴 아이들은 transport.send 에서만 켜진다(index.html:2981) → **무엇이 보냈는지**가 원인이다.
       화면 안을 더 들여다보는 대신 **전선 기록**으로 답한다 — 내 하니스가 보내는 것까지 다 잡힌다. */
    console.log('  🔑 여태 보낸 것 → ' + JSON.stringify(tx.map(m => m && m.type)));
    actLog('확인 누름 — 예약 요청 전송');
    await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);
    /* 🔴 아이들은 1초다. 아래 sleep(2500) 뒤에 재면 **항상 지나 있다** — 그러면 검사가
       영원히 "미측정"으로 통과해 버린다. **보낸 직후**에 표본을 떠 둔다.
       🔑 시간에 걸린 것은 "나중에 확인"이 안 된다. 표본을 그 순간에 떠야 한다.

       🔴 **그런데 "직후"를 클릭 직후로 잡으면 흔들린다.** `confirmDialog` 는 `close` 이벤트로
       풀리므로 `.click()` 이 돌아온 시점엔 **아직 안 보냈다.** 실측: 한 회차는 `left=997`(맞음),
       다음 회차는 `left=0`(전송 전에 떠서 미측정) — **같은 코드가 회차마다 갈렸다.**
       ⚠ 그리고 실패 방향이 "미측정"이라 **조용히 안 재고 지나간다.** 빨강보다 나쁘다.
       → **전선에 프레임이 나간 것을 확인한 뒤** 표본을 뜬다. 아이들 1초 안에 충분히 든다. */
    for (let i = 0; i < 40; i++) {
      if (tx.some(m => m && m.type === 'reserve')) break;
      await sleep(20);
    }
    const cdShot = await evaluate(client, `(() => {
      let left = -1; try { left = cooldownLeftMs(); } catch (e) {}
      const locked = [...document.querySelectorAll('#zone-grid .zbtn')].filter(b => b.getAttribute('aria-disabled') === 'true');
      return { left: left, locked: locked.length, titles: locked.slice(0, 2).map(b => b.title),
               counting: locked.some(b => /초 뒤에 다시 누를 수 있습니다/.test(b.title || '')) };
    })()`).catch(e => ({ err: String(e) }));
    await sleep(2500);
    const sentRv = tx.filter(m => m && m.type === 'reserve').slice(-1)[0] || null;
    const resp = rx.filter(m => m && (m.type === 'queued' || m.type === 'ack' || m.type === 'error'))
                   .filter(m => sentRv && m.rid === sentRv.rid).map(m => m.type + (m.code ? '/' + m.code : ''));
    console.log('  · 보낸 예약 → ' + JSON.stringify(sentRv) + '  · 응답 → ' + JSON.stringify(resp));
    /* 🔴 **"안 나갔다"로 끝내면 원인을 못 짚는다.** 확인을 눌러도 안 나가는 길은 화면 안에 둘뿐이고
       (`requestReserve` 의 확인-후 재검증) **문구 하나가 그 둘을 가른다** —
       "장치 연결이 끊겨"(deviceDown) 대 "그 사이 자리 상태가 바뀌어"(옛 경로가 예약 불가로 본다).
       🔑 재검증은 **옛 `slots[]`** 를 보는데 버튼은 **새 `actions`** 로 그려지므로, 둘이 갈리면
       사용자는 **눌리는 버튼을 누르고 거부를 받는다.** 그래서 실패 시 이 진단을 같이 찍는다. */
    if (!sentRv) {
      const why = await evaluate(client, `(() => {
        const m = [...document.querySelectorAll('#messages .msg')].map(x => x.textContent).filter(t => t.indexOf(${JSON.stringify(target)} + ' · ') === 0).slice(-1)[0] || null;
        /* 🔴 ~~window.state && state.snapshot~~ 로 썼다가 **거짓 "없다"** 를 받았다.
           최상위 const state 는 **window 의 속성이 안 된다**(전역 객체에 붙는 건 var·함수뿐).
           그래서 window.state 가 undefined → 진단이 no-snapshot 을 찍었고 **나는 그것을
           "화면이 스냅샷을 못 받았다"로 읽었다.** ③이 바로 위에서 state.snapshot 을 잘 읽고
           통과한 것과 모순이었는데 그 모순을 안 봤다.
           🔑 **진단이 "없다"고 할 때 먼저 의심할 것은 대상이 아니라 내 읽는 법이다.**
           ⚠ 그리고 이 주석은 **템플릿 문자열 안**이다 — 역따옴표를 쓰면 문자열이 끊겨
           SyntaxError 가 난다(방금 그렇게 깨졌다). 여기서는 강조에 역따옴표를 쓰지 마라. */
        const s = (typeof state !== 'undefined' && state.snapshot) || null;
        const z = ((state.zoneState && state.zoneState.zones) || []).find(x => x.id === ${JSON.stringify(target)}) || null;
        /* 🔑 재검증이 실제로 무엇을 보는지 그대로 찍는다 — tileView(옛 자리).action 이 판정자다.
           이것이 reserve 가 아니면 버튼이 열려 있어도 **확인 뒤에 거부**된다. */
        const os = s ? (s.slots || []).find(x => x.id === ${JSON.stringify(target)}) : null;
        let oldAction = os ? 'tileView-실패' : 'no-old-slot';
        try { if (os && typeof tileView === 'function') oldAction = (tileView(os) || {}).action; } catch (e) { oldAction = 'throw:' + e.message; }
        return { msg: m, oldAction: oldAction, deviceOnline: s ? s.device && s.device.online : 'no-snapshot',
                 oldSlot: s ? (s.slots || []).find(x => x.id === ${JSON.stringify(target)}) || 'not-in-slots' : 'no-snapshot',
                 newActions: z ? z.actions : null, newOccupied: z ? z.occupied : null };
      })()`).catch(e => ({ err: String(e) }));
      console.log('  🔑 왜 안 나갔나 → ' + JSON.stringify(why));
    }
    ok('⑤ 예약이 전선 형식으로 나갔다 (type·slot·rid)',
       !!(sentRv && sentRv.type === 'reserve' && sentRv.slot === target && sentRv.rid), JSON.stringify(sentRv));
    ok('⑤ 서버가 응답했다 (queued/ack/error)', resp.length > 0, JSON.stringify(resp) + ' — 무응답이면 화면 자체 타임아웃이 돈다');

    /* ⓑ 수정 ②의 확인 — **하행을 보낸 뒤에는** 아이들이 화면에 보여야 한다.
       🔴 전에는 아이들 동안 버튼이 그냥 정상으로 보였다. 설계 의도는 반대다:
       *"안 먹혔다고 생각해 더 세게 누르고, 그러면 막으려던 것을 우리가 만든다"* →
       **남은 시간이 줄어드는 것이 보여야 한다.** 그래서 문구 안의 숫자까지 확인한다.
       ⚠ 아이들은 1초라 이 검사는 **보낸 직후 창**에서만 성립한다. 이미 지났으면 미측정이다. */
    if (sentRv) {
      console.log('  🔑 보낸 직후 아이들 표시 → ' + JSON.stringify(cdShot));
      if (!cdShot || cdShot.left <= 0) skip('아이들이 화면에 보인다', '보낸 직후에도 아이들이 0 이었다 — 잴 창이 없었다');
      else ok('🔑 아이들 동안 버튼이 잠기고 남은 시간이 보인다', cdShot.locked > 0 && cdShot.counting === true, JSON.stringify(cdShot));
    }

    /* 🔴 **반드시 되돌린다.** 시험이 상태를 남기면 사용자가 실제 예약으로 읽는다. */
    const canCancel = await evaluate(client, `!!document.querySelector('#zone-grid .zbtn[data-act="cancel"][data-zone=' + ${JSON.stringify(JSON.stringify(target))} + ']')`);
    if (canCancel) {
      actLog('취소 클릭 — 자리 ' + target);
      await evaluate(client, `document.querySelector('#zone-grid .zbtn[data-act="cancel"][data-zone=' + ${JSON.stringify(JSON.stringify(target))} + ']').click()`);
      for (let i = 0; i < 40; i++) { const dd = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false); if (dd) break; await sleep(50); }
      actLog('확인 누름 — 취소 요청 전송');
      await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);
      await sleep(2000);
      ok('⑤ 🔴 취소까지 보냈다(상태를 남기지 않는다)', tx.some(m => m && m.type === 'cancel'), JSON.stringify(tx.map(m => m.type)));
    } else {
      /* 예약이 확정되지 않았으면 취소 버튼이 없다 — 남은 상태도 없다는 뜻이다. */
      skip('⑤ 취소 왕복', '취소 버튼이 없다 = 예약이 확정되지 않았다 → 남긴 상태도 없다');
    }
  }
  /* ══ ⑦ 가상 모듈(`V`)과 차단봉 조작 — arduino 굽기 `9edbcca` · socket `3c9e724` 뒤 ═══════ */
  console.log('\n[⑦] 가상 모듈 표시와 차단봉 조작');
  {
    const wire = rx.filter(x => x && x.type === 'map').slice(-1)[0] || null;
    const vMods = [];
    for (const z of (wire ? wire.zones || [] : [])) {
      for (const m of (z.modules || [])) {
        if (typeof m.kind === 'string' && m.kind.length === 3 && m.kind.endsWith('V')) vMods.push({ zone: z.id, kind: m.kind, name: m.name });
      }
    }
    console.log('  · 전선의 가상 모듈 → ' + JSON.stringify(vMods));
    if (!vMods.length) skip('⑦ V 접미 실물 표시', '전선에 kind 가 V 로 끝나는 모듈이 없다 — 굽기 전이거나 등록이 안 됐다');
    else {
      /* 🔑 **규칙을 단언한다**: 전선에 `V` 모듈이 있는 자리의 화면 행에 `(가상)` 이 있어야 한다.
         ⚠ `"차단봉 (가상)"` 같은 **문구를 박지 않는다** — 라벨이 바뀌면 제품이 옳은데 빨강이 된다.
            내가 재는 것은 **"가상이라는 사실이 표시되는가"** 이고 낱말 선택이 아니다. */
      const rows = await evaluate(client, `[...document.querySelectorAll('#zone-grid .zone')].map(e => ({
        id: (e.querySelector('.zone__id') || {}).textContent,
        mods: [...e.querySelectorAll('.zmod')].map(x => x.textContent.slice(0, 60)) }))`);
      const byZone = new Map((rows || []).map(r => [r.id, r.mods.join(' | ')]));
      const missing = vMods.filter(v => !(byZone.get(v.zone) || '').includes('가상'));
      console.log('  · 화면 행 → ' + JSON.stringify((rows || []).filter(r => vMods.some(v => v.zone === r.id))));
      ok('⑦ 가상 모듈이 화면에 "(가상)"으로 표시된다', missing.length === 0, JSON.stringify(missing));
      /* 🔴 원문 `kind` 가 그대로 새는지도 본다 — 라벨 표에서 못 찾으면 `"OBV"` 가 그대로 뜬다(§5.58). */
      const raw = vMods.filter(v => (byZone.get(v.zone) || '').includes(v.kind));
      ok('⑦ 원문 kind 가 화면에 새지 않는다', raw.length === 0, JSON.stringify(raw.map(v => v.kind)));
    }

    /* ── 차단봉 조작 — 🔴 완료는 **ACK 이 아니라 다음 S 의 에코**다(socket §7.2) ── */
    /* 🔴 **지금 값과 *반대* 조작을 고른다** (socket 지적 2026-08-19).
       앞 판본은 그냥 첫 버튼(=`open_gate`)을 눌렀는데 **에코 비트가 이미 1 이었다** —
       `settled` 가 떴지만 **상태 변화는 한 번도 안 밟혔다.** 🔴 **"명령이 상태를 바꾼다"가
       실물에서 미검증인 채로 초록이었다** — 오늘 네 번째 헛통과이고 **이번엔 하니스가 아니라
       제품 측정에서** 났다.
       🔑 그래서 **바꿀 것이 있는 조작을 고른다**: 값이 참이면 닫기, 거짓이면 열기.
       ⚠ 값을 모르면(`known:false`) 어느 쪽이 변화인지 알 수 없으므로 **미측정**으로 둔다 —
          찍어서 맞히면 그게 또 헛통과다. */
    const gate = await (async () => {
      const w = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
      const btns = await evaluate(client, `[...document.querySelectorAll('#zone-grid .zbtn')]
        .filter(x => x.dataset.act === 'open_gate' || x.dataset.act === 'close_gate')
        .map(x => ({ zone: x.dataset.zone, act: x.dataset.act, off: x.getAttribute('aria-disabled') === 'true' }))`);
      for (const z of (w ? w.zones || [] : [])) {
        /* `state` 의 모듈에는 `kind` 가 없다(그건 `map` 쪽이다) — 값이 확정된 것을 고른다.
           입·출구 자리는 모듈이 하나(`OBV`)라 이걸로 충분하다. */
        const m = (z.modules || []).find(x => x && x.known === true && typeof x.value === 'boolean');
        if (!m) continue;
        const want = m.value === true ? 'close_gate' : 'open_gate';
        const b = (btns || []).find(x => x.zone === z.id && x.act === want && !x.off);
        if (b) return { zone: z.id, act: want, valueBefore: m.value, wantAfter: !m.value };
      }
      return null;
    })();
    if (!gate) {
      const why = await evaluate(client, `[...document.querySelectorAll('#zone-grid .zbtn')].filter(x => x.dataset.act && x.dataset.act.indexOf('gate') >= 0).map(x => x.dataset.zone + '/' + x.dataset.act + ':' + x.title.slice(0, 40))`).catch(() => null);
      skip('⑦ 차단봉 조작 왕복', '누를 수 있는 차단봉 버튼이 없다 → ' + JSON.stringify(why));
    } else {
      const before = await evaluate(client, `(() => { try { return cooldownLeftMs(); } catch (e) { return -1; } })()`);
      for (let i = 0; i < 30 && before > 0; i++) await sleep(100);   // 아이들이 남았으면 기다린다
      actLog('차단봉 클릭 — ' + gate.zone + ' / ' + gate.act);
      await evaluate(client, `[...document.querySelectorAll('#zone-grid .zbtn')].find(x => x.dataset.zone === ${JSON.stringify(gate.zone)} && x.dataset.act === ${JSON.stringify(gate.act)}).click()`);
      let dg = false;
      for (let i = 0; i < 40; i++) { dg = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false); if (dg) break; await sleep(50); }
      ok('⑦ 차단봉도 확인을 받는다', dg === true);
      actLog('확인 누름 — ' + gate.act + ' 전송');
      await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);

      const rxAt = rx.length;
      for (let i = 0; i < 60 && !tx.some(m => m && m.type === gate.act); i++) await sleep(50);
      const sentG = tx.filter(m => m && m.type === gate.act).slice(-1)[0] || null;
      console.log('  · 보낸 차단봉 명령 → ' + JSON.stringify(sentG));
      ok('⑦ 조작이 전선 형식으로 나갔다 (type·slot·rid)',
         !!(sentG && sentG.slot === gate.zone && sentG.rid), JSON.stringify(sentG));

      /* 🔴 **에코를 기다린다.** ACK 만 보고 완료로 읽지 말라는 것이 socket §7.2 의 요점이다.
         그래서 `completion` 이 지나는 값들과 모듈 `value` 를 **시간순으로** 모은다 —
         이 자료가 REQ-0232 ②(출력 모듈이 둘 이상인 자리의 규칙)에 실물로 답한다. */
      const seq = [];
      for (let i = 0; i < 100; i++) {
        for (const m of rx.slice(rxAt)) {
          if (!m || m.type !== 'state') continue;
          const z = (m.zones || []).find(x => x.id === gate.zone);
          if (!z) continue;
          const line = z.completion + ' [' + (z.modules || []).map(x => (x.name || '?') + '=' + JSON.stringify(x.value) + (x.known ? '' : '?')).join(' ') + ']';
          if (seq[seq.length - 1] !== line) seq.push(line);
        }
        if (seq.some(s => s.startsWith('settled') || s.startsWith('mismatch'))) break;
        await sleep(100);
      }
      const acks = rx.slice(rxAt).filter(m => m && sentG && m.rid === sentG.rid);
      console.log('  · 응답 → ' + JSON.stringify(acks.map(m => m.type + (m.result !== undefined ? '/result=' + m.result : '') + (m.code ? '/' + m.code : ''))));
      console.log('  🔑 completion 이 지난 값 → ' + JSON.stringify(seq));
      ok('⑦ 서버가 응답했다', acks.length > 0, JSON.stringify(acks.map(m => m.type)));
      /* 🔑 **화면 문구**: socket 이 잡은 결함 — 차단봉을 열면 `"예약되었습니다"` 가 나가고 있었다. */
      /* 🔴 **최신 메시지는 `a[0]` 이다.** `showMessage` 가 `insertBefore(box, firstChild)` 로 넣는다 —
         ~~`a[a.length - 1]`~~ 을 읽어서 **가장 오래된 메시지**("서버에 연결되었습니다")를 집었고,
         그것으로 *"성공 경로는 문구를 내지 않는다"* 라고 결론지어 **동료 둘에게 그대로 보고했다.**
         🔑 원장 §5.21 과 **같은 결함**이다(하니스가 엉뚱한 메시지를 읽었다). 그때는 데모 시작 메시지였고
         이번엔 접속 메시지다 — **둘 다 "그 자리에 늘 있는 오래된 줄"** 이다.
         ⚠ 그래서 목록을 읽을 때는 **삽입 방향을 코드로 확인**하고, 가능하면 **내용으로 골라라**(⑤가 그렇게 한다). */
      const msg = await evaluate(client, `(() => { const a = [...document.querySelectorAll('#messages .msg')]; return a.length ? a[0].textContent.slice(0, 140) : null; })()`);
      console.log('  🔑 화면 문구 → ' + JSON.stringify(msg));
      /* 🔴 **앞 판본은 "예약 문구가 아니다"를 단언했고 통과했다 — 문구가 *아예 없어서* 통과했다.**
         `clearCmdPending(rid, false)`(성공)는 문구를 내지 않는다(설계: 피드백은 자리 요약·모듈 값).
         🔑 **부정형 단언은 대상이 없을 때도 참이다** — 오늘 세 번째 헛통과 형태다.
         → 문구 경로는 **미측정**으로 내리고, **있어야 하는 것**을 규칙으로 잰다. */
      /* 🔴 **정정**: 앞 판본은 *"성공 경로는 문구를 내지 않는다"* 로 미측정 처리했다. **틀렸다** —
         목록의 **오래된 쪽**을 읽고 있었다(위 주석). 문구는 나온다:
         `"E1 · 차단봉 열기 — 적용되었습니다 (실제 반영 여부는 다음 스냅샷으로 확인합니다)"`.
         🔑 그래서 **긍정형으로 단언한다** — 그 자리를 가리키고 그 조작을 이름으로 부르는가.
         부정형(*"예약 문구가 아니다"*)만 두면 **문구가 사라져도 초록이다.** */
      ok('⑦ 🔴 차단봉 응답이 그 조작을 이름으로 말한다',
         !!(msg && msg.includes(gate.zone) && /차단봉/.test(msg) && !/예약되었습니다|예약이 취소/.test(msg)),
         JSON.stringify(msg));
      /* 🔑 대신 **자리 요약이 `completion` 을 반영하는가**를 잰다 — 이것이 사용자가 실제로 읽는 것이다.
         값을 박지 않는다: 넷 각각이 **서로 다른 문구**로 나오고 **`unknown` 만 자리 이름**이면 된다. */
      const sumCmp = await (async () => {
        for (let i = 0; i < 3; i++) {
          const w = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
          const z = w ? (w.zones || []).find(x => x.id === gate.zone) : null;
          if (!z) return { skip: 'state 에 그 자리가 없다' };
          const dom = await evaluate(client, `(() => { const e = [...document.querySelectorAll('#zone-grid .zone')].find(n => (n.querySelector('.zone__id')||{}).textContent === ${JSON.stringify(gate.zone)}); return e ? (e.querySelector('.zone__sum')||{}).textContent : null; })()`);
          const neutral = /^(입구|출구)$/.test(dom || '');
          const r = { completion: z.completion, sum: dom, neutral: neutral };
          /* 규칙: `unknown` 이면 자리 이름(중립)이고, 그 밖이면 **중립이 아니어야** 한다. */
          if ((z.completion === 'unknown') === neutral) return r;
          if (i === 2) return r;
          await sleep(300);
        }
      })();
      console.log('  🔑 자리 요약 대 completion → ' + JSON.stringify(sumCmp));
      if (sumCmp && sumCmp.skip) skip('⑦ 자리 요약이 completion 을 반영한다', sumCmp.skip);
      else ok('⑦ 🔴 자리 요약이 completion 을 반영한다 (unknown 만 중립)',
              !!(sumCmp && (sumCmp.completion === 'unknown') === sumCmp.neutral), JSON.stringify(sumCmp));
      if (!seq.length) skip('⑦ 에코로 완료가 판정된다', 'state 가 안 왔다 — 장치 세션을 확인해라');
      else if (!seq.some(s => s.startsWith('settled'))) {
        /* ⚠ `settled` 가 안 왔다는 것을 **실패로 단정하지 않는다** — `mismatch`(장치가 못 움직였다)도
           계약상 정상 응답이고, 그건 **장치 쪽 사실**이지 화면 결함이 아니다. 값을 그대로 보고한다. */
        skip('⑦ 에코가 요청과 일치했다(settled)', '지난 값: ' + JSON.stringify(seq) + ' — mismatch 면 장치가 못 움직인 것이다(화면 결함 아님)');
      } else ok('⑦ 🔴 에코가 요청과 일치해 settled 로 갔다', true, JSON.stringify(seq));

      /* 🔴 **여기가 본체다: 값이 실제로 뒤집혔는가.** `settled` 는 *"요청과 에코가 같다"* 이므로
         **이미 그 상태였으면 아무것도 안 바뀌고도 `settled`** 다(socket 실측: `open_gate` 인데
         비트가 이미 1 이었다). **"명령이 상태를 바꾼다"는 별개의 진술이고 따로 재야 한다.** */
      const after = await (async () => {
        for (let i = 0; i < 40; i++) {
          const w = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
          const z = w ? (w.zones || []).find(x => x.id === gate.zone) : null;
          const m = z ? (z.modules || []).find(x => x && x.known === true && typeof x.value === 'boolean') : null;
          if (m && m.value === gate.wantAfter) return m.value;
          await sleep(150);
        }
        const w = rx.filter(x => x && x.type === 'state').slice(-1)[0] || null;
        const z = w ? (w.zones || []).find(x => x.id === gate.zone) : null;
        const m = z ? (z.modules || []).find(x => x && x.known === true && typeof x.value === 'boolean') : null;
        return m ? m.value : null;
      })();
      console.log('  🔑 에코 전이 → ' + JSON.stringify({ before: gate.valueBefore, want: gate.wantAfter, after: after }));
      /* 🔴 **갈래를 갈라 단언한다.** 거절(`result=3`)에서는 **안 바뀌는 것이 옳다** —
         한 단언으로 두 갈래를 덮으면 거절 시험이 "제품 결함"으로 빨강이 된다(§5.47 의 형태).
         🔑 갈래를 결정하는 것은 **ACK 의 `result`** 이고 내가 짐작하지 않는다. */
      const ackG = rx.slice(rxAt).filter(m => m && m.type === 'ack' && sentG && m.rid === sentG.rid).slice(-1)[0] || null;
      const rG = ackG ? Number(ackG.result) : null;
      if (rG === 0) {
        ok('⑦ 🔴🔴 명령이 장치 상태를 실제로 바꿨다 (에코 비트가 뒤집혔다)',
           after === gate.wantAfter && after !== gate.valueBefore,
           JSON.stringify({ before: gate.valueBefore, want: gate.wantAfter, after: after })
             + ' — 같으면 이미 그 상태였다는 뜻이고 그때 settled 는 아무것도 증명하지 않는다');
      } else if (rG === 3) {
        /* 🔑 거절 갈래 — **상태가 안 바뀌어야** 하고 **문구가 장치 탓이어야** 한다.
           내 표(`TEST_RESULT_TEXT[3]`)가 서버 문구를 덮으면 *"잘못된 자리 또는 값입니다"* 가 뜬다 —
           **장치 실패를 사용자 잘못으로 돌리는 것**이고 그게 §5.71 의 결함이다. */
        ok('⑦ 거절(result=3)이면 상태가 안 바뀐다', after === gate.valueBefore,
           JSON.stringify({ before: gate.valueBefore, after: after }));
        ok('⑦ 🔴 거절 문구가 장치 탓으로 말한다 (사용자 잘못이 아니다)',
           !!(msg && /수행할 수 없습니다/.test(msg) && !/잘못된 자리 또는 값/.test(msg)),
           JSON.stringify(msg) + ' — "잘못된 자리 또는 값입니다" 면 내 표가 서버 문구를 덮은 것이다(§5.71)');
      } else {
        skip('⑦ 에코 전이 판정', 'ACK result 가 0 도 3 도 아니다 → ' + JSON.stringify(rG) + ' · 갈래를 모르면 단언하지 않는다');
      }

      /* ── 미결 갈래: 앞선 요청이 살아 있는 동안 또 누르면 어떻게 되나 ──────────────
         socket 실측: `{"type":"error","code":"pending","message":"지금은 조작할 수 없습니다"}`
         🔴 **내 아이들 1초가 이 갈래를 사용자에게서 구조적으로 막을 수 있다** — 아이들이 풀릴 때쯤
         서버 쪽 미결도 풀려 있으면 **사용자는 이 거절을 만날 수 없다.**
         🔑 그러면 그 사실 자체가 결과다(문구가 도달 불가라는 뜻). **못 밟으면 미측정으로 적는다.** */
      const txAt2 = tx.length, rxAt2 = rx.length;
      for (let i = 0; i < 30; i++) {
        const left = await evaluate(client, `(() => { try { return cooldownLeftMs(); } catch (e) { return 0; } })()`).catch(() => 0);
        if (!left || left <= 0) break;
        await sleep(50);
      }
      /* 🔴 **다른 자리에서 한다.** 앞선 자리(E1)는 이미 `settled` 를 기다리느라 2.8초가 흘렀고
         **그 사이 서버 미결이 풀렸다**(실측: 재요청이 `queued` 로 접수됐다).
         🔑 겹치게 하려면 **정착을 기다리지 않아야** 하므로, 손대지 않은 자리(X1)에서
         **보내고 → 아이들만 풀리면 → 곧바로 다시** 를 한다. 그러면 E1 자료도 안 흔든다. */
      const other = await evaluate(client, `(() => {
        const b = [...document.querySelectorAll('#zone-grid .zbtn')].find(x => x.dataset.zone !== ${JSON.stringify(gate.zone)}
          && (x.dataset.act === 'open_gate' || x.dataset.act === 'close_gate') && x.getAttribute('aria-disabled') === 'false');
        if (!b) return null; b.click(); return { zone: b.dataset.zone, act: b.dataset.act };
      })()`);
      if (other) {
        actLog('미결 시험 1차 — ' + other.zone + ' / ' + other.act);
        for (let i = 0; i < 40; i++) { const d3 = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false); if (d3) break; await sleep(50); }
        await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);
      }
      const again = !other ? null : await (async () => {
        /* 아이들만 기다린다 — **정착은 기다리지 않는다.** 그게 이 시험의 요점이다. */
        for (let i = 0; i < 40; i++) {
          const left = await evaluate(client, `(() => { try { return cooldownLeftMs(); } catch (e) { return 0; } })()`).catch(() => 0);
          if (!left || left <= 0) break;
          await sleep(40);
        }
        return await evaluate(client, `(() => {
          const b = [...document.querySelectorAll('#zone-grid .zbtn')].find(x => x.dataset.zone === ${JSON.stringify(other.zone)}
            && (x.dataset.act === 'open_gate' || x.dataset.act === 'close_gate') && x.getAttribute('aria-disabled') === 'false');
          if (!b) return null; b.click(); return b.dataset.act;
        })()`);
      })();
      if (!again) skip('⑦ 미결 중 재요청', '아이들이 풀린 뒤에도 누를 수 있는 차단봉 버튼이 없었다');
      else {
        actLog('미결 시험 2차 — ' + (other ? other.zone : '?') + ' / ' + again + ' (정착 기다리지 않고 곧바로)');
        for (let i = 0; i < 40; i++) { const d2 = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false); if (d2) break; await sleep(50); }
        await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);
        await sleep(1500);
        const resp2 = rx.slice(rxAt2).filter(m => m && (m.type === 'error' || m.type === 'ack' || m.type === 'queued'));
        const errs = resp2.filter(m => m.type === 'error');
        const m2 = await evaluate(client, `(() => { const a = [...document.querySelectorAll('#messages .msg')]; return a.length ? a[0].textContent.slice(0, 140) : null; })()`);
        console.log('  🔑 연속 조작 응답 → ' + JSON.stringify(resp2.map(m => m.type + (m.code ? '/' + m.code : '') + (m.result !== undefined ? '/result=' + m.result : ''))));
        console.log('  🔑 그때 화면 문구 → ' + JSON.stringify(m2));
        if (!errs.length) {
          skip('⑦ 미결 중 재요청이 거절된다', '거절이 안 왔다 — 앞선 요청이 이미 풀렸다는 뜻이다(아이들 1초가 이 갈래를 막는다). 응답: ' + JSON.stringify(resp2.map(m => m.type)));
        } else {
          /* 🔴 문구가 **"기다려라"** 여야 한다. `'오류가 발생했습니다'` 나 막연한 문구면
             사용자가 **다시 누른다** — 그게 증폭 고리다(§5.14). 코드도 같이 찍어 둔다. */
          console.log('  🔑 거절 코드 → ' + JSON.stringify(errs.map(m => m.code)));
          ok('⑦ 🔴 미결 중 재요청 문구가 "기다려라"로 말한다',
             !!(m2 && /처리 중|결과가 나온 뒤/.test(m2) && !/오류가 발생했습니다/.test(m2)),
             JSON.stringify({ codes: errs.map(m => m.code), msg: m2 }));
        }
      }
    }
  }

  if (acted.length) console.log('\n  🕐 이번 실행에서 한 조작 ' + acted.length + '건:\n     ' + acted.join('\n     '));
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
