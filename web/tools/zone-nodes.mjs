/**
 * 🔴 **박스 안의 박스** — 영역 박스 > 노드 박스 > 모듈 리스트를 잰다. (REQ-0261)
 *
 * ⚠ **주입이다. 서버를 안 쓴다**(`file://` + `?demo=1` · 트래픽 0 · 하행 0).
 *   전송 계층만 건너뛰고 `handleServerMessage → applyMap/applyZoneState → render` 는 진짜 경로다.
 *
 * 🔑 **주입이라서 오늘 실물이 만들 수 없는 경우를 밟는다** — 지금 등록된 노드는 **한 대**뿐이라
 *   *"한 영역에 노드 둘"* 은 실기에서 재현이 불가능하다. 곧 그 지형이 온다(REQ-0260).
 *   ⚠ **그러므로 이 도구의 초록은 "실기에서 그렇게 나온다"가 아니라 "그 자료가 오면 그렇게 그린다"다.**
 *
 * 검사는 **긍정형·집합 대조**다(루트 지시 · 원장 §5.85):
 *   ❌ "노드 박스가 있다"  → 하나만 그려도 통과한다
 *   ✅ "영역의 노드 박스 devid 집합 **==** 그 영역 modules 의 devid 집합"
 *
 * 사용: node web/tools/zone-nodes.mjs            (--head 로 창을 본다)
 */
import { launch, evaluate, sleep } from './cdp.mjs';
import { assertServedIsCurrent } from './screen-build.mjs';

const HEAD = process.argv.includes('--head');
/* `--file <경로>` 로 다른 화면을 겨눌 수 있다. 🔑 **음성 대조용**이다 —
   개정 전 화면(예: 배포본)에 대고 돌리면 이 검사들이 **실제로 빨강이 되는지** 확인할 수 있다.
   검사가 실패할 수 없으면 그 초록은 아무 말도 안 한다(원장 §5.85 계열). */
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
/* 🔴 `--live <포트>` — **주입하지 않는다.** 실기 서버에 붙어 **서버가 실제로 보낸 `map`** 을
   계약으로 삼고 DOM 과 대조한다. 주입 모드의 초록은 *"그 자료가 오면 그렇게 그린다"* 이고,
   이 모드의 초록은 *"지금 실기에서 그렇게 나온다"* 다 — **다른 진술이라 따로 재야 한다.**
   ⚠ 클릭·하행 없음. 순수 관측이다. */
const LIVE = (() => { const i = process.argv.indexOf('--live'); return i >= 0 ? process.argv[i + 1] : null; })();
const URL_ = LIVE ? ('http://127.0.0.1:' + LIVE + '/index.html')
                  : ((fileArg ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg)).href
                              : new URL('../../조별과제샘플/web/index.html', import.meta.url).href) + '?demo=1');

let pass = 0, fail = 0, skipped = 0;
const ok = (n, c, d) => { if (c) { pass++; console.log('  ✅ ' + n); } else { fail++; console.log('  ❌ ' + n + (d ? '\n       → ' + d : '')); } };
/* 못 잰 것을 통과로도 실패로도 두지 않는다(원장 §5.40). */
const skip = (n, why) => { skipped++; console.log('  ⏭ ' + n + '  → 측정 불가: ' + why); };
const inject = (frame) => `(() => { handleServerMessage(${JSON.stringify(frame)}); return true; })()`;
const setEq = (a, b) => a.length === b.length && a.every((x) => b.includes(x));
const S = (arr) => JSON.stringify(arr.slice().sort());

/* ── 주입할 지형 ──────────────────────────────────────────────────────
   🔴 **퇴화형(영역당 노드 1대)으로만 시험하지 않는다.** 곧 오는 지형을 같이 넣는다:
     Z1 ← P1(A1,B1) + P2(C1)   **한 영역에 노드 둘** (REQ-0260 의 모양)
     Z2 ← P1(D1)               노드 하나 (지금 지형)
     Z3 ← 모듈 0개             등록 전·결속 끊김
   ⚠ `idx`/`value`/`known` 은 **서버가 실제로 내는 이름 그대로**다. 지어낸 필드가 하나도 없다. */
let MAP = {
  type: 'map', srv_id: 'T-1', epoch: 3, grid: { rows: 2, cols: 3 },
  zones: [
    { id: 'Z1', kind: 'parking', cells: [[0, 0]], modules: [
      { devid: 'P2', name: 'C1', kind: 'IP', idx: 2 },
      { devid: 'P1', name: 'A1', kind: 'IP', idx: 0 },
      { devid: 'P1', name: 'B1', kind: 'IP', idx: 1 } ] },
    { id: 'Z2', kind: 'entrance', cells: [[0, 1]], modules: [
      { devid: 'P1', name: 'D1', kind: 'OB', idx: 3 } ] },
    { id: 'Z3', kind: 'parking', cells: [[0, 2]], modules: [] },
    /* 🔴 **cells 가 둘인 자리** — socket 이 길이 강제를 풀면 실재한다(REQ-0269).
       옛 코드는 이것을 **칸마다 통째로 다시 그려** 예약 버튼이 두 개가 됐다. */
    { id: 'A1', kind: 'parking', cells: [[1, 0], [1, 1]], modules: [
      { devid: 'P1', name: 'A1', kind: 'IP', idx: 0 } ] },
  ],
};
const ST = {
  type: 'state', srv_id: 'T-1', epoch: 3, ts_ms: 1755500000123,
  zones: [
    { id: 'Z1', occupied: true, reserved: false, actions: {}, completion: 'unknown', modules: [
      { devid: 'P1', name: 'A1', idx: 0, value: true,  known: true },
      { devid: 'P1', name: 'B1', idx: 1, value: false, known: true },
      /* 🔴 둘째 노드는 지금 서버가 값을 못 채운다(`park.devid` 하나만 본다) → `known:false`.
         **그 상태가 화면에서 ⚪ 모름으로 그려지는지**가 이 검사의 핵심 하나다. */
      { devid: 'P2', name: 'C1', idx: -1, value: null, known: false } ] },
    { id: 'Z2', actions: {}, completion: 'settled', modules: [
      { devid: 'P1', name: 'D1', idx: 3, value: true, known: true } ] },
    { id: 'Z3', occupied: false, reserved: false, actions: {}, completion: 'unknown', modules: [] },
    { id: 'A1', occupied: false, reserved: false, completion: 'unknown',
      actions: { reserve: { ok: true } }, modules: [
      { devid: 'P1', name: 'A1', idx: 0, value: false, known: true } ] },
  ],
};

const readZones = `(() => {
  const out = {};
  for (const z of document.querySelectorAll('#zone-grid .zone')) {
    /* 🔑 dataset.zone 으로 센다. .zone__id 의 글자를 키로 쓰면 표시가 바뀔 때 조용히 깨진다.
       (역따옴표 금지 — 이 문자열 자체가 템플릿 리터럴이다. 원장 5.49) */
    const id = z.dataset.zone;
    out[id] = (out[id] ? (out[id].dup = (out[id].dup || 1) + 1, out[id]) : {
      dup: 1,
      name: (z.querySelector('.zone__name') || {}).textContent || null,
      idText: (z.querySelector('.zone__id') || {}).textContent || null,
      nodes: [...z.querySelectorAll('.znode:not(.znode--empty)')].map(n => ({
        devid: n.dataset.devid,
        headText: (n.querySelector('.znode__id') || {}).textContent,
        mods: [...n.querySelectorAll('.zmod')].map(m => ({
          text: m.textContent,
          lamp: (m.querySelector('.zlamp') || {}).dataset ? m.querySelector('.zlamp').dataset.state : null,
          lampLabel: m.querySelector('.zlamp') ? m.querySelector('.zlamp').getAttribute('aria-label') : null,
        })),
      })),
      emptyBoxes: z.querySelectorAll('.znode--empty').length,
      emptyText: (z.querySelector('.znode--empty') || {}).textContent || null,
      reserveBtns: z.querySelectorAll('.zbtn[data-act="reserve"]').length,
    });
  }
  return { zones: out, cont: document.querySelectorAll('#zone-grid .zcell--cont').length };
})()`;

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

  if (LIVE) {
    /* 🔴 판본을 먼저 확인한다 — 낡은 화면을 재면 남의 판을 재고 내 것이라고 보고한다(원장 §5.85). */
    await assertServedIsCurrent(URL_);
    let link = null;
    for (let i = 0; i < 150; i++) {
      link = await evaluate(client, `state.link`).catch(() => null);
      if (link === 'ws') break;
      await sleep(100);
    }
    ok('실제 WS 로 붙었다 (link=' + link + ')', link === 'ws', '데모/폴백이면 서버 자료가 아니다');
    if (link !== 'ws') throw new Error('WS 실패');
    let live = null;
    for (let i = 0; i < 100; i++) {
      live = await evaluate(client, `state.map`).catch(() => null);
      if (live && Array.isArray(live.zones) && live.zones.length) break;
      await sleep(100);
    }
    ok('서버가 map 을 보냈다', !!(live && Array.isArray(live.zones) && live.zones.length),
       '받은 것: ' + JSON.stringify(live));
    if (!live || !live.zones || !live.zones.length) throw new Error('map 없음');
    MAP = live;                                   /* 🔑 계약은 **서버가 보낸 것**이다 */
    /* 🔴 `srv_id` 는 **화면의 `state.srvId`** 에서 읽는다. `state.map` 객체에는 안 담긴다 —
       여기서 `live.srv_id` 를 찍었더니 `undefined` 가 나와 **서버가 안 보낸다고 오해할 뻔했다.**
       전선 프레임에는 멀쩡히 있다(확인함). **계측기가 못 본 것을 서버가 안 한 것으로 읽는 형태**(§5.30). */
    const srvId = await evaluate(client, `state.srvId`).catch(() => null);
    console.log('  · 실기 map: srv_id=' + srvId + ' epoch=' + live.epoch
              + ' 영역 ' + live.zones.length + '개 · 모듈 '
              + live.zones.reduce((a, zz) => a + (zz.modules || []).length, 0) + '개\n');
  } else {
    await evaluate(client, inject(MAP));
    await evaluate(client, inject(ST));
  }
  await sleep(150);

  const usable = await evaluate(client, `mapUsable()`);
  ok('개정 격자가 켜졌다 (map 이 쓰인다)', usable === true, '옛 slots[] 경로면 이 검사는 무의미하다');
  if (usable !== true) throw new Error('격자가 안 켜졌다');

  /* 🔑 **기대 표를 하니스가 따로 갖지 않는다** — 화면의 ZONE_LABEL 을 읽어 온다.
     여기에 복사본을 두면 판정자가 둘이 되고, 표를 고칠 때 한쪽만 고치는 날이 온다. */
  if (process.argv.includes('--drop-labels')) {
    /* 🔴 **음성 대조**: 라벨 표를 비우면 정말 빨강이 되는가(REQ-0269 §4).
       ⚠ 여기에 함정이 있다 — 기대 집합을 **화면에서 읽으므로** 표를 비우면 기대도 비어
       *"집합 == 집합"* 이 **공허하게 통과**한다. 그래서 **표가 비어 있지 않다**를 따로 단언한다.
       🔑 이 플래그가 그 단언이 실제로 작동하는지 재는 자리다. */
    await evaluate(client, `(() => { for (const k of Object.keys(ZONE_LABEL)) delete ZONE_LABEL[k]; renderZoneGrid(); return true; })()`);
    console.log('  ⚠ --drop-labels: ZONE_LABEL 을 비웠다. **여기서 빨강이 나와야 정상이다.**\n');
  }
  const LABELS = await evaluate(client, `(typeof ZONE_LABEL === 'object' && ZONE_LABEL) || {}`);
  ok('화면에 ZONE_LABEL 표가 있다', LABELS && Object.keys(LABELS).length > 0, JSON.stringify(LABELS));
  const _read = await evaluate(client, readZones);
  const z = _read.zones;

  /* ── ① 노드 그룹핑 — 집합 대조 (긍정형) ───────────────────────────── */
  const expect = {};
  for (const zone of MAP.zones) {
    const m = {};
    for (const mod of zone.modules) (m[mod.devid] = m[mod.devid] || []).push(mod.name);
    expect[zone.id] = m;
  }
  for (const zid of Object.keys(expect)) {
    const want = Object.keys(expect[zid]);
    const got = (z[zid] ? z[zid].nodes.map((n) => n.devid) : []);
    ok('🔴 ' + zid + ' 의 노드 박스 집합 == modules 의 devid 집합  ' + S(got) + ' == ' + S(want),
       setEq(got, want), '그렸다: ' + S(got) + ' · 계약: ' + S(want));

    for (const dv of want) {
      const box = z[zid] && z[zid].nodes.find((n) => n.devid === dv);
      if (!box) { ok(zid + '/' + dv + ' 박스가 있다', false, '박스 자체가 없다'); continue; }
      /* 모듈 행의 이름은 `kind` 라벨이라 원문 `name` 이 안 보일 수 있다 —
         그래서 **텍스트 포함**이 아니라 **개수 + 각 행의 등 상태**로 대조하고,
         이름 대조는 중복 종류일 때만 가능하다는 것을 그대로 적는다. */
      ok(zid + '/' + dv + ' 박스 안 모듈 수 == 계약 수 (' + box.mods.length + ' == ' + expect[zid][dv].length + ')',
         box.mods.length === expect[zid][dv].length,
         JSON.stringify(box.mods.map((m) => m.text)));
      ok(zid + '/' + dv + ' 박스 머리에 노드 ID 가 명시된다', box.headText === dv, '머리글: ' + box.headText);
    }
  }

  /* ── ①-b 🔴 인덱스가 **늘** 보이는가 (REQ-0268 · 사용자 지시) ────────
     ❌ "인덱스가 어딘가 있다" 가 아니라 ✅ **"보이는 #idx(name) 집합 == map 이 준 집합"** 이다.
     🔑 조건부(`둘 이상일 때만`)로 되돌아가면 이 검사가 곧바로 빨강이 된다 — 그게 이 검사의 목적이다. */
  for (const zone of MAP.zones) {
    for (const dv of Object.keys(expect[zone.id])) {
      const box = z[zone.id] && z[zone.id].nodes.find((n) => n.devid === dv);
      if (!box) continue;
      const want = zone.modules.filter((m) => m.devid === dv)
        .map((m) => '#' + m.idx + ' (' + m.name + ')');
      const got = box.mods.map((t) => {
        const mm = t.text.match(/#(\d+|\?)\s*\(([^)]*)\)/);
        return mm ? '#' + mm[1] + ' (' + mm[2] + ')' : null;
      });
      ok('🔴 ' + zone.id + '/' + dv + ' 의 표시 인덱스 집합 == map 의 idx 집합  ' + S(got.filter(Boolean)) + ' == ' + S(want),
         setEq(got.filter(Boolean), want),
         '그렸다: ' + JSON.stringify(box.mods.map((m) => m.text)) + ' · 계약: ' + S(want));
    }
  }

  /* ── ② 노드 정렬 — **모든 영역에서** 사전순인가 (지형에 안 매인다) ── */
  {
    const bad = Object.keys(z).filter((zid) => {
      const o = z[zid].nodes.map((n) => n.devid);
      return JSON.stringify(o) !== JSON.stringify(o.slice().sort());
    });
    ok('🔴 모든 영역에서 노드가 devid 사전순이다 (' + Object.keys(z).length + '개 영역)',
       bad.length === 0, '어긋난 영역: ' + JSON.stringify(bad)
       + ' — 순서를 안 정하면 프레임마다 흔들려 눈이 못 따라간다');
  }
  /* 한 영역에 노드 둘 — **그런 영역이 있을 때만** 단언한다. 없으면 위 ⏭ 가 센다. */
  for (const zone of MAP.zones) {
    const devids = [...new Set((zone.modules || []).map((m) => m.devid))];
    if (devids.length < 2) continue;
    const got = z[zone.id] ? z[zone.id].nodes.length : 0;
    ok('🔴 ' + zone.id + ' 는 노드가 ' + devids.length + '개라 박스가 그만큼 갈린다 (→ ' + got + ')',
       got === devids.length, JSON.stringify(devids));
  }

  /* ── ②-b 🔴 자리를 두 번 그리지 않는가 · 표시 이름 · id 노출 ──────── */
  {
    const dups = Object.keys(z).filter((zid) => (z[zid].dup || 1) > 1);
    ok('🔴 어떤 자리도 두 번 그려지지 않는다 (cells 가 둘이어도)', dups.length === 0,
       '중복: ' + JSON.stringify(dups) + ' — 같은 자리에 예약 버튼이 여러 개 생긴다');
    const multi = MAP.zones.filter((zz) => (zz.cells || []).length > 1).map((zz) => zz.id);
    if (!multi.length) skip('cells 가 둘 이상인 자리', '지금 지형에 없다');
    else for (const zid of multi) {
      ok('🔴 ' + zid + ' (cells ' + (MAP.zones.find((q) => q.id === zid).cells.length) + '칸) 가 한 번만 그려진다',
         z[zid] && (z[zid].dup || 1) === 1, JSON.stringify(z[zid] && z[zid].dup));
      ok(zid + ' 의 예약 버튼이 한 개다', !z[zid] || z[zid].reserveBtns <= 1,
         '버튼 ' + (z[zid] && z[zid].reserveBtns) + '개 — 자리 하나에 예약이 둘이면 사용자가 두 번 건다');
    }
  }
  {
    /* 🔴 표시 이름 — **집합 대조**. "이름이 어딘가 있다"가 아니다. */
    const labeled = Object.keys(z).filter((zid) => z[zid].name);
    const wantLabeled = Object.keys(z).filter((zid) => LABELS[zid]);
    ok('🔴 표시 이름이 붙은 자리 집합 == ZONE_LABEL 이 정의한 집합  ' + S(labeled) + ' == ' + S(wantLabeled),
       setEq(labeled, wantLabeled), '그렸다: ' + S(labeled) + ' · 표: ' + S(wantLabeled));
    const wrong = wantLabeled.filter((zid) => z[zid].name !== LABELS[zid]);
    ok('표시 이름의 글자가 표와 같다', wrong.length === 0,
       JSON.stringify(wrong.map((zid) => zid + ': ' + z[zid].name + ' != ' + LABELS[zid])));
    /* 🔴 id 는 **반드시** 보인다 — 로그와 맞추는 유일한 값이다. */
    const hidden = Object.keys(z).filter((zid) => (z[zid].idText || '').trim() !== zid);
    ok('🔴 모든 자리에서 날 id 가 화면에 그대로 보인다 (' + Object.keys(z).length + '개)',
       hidden.length === 0, '안 보이거나 다른 자리: ' + JSON.stringify(hidden));
  }

  /* ── ③ 모듈 0개 영역 — 정상처럼 그리지 않는가 (있는 만큼 전부) ───── */
  {
    const emptyZones = MAP.zones.filter((zz) => !(zz.modules || []).length).map((zz) => zz.id);
    if (!emptyZones.length) {
      skip('모듈 0개 영역 표시', '지금 지형에 모듈 0개인 영역이 없다');
    } else {
      for (const zid of emptyZones) {
        const zz = z[zid] || {};
        ok('🔴 ' + zid + ' (모듈 0개) 가 "비어 있음"으로 표시된다 (노드 박스 0 + 빈 표시 1)',
           (zz.nodes || []).length === 0 && zz.emptyBoxes === 1, JSON.stringify(zz));
        ok(zid + ' 의 빈 표시가 원인을 단정하지 않는다 (등록 전/결속 끊김 둘 다 말한다)',
           typeof zz.emptyText === 'string' && zz.emptyText.includes('등록') && zz.emptyText.includes('결속'),
           '문구: ' + zz.emptyText);
      }
    }
  }

  /* ── ④ 상태등 셋 — known 을 그대로 옮기는가 ──────────────────────── */
  const lampOf = (zid, dv, i) => {
    const b = z[zid] && z[zid].nodes.find((n) => n.devid === dv);
    return b && b.mods[i] ? b.mods[i].lamp : null;
  };
  if (LIVE) {
    /* 🔴 실기에서는 **서버가 보낸 `known` 과 등이 일치하는가**를 전수로 본다.
       ⚠ 값 축(`value`)과 섞이지 않는지가 핵심이다 — `value:false` 인데 등이 회색이면 축이 섞인 것이다. */
    const liveSt = await evaluate(client, `(() => {
      const out = {};
      const zs = state.zoneState && state.zoneState.zones;
      if (Array.isArray(zs)) for (const zz of zs) out[zz.id] = (zz.modules || []).map(m => ({ devid: m.devid, name: m.name, known: m.known, value: m.value }));
      return out;
    })()`).catch(() => null);
    let n = 0, mism = [], noRow = [];
    for (const zid of Object.keys(liveSt || {})) {
      for (const sm of liveSt[zid]) {
        const b = z[zid] && z[zid].nodes.find((x) => x.devid === sm.devid);
        if (!b) continue;
        /* 🔴 **그 모듈의 행 하나**와 맞춘다. 예전에는 박스 안 **모든 행**과 맞춰서
           자리당 모듈이 둘이면 검사 수가 제곱으로 불었다(모듈 12개인데 22행이 나왔다) —
           그리고 **A 의 known 을 B 의 등과 비교**하므로 둘의 known 이 다르면 거짓 실패가 난다.
           🔑 분모를 세 보고 잡았다: **통과 수는 밟은 것의 수이지 분모가 아니다**(원장 §5.40). */
        const row = b.mods.find((t) => (t.text || '').includes('(' + sm.name + ')'));
        if (!row || row.lamp === null) { noRow.push(zid + '/' + sm.devid + '/' + sm.name); continue; }
        n += 1;
        if (sm.known === true && row.lamp !== 'ok') mism.push(zid + '/' + sm.name + ' known=true 인데 등=' + row.lamp);
        if (sm.known !== true && row.lamp === 'ok') mism.push(zid + '/' + sm.name + ' known=false 인데 등=ok');
      }
    }
    /* 🔴 **분모를 같이 단언한다** — 검사한 행 수가 서버가 준 모듈 수와 같아야 한다.
       같지 않으면 어떤 모듈은 아예 안 밟힌 것이고, 그 초록은 그만큼 거짓이다. */
    const totalMods = Object.values(liveSt || {}).reduce((a, v) => a + v.length, 0);
    ok('🔴 실기: 모든 모듈의 등이 서버 `known` 과 일치한다 (' + n + '행)', mism.length === 0, JSON.stringify(mism));
    ok('🔴 검사한 행 수 == 서버가 준 모듈 수 (' + n + ' == ' + totalMods + ')', n === totalMods,
       '못 찾은 행: ' + JSON.stringify(noRow) + ' — 분모가 안 맞으면 위 초록은 그만큼 거짓이다');
    if (!n) skip('실기 등 대조', '상태 모듈이 0행이다 — 장치가 안 붙었거나 state 가 비었다');
  }
  if (!LIVE) ok('🔴 known:true 인 모듈의 등이 🟢 정상이다 (Z1/P1)', lampOf('Z1', 'P1', 0) === 'ok', '등: ' + lampOf('Z1', 'P1', 0));
  if (!LIVE) ok('🔴 known:false 인 모듈의 등이 ⚪ 모름이다 (Z1/P2 — 녹색도 적색도 아니다)',
     lampOf('Z1', 'P2', 0) === 'unknown',
     '등: ' + lampOf('Z1', 'P2', 0) + ' — 🔴 모름을 녹/적으로 칠하면 "0 이 건강인지 못 셈인지"를 못 가른다');
  /* 🔑 **`value:false` 와 `known:false` 는 다른 축이다.** B1 은 값이 false 지만 **값을 받고 있다** —
     등은 🟢 여야 하고 값 기호만 ○ 다. 이 둘을 한 칸에 합치면 "차 없음"이 "모듈 죽음"으로 보인다. */
  if (!LIVE) ok('🔴 value:false 인데 known:true 면 등은 🟢 다 (값 축과 상태 축이 안 섞인다 · Z1/P1 의 B1)',
     lampOf('Z1', 'P1', 1) === 'ok', '등: ' + lampOf('Z1', 'P1', 1));
  const lbl = (() => { const b = z.Z1 && z.Z1.nodes.find((n) => n.devid === 'P2'); return b && b.mods[0] ? b.mods[0].lampLabel : null; })();
  if (!LIVE) ok('등이 색만으로 뜻을 나르지 않는다 (접근 이름에 문장이 있다)',
     typeof lbl === 'string' && lbl.includes('모름'), '접근 이름: ' + lbl);

  if (!LIVE) {
    skip('실기에서 이 지형이 그렇게 온다', '주입 모드다 — 이 초록은 "그 자료가 오면 그렇게 그린다"이지 '
       + '"실기에서 그렇게 나온다"가 아니다. `--live <포트>` 로 따로 재라');
  } else {
    /* 🔴 실기에서 **무엇을 못 밟았는지**를 세어 둔다 — 통과 수는 분모가 아니다(§5.40). */
    const twoNode = MAP.zones.filter((zz) => new Set((zz.modules || []).map((m) => m.devid)).size > 1).length;
    if (!twoNode) skip('한 영역에 노드 둘 (실기)', '지금 지형에 그런 영역이 없다 — 주입 모드에서만 밟힌다');
  }

} catch (e) {
  console.log('\n💥 중단: ' + (e && e.message ? e.message : String(e)));
  fail += 1;
} finally {
  if (client) await client.close().catch(() => {});
}

console.log('\n' + '─'.repeat(60));
console.log('  ' + pass + ' pass / ' + fail + ' fail / ' + skipped + ' 미측정');
process.exit(fail > 0 ? 1 : 0);
