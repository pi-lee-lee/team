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

const HEAD = process.argv.includes('--head');
/* `--file <경로>` 로 다른 화면을 겨눌 수 있다. 🔑 **음성 대조용**이다 —
   개정 전 화면(예: 배포본)에 대고 돌리면 이 검사들이 **실제로 빨강이 되는지** 확인할 수 있다.
   검사가 실패할 수 없으면 그 초록은 아무 말도 안 한다(원장 §5.85 계열). */
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
const URL_ = (fileArg ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg)).href
                      : new URL('../../조별과제샘플/web/index.html', import.meta.url).href) + '?demo=1';

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
const MAP = {
  type: 'map', srv_id: 'T-1', epoch: 3, grid: { rows: 1, cols: 3 },
  zones: [
    { id: 'Z1', kind: 'parking', cells: [[0, 0]], modules: [
      { devid: 'P2', name: 'C1', kind: 'IP', idx: 2 },
      { devid: 'P1', name: 'A1', kind: 'IP', idx: 0 },
      { devid: 'P1', name: 'B1', kind: 'IP', idx: 1 } ] },
    { id: 'Z2', kind: 'entrance', cells: [[0, 1]], modules: [
      { devid: 'P1', name: 'D1', kind: 'OB', idx: 3 } ] },
    { id: 'Z3', kind: 'parking', cells: [[0, 2]], modules: [] },
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
  ],
};

const readZones = `(() => {
  const out = {};
  for (const z of document.querySelectorAll('#zone-grid .zone')) {
    const id = (z.querySelector('.zone__id') || {}).textContent;
    out[id] = {
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
    };
  }
  return out;
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

  await evaluate(client, inject(MAP));
  await evaluate(client, inject(ST));
  await sleep(150);

  const usable = await evaluate(client, `mapUsable()`);
  ok('개정 격자가 켜졌다 (map 이 쓰인다)', usable === true, '옛 slots[] 경로면 이 검사는 무의미하다');
  if (usable !== true) throw new Error('격자가 안 켜졌다');

  const z = await evaluate(client, readZones);

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

  /* ── ② 한 영역에 노드 둘 — 갈라 그리는가 ─────────────────────────── */
  const z1 = z.Z1 ? z.Z1.nodes.length : 0;
  ok('🔴 한 영역에 노드가 둘이면 박스가 둘로 갈린다 (Z1 → ' + z1 + '개)', z1 === 2,
     '지금 실기 지형은 영역당 1대라 이 경우는 **주입으로만** 밟을 수 있다');
  const order = z.Z1 ? z.Z1.nodes.map((n) => n.devid) : [];
  ok('노드 정렬이 devid 사전순이다 (' + JSON.stringify(order) + ')',
     JSON.stringify(order) === JSON.stringify(order.slice().sort()),
     '순서를 안 정하면 프레임마다 흔들려 눈이 못 따라간다');

  /* ── ③ 모듈 0개 영역 — 정상처럼 그리지 않는가 ────────────────────── */
  const z3 = z.Z3 || {};
  ok('🔴 모듈 0개 영역이 "비어 있음"으로 표시된다 (노드 박스 0 + 빈 표시 1)',
     (z3.nodes || []).length === 0 && z3.emptyBoxes === 1,
     JSON.stringify(z3));
  ok('빈 표시가 원인을 단정하지 않는다 (등록 전/결속 끊김 둘 다 말한다)',
     typeof z3.emptyText === 'string' && z3.emptyText.includes('등록') && z3.emptyText.includes('결속'),
     '문구: ' + z3.emptyText);

  /* ── ④ 상태등 셋 — known 을 그대로 옮기는가 ──────────────────────── */
  const lampOf = (zid, dv, i) => {
    const b = z[zid] && z[zid].nodes.find((n) => n.devid === dv);
    return b && b.mods[i] ? b.mods[i].lamp : null;
  };
  ok('🔴 known:true 인 모듈의 등이 🟢 정상이다 (Z1/P1)', lampOf('Z1', 'P1', 0) === 'ok', '등: ' + lampOf('Z1', 'P1', 0));
  ok('🔴 known:false 인 모듈의 등이 ⚪ 모름이다 (Z1/P2 — 녹색도 적색도 아니다)',
     lampOf('Z1', 'P2', 0) === 'unknown',
     '등: ' + lampOf('Z1', 'P2', 0) + ' — 🔴 모름을 녹/적으로 칠하면 "0 이 건강인지 못 셈인지"를 못 가른다');
  /* 🔑 **`value:false` 와 `known:false` 는 다른 축이다.** B1 은 값이 false 지만 **값을 받고 있다** —
     등은 🟢 여야 하고 값 기호만 ○ 다. 이 둘을 한 칸에 합치면 "차 없음"이 "모듈 죽음"으로 보인다. */
  ok('🔴 value:false 인데 known:true 면 등은 🟢 다 (값 축과 상태 축이 안 섞인다 · Z1/P1 의 B1)',
     lampOf('Z1', 'P1', 1) === 'ok', '등: ' + lampOf('Z1', 'P1', 1));
  const lbl = (() => { const b = z.Z1 && z.Z1.nodes.find((n) => n.devid === 'P2'); return b && b.mods[0] ? b.mods[0].lampLabel : null; })();
  ok('등이 색만으로 뜻을 나르지 않는다 (접근 이름에 문장이 있다)',
     typeof lbl === 'string' && lbl.includes('모름'), '접근 이름: ' + lbl);

  skip('실기에서 이 지형이 그렇게 온다', '지금 등록된 노드는 한 대 — **주입으로만 밟았다.** '
     + '둘째 노드의 값은 서버가 아직 못 채운다(REQ-0262)');

} catch (e) {
  console.log('\n💥 중단: ' + (e && e.message ? e.message : String(e)));
  fail += 1;
} finally {
  if (client) await client.close().catch(() => {});
}

console.log('\n' + '─'.repeat(60));
console.log('  ' + pass + ' pass / ' + fail + ' fail / ' + skipped + ' 미측정');
process.exit(fail > 0 ? 1 : 0);
