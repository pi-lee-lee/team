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
    /* 🔑 `label` 이 오면 **내 ZONE_LABEL 표(A1→영역1)를 이겨야** 한다 — 정본은 기여자 선언이다. */
    /* 🔴 **겹침** — `Z3` 과 같은 칸([0,2])을 쓴다. SPEC-assembly-v2 ④: 서버는 막지 않는다.
       옛 코드는 나중 것이 앞 것을 **소리 없이 덮었다**. 이제 가려진 것이 화면에 적혀야 한다. */
    { id: 'OV', kind: 'area', label: '겹친 자리', cells: [[0, 2]], modules: [] },
    { id: 'A1', kind: 'parking', label: '1번 자리', cells: [[1, 0], [1, 1]], modules: [
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

/* 🔴 **자리 상세는 우측 패널에 있다** (REQ-0288 - 격자 칸은 요약만 그린다).
   그래서 자리마다 **그 칸을 눌러** 패널을 그린 뒤 읽는다. 누르는 것은 앱의 선택 경로 그대로다 -
   두 번째 경로를 만들면 그것이 곧 두 번째 판정자다.
   ⚠ 클릭이 격자를 통째로 다시 만들므로 **NodeList 를 들고 순회하면 안 된다.** id 목록을 먼저 뜨고
   매번 다시 찾는다. (역따옴표 금지 - 이 문자열 자체가 템플릿 리터럴이다. 원장 5.49) */
const readZones = `(() => {
  const grid = document.getElementById('zone-grid');
  const panel = document.getElementById('zone-detail');
  /* 🔴 없으면 **왜 없는지 말한다.** null 을 그냥 들고 가면 다음 사람이 TypeError 를 보고
     '하니스가 깨졌다' 로 읽는다. 실제 원인은 '이 화면이 REQ-0288 이전 판' 이다. */
  if (!panel) throw new Error('이 화면에 #zone-detail 패널이 없다 - REQ-0288 이전 판이다(격자 칸에 모듈이 들어 있던 판). 배포/사본을 갱신해라');
  const cellOf = (id) => [...grid.querySelectorAll('.zone')].find(z => z.dataset.zone === id);
  const ids = [...grid.querySelectorAll('.zone')].map(z => z.dataset.zone);
  const out = {};
  for (const id of ids) {
    if (out[id]) { out[id].dup = (out[id].dup || 1) + 1; continue; }
    const cell = cellOf(id);
    if (!cell) continue;
    cell.click();                          /* 이 자리를 패널에 띄운다 */
    out[id] = {
      dup: 1,
      /* 이름·id·배지·겹침 경고는 **칸**이 그린다 */
      name: (cell.querySelector('.zone__name') || {}).textContent || null,
      idText: (cell.querySelector('.zone__id') || {}).textContent || null,
      badgeText: (cell.querySelector('.zone__badge') || {}).textContent || null,
      overlapText: (cell.querySelector('.zone__overlap') || {}).textContent || null,
      /* 노드 박스·모듈 행·조작 버튼은 **패널**이 그린다 */
      nodes: [...panel.querySelectorAll('.znode:not(.znode--empty)')].map(n => ({
        devid: n.dataset.devid,
        headText: (n.querySelector('.znode__id') || {}).textContent,
        mods: [...n.querySelectorAll('.zmod')].map(m => ({
          text: m.textContent,
          lamp: (m.querySelector('.zlamp') || {}).dataset ? m.querySelector('.zlamp').dataset.state : null,
          lampLabel: m.querySelector('.zlamp') ? m.querySelector('.zlamp').getAttribute('aria-label') : null,
        })),
      })),
      emptyBoxes: panel.querySelectorAll('.znode--empty').length,
      emptyText: (panel.querySelector('.znode--empty') || {}).textContent || null,
      reserveBtns: panel.querySelectorAll('.zbtn[data-act="reserve"]').length,
    };
  }
  return { zones: out, cont: grid.querySelectorAll('.zcell--cont').length };
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
  /* 🔴 **겹쳐서 가려진 자리는 안 그려지는 것이 정상이다**(SPEC-assembly-v2 ④).
     아래 대조들에서 빼지 않으면 **제품이 옳은데 검사가 빨강**이 된다 — 계측기가 틀린 것이다. */
  const _hidden = (() => {
    const seen = new Map(), out = [];
    for (const zz of MAP.zones) for (const cc of (zz.cells || [])) {
      const k = cc[0] + ',' + cc[1];
      if (seen.has(k)) { out.push(zz.id); break; } else seen.set(k, zz.id);
    }
    return out;
  })();
  const visible = (list) => list.filter((id) => !_hidden.includes(id));

  const _read = await evaluate(client, readZones);
  const z = _read.zones;

  /* ── ① 노드 그룹핑 — 집합 대조 (긍정형) ───────────────────────────── */
  const expect = {};
  for (const zone of MAP.zones) {
    const m = {};
    for (const mod of zone.modules) (m[mod.devid] = m[mod.devid] || []).push(mod.name);
    expect[zone.id] = m;
  }
  for (const zid of visible(Object.keys(expect))) {
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
    const srvLabelAll = {};
    for (const zz of MAP.zones) if (typeof zz.label === 'string' && zz.label) srvLabelAll[zz.id] = zz.label;
    const labeled = Object.keys(z).filter((zid) => z[zid].name);
    const wantLabeled = Object.keys(z).filter((zid) => LABELS[zid] || srvLabelAll[zid]);
    ok('🔴 표시 이름이 붙은 자리 집합 == ZONE_LABEL 이 정의한 집합  ' + S(labeled) + ' == ' + S(wantLabeled),
       setEq(labeled, wantLabeled), '그렸다: ' + S(labeled) + ' · 표: ' + S(wantLabeled));
    /* 🔴 `zone.label` 이 온 자리는 **그것이 정본**이고 표를 이긴다. 나머지는 표를 쓴다. */
    const srvLabel = {};
    for (const zz of MAP.zones) if (typeof zz.label === 'string' && zz.label) srvLabel[zz.id] = zz.label;
    const wrong = wantLabeled.filter((zid) => z[zid].name !== (srvLabel[zid] || LABELS[zid]));
    ok('표시 이름의 글자가 정본과 같다 (zone.label 이 있으면 그것, 없으면 ZONE_LABEL)', wrong.length === 0,
       JSON.stringify(wrong.map((zid) => zid + ': ' + z[zid].name + ' != ' + (srvLabel[zid] || LABELS[zid]))));
    /* 🔴 **보이는 자리 중에서 고른다.** 아무거나 첫째를 고르면 그 선택이 **남의 상태에 딸린다** —
       실제로 겹쳐서 가려진 자리(OV)를 골라 빨강이 났다. 제품이 아니라 계측기가 틀린 것이었다
       (CLAUDE.md §"하니스가 '첫 번째 것'을 고르면 그 선택 자체가 오염될 수 있다"). */
    const pickable = visible(Object.keys(srvLabel));
    if (pickable.length) {
      const zid = pickable[0];
      ok('🔴 zone.label 이 내 ZONE_LABEL 표를 이긴다 (' + zid + ' → "' + srvLabel[zid] + '", 표는 "' + (LABELS[zid] || '없음') + '")',
         z[zid] && z[zid].name === srvLabel[zid], z[zid] && z[zid].name);
    } else skip('zone.label 우선순위', '이 지형에 zone.label 이 없다');
    /* 🔴 id 는 **반드시** 보인다 — 로그와 맞추는 유일한 값이다. */
    const hidden = Object.keys(z).filter((zid) => (z[zid].idText || '').trim() !== zid);
    ok('🔴 모든 자리에서 날 id 가 화면에 그대로 보인다 (' + Object.keys(z).length + '개)',
       hidden.length === 0, '안 보이거나 다른 자리: ' + JSON.stringify(hidden));
  }

  /* ── ③ 모듈 0개 영역 — 정상처럼 그리지 않는가 (있는 만큼 전부) ───── */
  {
    const emptyZones = visible(MAP.zones.filter((zz) => !(zz.modules || []).length).map((zz) => zz.id));
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

  /* ── ④-b 🔴 **칸이 겹치면 가려진 자리를 화면이 말한다** (SPEC-assembly-v2 ④)
     ⚠ **부정형으로 쓰지 않는다** — "안 사라졌다"는 자리 자체가 없어도 참이다.
     ✅ **가려진 자리 id 가 화면 글자에 그대로 나온다**로 쓴다. */
  {
    const seen = new Map();
    const hiddenIds = [];
    for (const zz of MAP.zones) for (const cc of (zz.cells || [])) {
      const k = cc[0] + ',' + cc[1];
      if (seen.has(k)) hiddenIds.push(zz.id); else seen.set(k, zz.id);
    }
    if (!hiddenIds.length) skip('칸 겹침 표시', '이 지형에 겹치는 칸이 없다');
    else {
      const txt = Object.keys(z).map((k) => z[k].overlapText).filter(Boolean).join(' | ');
      ok('🔴 가려진 자리 id 가 화면에 적힌다 (' + S(hiddenIds) + ')',
         hiddenIds.every((id) => txt.includes(id)),
         '화면 글자: ' + JSON.stringify(txt) + ' — 조용히 덮으면 기여자가 "내 자리가 왜 안 보이지"를 못 푼다');
      ok('겹침 경고가 분모 0 이 아니다 (' + hiddenIds.length + '건)', hiddenIds.length > 0 && !!txt);
    }
  }

  /* ── ⑤ 🔴 게이트 조작은 **제거됐다**(사용자 확정 2026-08-20 · control 로 통일)
     ⚠ **"차단봉 버튼이 없다"로 쓰지 않는다** — 부정형은 자리 자체가 없어도 참이 된다.
     ✅ **그려진 버튼 집합 == 서버 actions 중 화면이 아는 것** 으로 쓴다(동일성).
        서버가 `open_gate` 를 계속 보내도 화면이 모르는 조작이므로 집합에서 빠진다.
     🔑 이렇게 쓰면 **누가 실수로 되살리면 곧바로 빨강**이 된다. 그게 이 검사의 목적이다. */
  if (!LIVE) {
    const GATE = {
      type: 'state', srv_id: 'T-1', epoch: 3, ts_ms: 1787200000001,
      zones: [{ id: 'Z2', occupied: false, reserved: false, completion: 'unknown',
        actions: { reserve: { ok: true, reason: null },
                   open_gate: { ok: true, reason: null },
                   close_gate: { ok: true, reason: null } },
        modules: [{ devid: 'P1', name: 'D1', idx: 3, value: true, known: true }] }],
    };
    await evaluate(client, inject(GATE));
    await sleep(120);
    /* 🔴 조작 버튼은 **패널**에 있다(REQ-0288). 그 자리를 먼저 선택해야 그려진다. */
    const acts = await evaluate(client, `(() => {
      const cell = [...document.querySelectorAll('#zone-grid .zone')].find(e => e.dataset.zone === 'Z2');
      if (!cell) return null;
      cell.click();
      const panel = document.getElementById('zone-detail');
      return [...panel.querySelectorAll('.zbtn')].map(b => b.dataset.act);
    })()`);
    ok('🔴 서버가 open_gate/close_gate 를 보내도 그려지는 버튼 집합은 {reserve} 뿐이다  ' + S(acts || []),
       Array.isArray(acts) && setEq(acts, ['reserve']),
       '그렸다: ' + S(acts || []) + ' — 게이트 조작은 제거됐다(control 로 통일). '
       + '되살아났으면 ACTION_LABEL 주석을 읽어라: 서버는 닫기=0, 장치는 닫기=2 로 갈렸다');
  }

  /* ── 🔴 REQ-0288 의 합격선 — **모듈이 몇 개든 칸이 안 변한다** ──────────
     사용자가 실물에서 관측한 결함이다: 모듈 많은 자리 하나가 grid 행 전체를 늘렸다.
     🔑 긍정형 + 분모로 쓴다: 칸이 둘 이상 그려졌고, 그 높이가 **같다**. */
  if (!LIVE) {
    const fatMods = [];
    for (let i = 0; i < 10; i++) fatMods.push({ devid: 'P1', name: 'M' + i, kind: 'IP', idx: i });
    const FAT = {
      type: 'map', srv_id: 'T-1', epoch: 9, grid: { rows: 1, cols: 2 },
      zones: [
        { id: 'FA', kind: 'parking', cells: [[0, 0]], modules: fatMods },
        { id: 'FB', kind: 'parking', cells: [[0, 1]], modules: [{ devid: 'P1', name: 'N0', kind: 'IP', idx: 0 }] },
      ],
    };
    await evaluate(client, inject(FAT));
    await sleep(160);
    const cells = await evaluate(client, `(() => {
      return [...document.querySelectorAll('#zone-grid .zone')].map((c) => {
        const r = c.getBoundingClientRect();
        return { id: c.dataset.zone, h: Math.round(r.height), w: Math.round(r.width),
                 badge: (c.querySelector('.zone__badge') || {}).textContent || null,
                 mods: c.querySelectorAll('.zmod').length,
                 tag: c.tagName };
      });
    })()`);
    const list = Array.isArray(cells) ? cells : [];
    ok('분모: 칸이 둘 이상 그려졌다 (' + list.length + ')', list.length >= 2, JSON.stringify(list));
    ok('🔴 모듈 10개 자리와 1개 자리의 **칸 높이가 같다** (REQ-0288 합격선)',
       list.length >= 2 && list.every((b) => b.h === list[0].h),
       JSON.stringify(list) + ' — 다르면 행 높이가 다시 내용에 딸린 것이다(grid-auto-rows 를 봐라)');
    ok('🔴 칸에는 모듈 행이 하나도 없다 — 요약만 그린다',
       list.length >= 2 && list.every((b) => b.mods === 0), JSON.stringify(list));
    ok('모듈 수 배지가 칸에 있다 (10 / 1)',
       list.length >= 2 && /10/.test(list[0].badge || '') && /1/.test(list[1].badge || ''),
       JSON.stringify(list.map((b) => b.badge)));
    ok('🔴 칸이 button 이다 (div 로 버튼을 만들지 않는다)',
       list.length >= 2 && list.every((b) => b.tag === 'BUTTON'), JSON.stringify(list.map((b) => b.tag)));
    /* 지형이 갈리면 선택이 사라진다 — 그때 **첫 자리로 떨어지는지**를 같이 본다(패널이 비면 조작이 사라진다). */
    const sel = await evaluate(client, `(() => {
      const c = document.querySelector('#zone-grid .zone[aria-current="true"]');
      const panel = document.getElementById('zone-detail');
      return { sel: c ? c.dataset.zone : null, panelMods: panel.querySelectorAll('.zmod').length };
    })()`);
    ok('🔴 옛 선택이 사라지면 첫 자리로 떨어진다 (FA) · 패널에 모듈 10행',
       sel && sel.sel === 'FA' && sel.panelMods === 10, JSON.stringify(sel));
  }

  /* ── 🔴 REQ-0293 — `map.zones[].active` 를 화면이 어떻게 그리나 ────────────
     계약: `active = {ok, reason}` · **계산 주체는 서버**(화면이 modules.length 를 세지 않는다).
     🔑 그래서 이 시험은 **`modules` 와 `active` 를 일부러 어긋나게** 넣는다 —
        화면이 모듈 수를 세고 있으면 여기서 갈린다. */
  if (!LIVE) {
    const ACT = {
      type: 'map', srv_id: 'T-1', epoch: 11, grid: { rows: 1, cols: 4 },
      zones: [
        { id: 'K1', kind: 'parking', cells: [[0, 0]], active: { ok: true, reason: null },
          modules: [{ devid: 'P1', name: 'A1', kind: 'IP', idx: 0 }] },
        { id: 'K2', kind: 'parking', cells: [[0, 1]], active: { ok: false, reason: 'no_modules' }, modules: [] },
        /* 🔴 모르는 사유 코드 — 조용히 정상으로 떨어뜨리지 않고 원문을 보여야 한다 */
        { id: 'K3', kind: 'parking', cells: [[0, 2]], active: { ok: false, reason: 'wibble_zz' }, modules: [] },
        /* 🔴 `active` 키가 **없는** 자리(옛 서버) — 아무 주장도 하지 않아야 한다.
           ⚠ 그리고 **모듈이 0개인데 active 가 없다** → 화면이 modules 를 센다면 여기서 비활성으로 그린다(오답). */
        { id: 'K4', kind: 'parking', cells: [[0, 3]], modules: [] },
      ],
    };
    await evaluate(client, inject(ACT));
    await evaluate(client, inject({ type: 'state', srv_id: 'T-1', epoch: 11, ts_ms: 5, zones: [
      { id: 'K1', occupied: false, reserved: false, value_state: 'known', actions: { reserve: { ok: true, reason: null } }, modules: [] },
      { id: 'K2', occupied: false, reserved: false, value_state: 'unknown', actions: {}, modules: [] },
      { id: 'K3', occupied: false, reserved: false, value_state: 'unknown', actions: {}, modules: [] },
      { id: 'K4', occupied: false, reserved: false, value_state: 'unknown', actions: {}, modules: [] } ] }));
    await sleep(160);
    const av = await evaluate(client, `(() => {
      const out = {};
      for (const c of document.querySelectorAll('#zone-grid .zone')) {
        out[c.dataset.zone] = { act: c.dataset.active === undefined ? null : c.dataset.active,
          sum: (c.querySelector('.zone__sum') || {}).textContent || null,
          why: (c.querySelector('.zone__inactive') || {}).textContent || null,
          view: c.dataset.view || null, aria: c.getAttribute('aria-label') };
      }
      return out;
    })()`);
    const keys = Object.keys(av || {});
    const marked = keys.filter((k) => av[k].act === '0');
    ok('분모: 자리 넷이 다 그려졌다 (' + keys.length + ')', keys.length === 4, JSON.stringify(keys));
    ok('🔴 비활성으로 그린 자리 집합 == active.ok:false 인 자리 집합  ' + S(marked),
       setEq(marked, ['K2', 'K3']), JSON.stringify(av));
    ok('🔴 그 집합이 비어 있지 않다 (' + marked.length + ')', marked.length === 2);
    ok('🔴 active 키가 없는 자리는 아무 주장도 하지 않는다 (K4 · 모듈 0개인데도)',
       av.K4 && av.K4.act === null && av.K4.why === null, JSON.stringify(av.K4));
    ok('사유를 한국어로 말한다 (no_modules)',
       !!(av.K2 && /모듈이 없어/.test(av.K2.why || '')), JSON.stringify(av.K2));
    ok('🔴 모르는 사유 코드는 원문을 보인다 (wibble_zz)',
       !!(av.K3 && /wibble_zz/.test(av.K3.why || '')), JSON.stringify(av.K3));
    /* 🔴 socket 조건: `occupied:false` 를 "비었다"로 바꿔 그리지 마라 — 여전히 "모른다"다. */
    ok('🔴 비활성 자리를 "빈 자리"로 말하지 않는다 (점유 모름)',
       !!(av.K2 && av.K2.sum === '점유 모름' && av.K3 && av.K3.sum === '점유 모름'),
       JSON.stringify([av.K2 && av.K2.sum, av.K3 && av.K3.sum]));
    ok('🔴 비활성 자리를 초록(빈 자리)으로 칠하지 않는다',
       !!(av.K2 && av.K2.view === 'unknown'), JSON.stringify(av.K2));
    ok('활성 주차 자리는 그대로 "빈 자리" 다 (대조군)',
       !!(av.K1 && av.K1.sum === '빈 자리' && av.K1.act === null), JSON.stringify(av.K1));
    ok('접근 이름에 사유가 문장으로 들어간다 (기호만으로 나르지 않는다)',
       !!(av.K2 && /모듈이 없어/.test(av.K2.aria || '')), JSON.stringify(av.K2 && av.K2.aria));
    /* 패널에서도 이유를 읽을 수 있는가 — 사람이 이유를 읽는 자리는 여기다. */
    const pan = await evaluate(client, `(() => {
      const cell = [...document.querySelectorAll('#zone-grid .zone')].find(z => z.dataset.zone === 'K2');
      if (!cell) return null;
      cell.click();
      const ps = [...document.querySelectorAll('#zone-detail .zpanel__empty')].map(p => p.textContent);
      return ps;
    })()`);
    ok('🔴 패널이 비활성 사유와 "점유는 모름" 을 같이 말한다',
       Array.isArray(pan) && pan.some((t) => /사용할 수 없는 자리/.test(t) && /모름/.test(t)),
       JSON.stringify(pan));
  }

  /* ── 🔴 REQ-0289 회귀 방지 — **폭 단계마다 판 배치와 합격선을 같이 본다** ──────
     사용자 요구는 *"PC 화면에서는 넓게"* 였고, 루트의 조건은 *"좁은 화면·2열 거동을 깨지 마라"* 였다.
     🔑 그래서 **세 폭에서 같은 것을 잰다.** 한 폭만 재면 다른 폭이 조용히 깨진다.
     ⚠ 판 배치는 좌표로 잰다 — CSS 문자열을 읽으면 "그렇게 적혀 있다"까지고 "그렇게 놓인다"가 아니다. */
  if (!LIVE) {
    const LAYOUTS = [
      { w: 760,  cols: 1, name: '좁은 화면(1열)' },
      { w: 1280, cols: 2, name: '보통(2열)' },
      { w: 1800, cols: 3, name: '넓은 화면(3열)' },
    ];
    for (const L of LAYOUTS) {
      await client.send('Emulation.setDeviceMetricsOverride',
        { width: L.w, height: 900, deviceScaleFactor: 1, mobile: false });
      await sleep(140);
      const box = await evaluate(client, `(() => {
        const r = (sel) => { const e = document.querySelector(sel); if (!e) return null;
          const b = e.getBoundingClientRect(); return { l: Math.round(b.left), t: Math.round(b.top), r: Math.round(b.right), h: Math.round(b.height) }; };
        const cells = [...document.querySelectorAll('#zone-grid .zone')].map(c => Math.round(c.getBoundingClientRect().height));
        return { grid: r('.pane--grid'), detail: r('.pane--detail'), info: r('.pane--info'),
                 main: r('main'), cells: cells, wrap: r('.wrap') };
      })()`);
      const g = box && box.grid, d = box && box.detail, i = box && box.info;
      ok('분모: ' + L.name + ' 에서 판 셋이 다 있다', !!(g && d && i), JSON.stringify(box));
      if (!(g && d && i)) continue;
      if (L.cols === 1) {
        ok('🔴 ' + L.name + ': 셋이 같은 열에 쌓인다 (왼쪽 좌표가 같다)',
           g.l === d.l && d.l === i.l, JSON.stringify({ g: g.l, d: d.l, i: i.l }));
        ok('🔴 ' + L.name + ': 읽는 순서가 격자 → 상세 → 그 밖',
           g.t <= d.t && d.t <= i.t, JSON.stringify({ g: g.t, d: d.t, i: i.t }));
      }
      if (L.cols === 2) {
        ok('🔴 ' + L.name + ': 격자와 상세가 나란히 선다',
           g.r <= d.l, JSON.stringify({ gridRight: g.r, detailLeft: d.l }));
        ok('🔴 ' + L.name + ': 그 밖은 상세 **아래**에 쌓인다 (둘째 열)',
           i.l === d.l && i.t > d.t, JSON.stringify({ dl: d.l, il: i.l, dt: d.t, it: i.t }));
      }
      if (L.cols === 3) {
        ok('🔴 ' + L.name + ': 셋이 **나란히** 선다 (가로를 쓴다)',
           g.r <= d.l && d.r <= i.l, JSON.stringify({ g: g.r, dl: d.l, dr: d.r, il: i.l }));
        ok('🔴 ' + L.name + ': 세 판의 위쪽이 같은 줄에 있다',
           g.t === d.t && d.t === i.t, JSON.stringify({ g: g.t, d: d.t, i: i.t }));
        ok('넓은 화면에서 .wrap 이 1060 보다 넓다 (' + (box.wrap ? (box.wrap.r - box.wrap.l) : '?') + 'px)',
           !!box.wrap && (box.wrap.r - box.wrap.l) > 1060, JSON.stringify(box.wrap));
      }
      /* 🔴 REQ-0288 의 합격선을 **폭마다** 다시 본다 — 높이를 정하는 것은 뷰포트뿐이어야 한다. */
      ok('🔴 ' + L.name + ': 칸 높이가 서로 같다 (REQ-0288 합격선 유지 · ' + L.w + 'px)',
         box.cells.length >= 2 && box.cells.every((h) => h === box.cells[0]),
         JSON.stringify(box.cells));
    }
    await client.send('Emulation.clearDeviceMetricsOverride');
    await sleep(120);
  }

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
