/**
 * 🔴 **모듈 직접 조작** UI 를 잰다 — 정본 `docs/net/SPEC-web-control.md` §6.
 *
 * ⚠ **주입이다. 서버를 안 쓴다**(`file://` + `?demo=1` · 트래픽 0 · 하행 0).
 *   🔑 **서버가 아직 `control` 을 안 내려보낸다.** 그래서 실기로는 *"재료가 없다"* 밖에 못 잰다 —
 *   주입만이 지금 이 UI 를 밟을 수 있는 유일한 길이다. **배포되면 `--live` 로 다시 재라.**
 *
 * 검사는 **긍정형**이고 **분모를 같이 단언한다**(§6):
 *   ❌ "오류 문구가 안 뜬다"        → 대상이 없어도 참이다
 *   ✅ 조작 UI 가 붙은 모듈 집합 **==** `control` 선언이 있는 모듈 집합
 *   ✅ 🔴 그 집합이 **비어 있지 않다** — 비면 모든 대조가 공허하게 참이 된다(원장 §5.88)
 *
 * 사용: node web/tools/mod-control.mjs           (--head 로 창을 본다)
 */
import { launch, evaluate, sleep } from './cdp.mjs';
import { assertServedIsCurrent } from './screen-build.mjs';

const HEAD = process.argv.includes('--head');
/* `--file <경로>` — 🔑 **음성 대조용**. 고치기 전 판(예: 배포본)에 대고 돌려
   **이 검사가 실제로 빨강이 되는지** 본다. 실패할 수 없는 검사의 초록은 아무 말도 안 한다(원장 §5.88). */
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
/* 🔴 `--live <포트>` — **주입하지 않는다.** 실기 서버가 실제로 보낸 `map`·`state` 만 읽는다.
   주입 모드의 초록은 *"그 자료가 오면 그렇게 그린다"* 이고, 이 모드의 초록은
   *"지금 실기에서 그렇게 나온다"* 다 — **다른 진술이라 따로 재야 한다.**
   ⚠ 클릭·하행 0. 순수 관측이다. `--secs <초>` 로 관측 창을 늘린다(기본 30). */
const LIVE = (() => { const i = process.argv.indexOf('--live'); return i >= 0 ? process.argv[i + 1] : null; })();
const SECS = (() => { const i = process.argv.indexOf('--secs'); return i >= 0 ? Math.max(5, Number(process.argv[i + 1]) || 0) : 30; })();
const URL_ = LIVE
  ? ('http://127.0.0.1:' + LIVE + '/index.html')
  : ((fileArg
      ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg)).href
      : new URL('../../조별과제샘플/web/index.html', import.meta.url).href) + '?demo=1');

let pass = 0, fail = 0, skipped = 0;
const ok = (n, c, d) => { if (c) { pass++; console.log('  ✅ ' + n); } else { fail++; console.log('  ❌ ' + n + (d ? '\n       → ' + d : '')); } };
const skip = (n, why) => { skipped++; console.log('  ⏭ ' + n + '  → 측정 불가: ' + why); };
const inject = (frame) => `(() => { handleServerMessage(${JSON.stringify(frame)}); return true; })()`;
const S = (a) => JSON.stringify(a.slice().sort());
const setEq = (a, b) => a.length === b.length && a.every((x) => b.includes(x));

/* ── 주입 지형 — `control` 선언 셋을 다 밟는다 ─────────────────────
   ⚠ 필드 이름은 **명세 그대로**다. 지어낸 것이 하나도 없다. */
const MAP = {
  type: 'map', srv_id: 'T-9', epoch: 1, grid: { rows: 1, cols: 1 },
  zones: [{ id: 'A1', kind: 'parking', cells: [[0, 0]], modules: [
    /* 선언 없음 — 🔴 조작 UI 가 **안 붙어야** 한다 */
    { devid: 'P1', name: 'A1', kind: 'IP', idx: 0 },
    /* 🔑 **label 있는 것**(기여자 선언) */
    { devid: 'P1', name: 'LD', kind: 'OG', idx: 2, label: '안내등',
      control: { widget: 'toggle' } },
    /* 🔑 **label 없는 것** — MOD_KIND_LABEL 폴백이 돌아야 한다(유도등) */
    { devid: 'P1', name: 'LC', kind: 'OL', idx: 3,
      control: { widget: 'number', min: 0, max: 9999999 } },
    /* 🔑 **label 없고 kind 도 표에 없는 것** — 마지막 폴백(name)까지 밟는다 */
    { devid: 'P1', name: 'DR', kind: 'OZ', idx: 4,
      control: { widget: 'choice',
                 options: [{ value: 1, label: '열기' }, { value: 2, label: '닫기' }] } },
    /* 🔴 이 화면이 모르는 위젯 — **조용히 무시하지 않고 그 사실을 보여야** 한다 */
    { devid: 'P1', name: 'ZZ', kind: 'OG', idx: 6,
      control: { widget: 'dial', label: '미래위젯' } },
  ] }],
};
const ST = (mods) => ({
  type: 'state', srv_id: 'T-9', epoch: 1, ts_ms: 1787200000000, max_per_batch: 4,
  zones: [{ id: 'A1', occupied: false, reserved: false, actions: {}, completion: 'unknown', modules: mods }],
});
const BASE = [
  { devid: 'P1', name: 'A1', idx: 0, value: false, known: true },
  { devid: 'P1', name: 'LD', idx: 2, value: false, known: true, confirmed: 'unknown', cmd: { ok: true, reason: null } },
  { devid: 'P1', name: 'LC', idx: 3, value: false, known: true, confirmed: 'unknown', cmd: { ok: true, reason: null } },
  { devid: 'P1', name: 'DR', idx: 4, value: false, known: true, confirmed: 'unknown', cmd: { ok: true, reason: null } },
  { devid: 'P1', name: 'ZZ', idx: 6, value: false, known: true, confirmed: 'unknown', cmd: { ok: true, reason: null } },
];

const READ = `(() => {
  const out = {};
  /* 🔴 모듈 행과 조작 UI 는 **우측 패널**에 있다 (REQ-0288 - 격자 칸은 요약만).
     ⚠ 예전 선택자 '#zone-grid .zmod' 는 지금 0개를 돌려준다 - 그러면 아래 대조가 전부
     공허하게 참이 되거나 통째로 빨강이 된다. 자리가 바뀌면 계측기의 자리도 바꿔야 한다.
     ⚠ 이 주석은 템플릿 리터럴 안이다 - 역따옴표를 쓰면 문자열이 그 자리에서 끊긴다. */
  for (const li of document.querySelectorAll('#zone-detail .zmod')) {
    const nm = (li.querySelector('.zctl') || {}).dataset;
    const head = (li.querySelector('.zmod__head') || {}).textContent || '';
    const ctl = li.querySelector('.zctl');
    const key = ctl ? ctl.dataset.module : (head.match(/\\(([^)]*)\\)/) || [])[1];
    out[key] = {
      hasCtl: !!ctl,
      widget: ctl ? ctl.dataset.widget : null,
      /* 🔴 연습 칸(.zctl__prac) 안의 버튼을 세면 안 된다 — 그것은 주 위젯이 아니다.
         :scope 직계 행만 본다. (처음에 안 갈라서 toggle 버튼이 3개로 세어졌다 — 계측기 결함이었다.) */
      btns: ctl ? [...ctl.querySelectorAll(':scope .zctl__row:not(.zctl__prac .zctl__row) .zctl__btn')].map(b => b.textContent) : [],
      nums: ctl ? ctl.querySelectorAll(':scope .zctl__row:not(.zctl__prac .zctl__row) .zctl__num').length : 0,
      prac: ctl ? ctl.querySelectorAll('.zctl__prac').length : 0,
      hint: ctl ? ((ctl.querySelector('.zctl__hint') || {}).textContent || null) : null,
      ctlLabel: ctl ? ((ctl.querySelector('.zctl__label') || {}).textContent || null) : null,
      headText: head,
      msgs: ctl ? [...ctl.querySelectorAll('.zctl__msg')].map(m => m.dataset.kind + ':' + m.textContent) : [],
    };
  }
  return out;
})()`;

/**
 * 🔴 **실기 관측** (`--live <포트>`) — 주입·클릭·하행이 하나도 없다.
 *
 * 🔑 §6 의 확인 항목 셋 중 **관측만으로 되는 것은 하나**다:
 *   ✅ `number`·`choice` 모듈에서 `confirmed` 가 `settled` 로 온 적이 없다
 *   ⏭ `outcome` 이 셋 중 하나로 온다 · 보낸 건수 == `queued+rejected`
 *      → **하행이 필요하다.** 하행은 실물 조작이라 이 도구가 스스로 걸지 않는다(루트 순서).
 *        다만 **사람이 화면에서 누른 것**이 이 창에 잡히면 그것으로 잰다.
 *
 * ⚠ **분모를 같이 낸다.** `confirmed` 가 닫힌 값으로 한 번도 안 오면 **미측정**이다 —
 *   그때 초록을 내면 *"서버가 settled 를 안 냈다"* 가 아니라 *"아무 일도 없었다"* 를 초록으로 말한다(원장 §5.88).
 */
async function liveSuite(client) {
  let link = null;
  for (let i = 0; i < 150; i++) {
    link = await evaluate(client, `state.link`).catch(() => null);
    if (link === 'ws') break;
    await sleep(100);
  }
  ok('실기: WS 로 붙었다 (link=' + link + ')', link === 'ws', '데모·폴백이면 서버 자료가 아니다');
  if (link !== 'ws') { skip('실기 관측 전체', 'WS 가 아니다 — 판정하지 않는다'); return; }

  /* 판본을 **값으로** 찍는다. 알림으로 받지 않는다. */
  const srvId = await evaluate(client, `state.srvId`).catch(() => null);
  let live = null;
  for (let i = 0; i < 100; i++) {
    live = await evaluate(client, `state.map`).catch(() => null);
    if (live && Array.isArray(live.zones) && live.zones.length) break;
    await sleep(100);
  }
  ok('실기: 서버가 map 을 보냈다', !!(live && Array.isArray(live.zones) && live.zones.length),
     '받은 것: ' + JSON.stringify(live));
  if (!live || !Array.isArray(live.zones) || !live.zones.length) return;

  /* 🔑 `control` 선언은 **서버가 보낸 것에서** 뽑는다. 내가 아는 목록으로 세면 기대값이 피검체에서 온다. */
  const decl = [];
  for (const z of live.zones) for (const m of (z.modules || [])) {
    if (m && m.control && m.control.widget) decl.push({ devid: m.devid, name: m.name, widget: m.control.widget });
  }
  const nonToggle = decl.filter((d) => d.widget === 'number' || d.widget === 'choice');
  console.log('  · 실기 판본 srv_id=' + srvId + ' epoch=' + live.epoch
            + ' · control 선언 ' + decl.length + '개 (number·choice ' + nonToggle.length + '개) · 관측 ' + SECS + '초');

  ok('🔴 실기에서 control 선언이 온다 (' + decl.length + '개)', decl.length > 0,
     '서버가 control 을 안 내려보낸다 — 도는 서버 판본을 확인해라');

  /* ── 관측 창 — `state` 를 훑어 `confirmed` 표본을 모은다 ─────────── */
  const CLOSED = ['pending', 'settled', 'partial', 'mismatch'];   /* 🔑 `unknown` 은 표본이 아니다 */
  const seen = new Map();      /* devid|name -> Set(confirmed) */
  const outcomes = new Map();  /* devid|name -> outcome */
  let frames = 0, lastTs = null;
  const t0 = Date.now();
  while (Date.now() - t0 < SECS * 1000) {
    const snap = await evaluate(client, `(() => {
      const zs = state.zoneState && state.zoneState.zones;
      const mods = [];
      if (Array.isArray(zs)) for (const z of zs) for (const m of (z.modules || []))
        mods.push({ devid: m.devid, name: m.name, confirmed: m.confirmed === undefined ? null : m.confirmed });
      const cmds = [];
      if (state.modCmd && state.modCmd.forEach) state.modCmd.forEach((v, k) => cmds.push({ key: k, outcome: (v && v.outcome) || null }));
      return { ts: (state.zoneState && state.zoneState.ts_ms) || null, mods: mods, cmds: cmds };
    })()`).catch(() => null);
    if (snap) {
      if (snap.ts !== lastTs) { frames += 1; lastTs = snap.ts; }
      for (const m of snap.mods) {
        const k = m.devid + '|' + m.name;
        if (!seen.has(k)) seen.set(k, new Set());
        if (m.confirmed !== null) seen.get(k).add(String(m.confirmed));
      }
      for (const c of snap.cmds) if (c.outcome) outcomes.set(c.key, c.outcome);
    }
    await sleep(500);
  }
  ok('실기: state 프레임이 창 안에서 갱신됐다 (' + frames + '판)', frames > 0,
     'ts_ms 가 안 바뀐다 — state 가 주기적으로 오지 않는다');

  /* 🔴 판정 — **분모부터 센다.** */
  let sample = 0; const settledAt = [], outside = [];
  for (const d of nonToggle) {
    for (const v of (seen.get(d.devid + '|' + d.name) || [])) {
      if (CLOSED.includes(v)) sample += 1;
      else if (v !== 'unknown') outside.push(d.name + '=' + v);
      if (v === 'settled') settledAt.push(d.name + '(' + d.widget + ')');
    }
  }
  if (!nonToggle.length) {
    skip('🔴 number·choice 모듈에서 settled 가 한 번도 안 온다 (§6)',
         'number·choice 선언이 0개다 — **분모가 없으면 이 대조는 공허하게 참이 된다**(원장 §5.88)');
  } else if (sample === 0) {
    skip('🔴 number·choice 모듈에서 settled 가 한 번도 안 온다 (§6)',
         'confirmed 가 닫힌 값으로 온 적이 **0건**이다(전부 unknown·부재) — 이 창에서 아무도 하행을 안 걸었다. '
       + '초록을 내면 "서버가 안 냈다"가 아니라 "아무 일도 없었다"를 초록으로 말하는 것이다');
  } else {
    ok('🔴 실기: number·choice 의 confirmed 표본 ' + sample + '건이 전부 settled 가 아니다 (§6)',
       settledAt.length === 0,
       '🔴 settled 가 왔다: ' + JSON.stringify(settledAt) + ' — **서버 결함이다**(명세 §4: 판별자는 위젯이다)');
  }
  ok('실기: 닫힌 다섯 밖의 confirmed 값이 없다', outside.length === 0, JSON.stringify(outside));

  /* §6 의 나머지 둘 — 하행 표본이 있으면 재고, 없으면 미측정으로 남긴다. */
  if (outcomes.size === 0) {
    skip('cmd_result.outcome 이 셋 중 하나로 온다 (§6)',
         '이 창에서 하행이 0건이다 — 🔴 **하행은 실물 조작이라 이 도구가 스스로 걸지 않는다**(루트 순서). '
       + '사람이 화면에서 누르면 그것이 이 창에 잡힌다');
  } else {
    const bad = [...outcomes.entries()].filter((e) => !['ok', 'rejected', 'no_answer', 'not_sent'].includes(e[1]));
    ok('실기: outcome 표본 ' + outcomes.size + '건이 모두 아는 갈래다 (ok·rejected·no_answer·not_sent)',
       bad.length === 0, JSON.stringify(bad));
  }
  skip('보낸 건수 == queued + rejected (§6)', '묶음(send_batch)을 이 도구가 스스로 안 보낸다 — 하행이다');
}

let client = null;
try {
  console.log('\n대상: ' + URL_
            + (LIVE ? '\n(🔴 실기 관측 — 주입 0 · 클릭 0 · 하행 0)\n' : '\n(서버 미사용 · 트래픽 0 · 주입)\n'));
  /* 🔴 실기면 **브라우저를 띄우기 전에** 판본을 본다. 낡은 판이면 WS 한 개도 안 열고 끝낸다 —
     붙고 나서 재면 남의 판을 재고 내 것이라고 보고한다(원장 §5.85).
     🔑 그리고 **판정을 내지 않는다**: 선행 조건이 깨진 자리에 빨강을 내면 엉뚱한 데를 고치게 된다. */
  if (LIVE) {
    try {
      await assertServedIsCurrent('http://127.0.0.1:' + LIVE + '/index.html');
    } catch (e) {
      console.log('  ⏭ 실기 관측 전체  → 측정 불가: ' + ((e && e.message) || String(e)));
      console.log('\n' + '─'.repeat(60));
      console.log('  0 pass / 0 fail / 1 미측정 — 🔴 판정하지 않는다 (배포가 먼저다)');
      process.exit(2);
    }
  }
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

  /* 🔴 실기 모드는 **여기서 끝난다** — 이어서 주입하면 실기 자료를 내 주입으로 덮는다. */
  if (LIVE) {
    await liveSuite(client);
    await client.close().catch(() => {});
    console.log('\n' + '─'.repeat(60));
    console.log('  ' + pass + ' pass / ' + fail + ' fail / ' + skipped + ' 미측정');
    process.exit(fail > 0 ? 1 : 0);
  }

  /* 🔑 나가는 프레임을 잡는다 — **"버튼이 있다"가 아니라 "실제로 나갔나"** 를 재기 위해서다. */
  await evaluate(client, `(() => {
    window.__sent = [];
    const orig = transport.send.bind(transport);
    transport.send = function (p) { window.__sent.push(p); };
    window.__origSend = orig;
    return true;
  })()`);

  await evaluate(client, inject(MAP));
  await evaluate(client, inject(ST(BASE)));
  await sleep(150);

  const declared = MAP.zones[0].modules.filter((m) => m.control).map((m) => m.name);
  const undeclared = MAP.zones[0].modules.filter((m) => !m.control).map((m) => m.name);
  let v = await evaluate(client, READ);

  /* ── ① 존재/부재 규칙 — 긍정형 집합 대조 + 🔴 분모 ────────────── */
  ok('🔴 조작 UI 가 붙은 모듈 집합 == control 선언이 있는 모듈 집합',
     setEq(Object.keys(v).filter((k) => v[k].hasCtl), declared),
     '그렸다: ' + S(Object.keys(v).filter((k) => v[k].hasCtl)) + ' · 선언: ' + S(declared));
  ok('🔴 그 집합이 비어 있지 않다 (' + declared.length + '개) — 비면 위 대조가 공허하게 참이 된다',
     declared.length > 0);
  ok('🔴 선언 없는 모듈에는 조작 UI 가 아예 없다 (' + S(undeclared) + ')',
     undeclared.every((k) => v[k] && v[k].hasCtl === false),
     '없는 것과 막힌 것은 다르다 — 막힌 버튼으로도 그리지 않는다');

  /* ── ② 위젯별로 무엇이 그려지나 ──────────────────────────────── */
  ok('toggle → 버튼 둘 (켬/끔)', v.LD && v.LD.widget === 'toggle' && v.LD.btns.length === 2, JSON.stringify(v.LD));
  ok('choice → options 수만큼 버튼 · label 을 그린다 (열기/닫기)',
     v.DR && v.DR.widget === 'choice' && setEq(v.DR.btns, ['열기', '닫기']), JSON.stringify(v.DR && v.DR.btns));
  ok('number → 숫자 칸 + 보내기 + 범위 힌트',
     v.LC && v.LC.widget === 'number' && v.LC.nums >= 1 && v.LC.hint === '0~9999999', JSON.stringify(v.LC));
  ok('🔴 모르는 위젯을 조용히 무시하지 않고 그 사실을 보인다 (dial)',
     v.ZZ && v.ZZ.hasCtl === true && /모르는 조작 형식/.test(JSON.stringify(v.ZZ)), JSON.stringify(v.ZZ));

  /* ── ②-b 🔴 **표시 이름 폴백 3단** — 셋을 다 밟는다 (socket 2026-08-20)
     `mod.label` → `MOD_KIND_LABEL[kind]` → `mod.name`
     🔑 **셋을 다 밟는 재료를 일부러 섞었다.** 전부 label 을 달면 폴백이 한 번도 안 돌고,
        그건 시험이 아니다(socket 이 샘플에도 같은 이유로 섞어 뒀다).
     🔴 **어느 단이든 빈칸이면 안 된다** — 이름 없는 줄은 사용자가 무엇을 조작하는지 모른다. */
  const nameShown = (k) => ((v[k] && v[k].headText) || '');
  ok('폴백①: label 이 있으면 그것을 쓴다 (LD → "안내등")',
     /안내등/.test(nameShown('LD')) && v.LD.ctlLabel === '안내등', nameShown('LD') + ' | ctl:' + v.LD.ctlLabel);
  ok('폴백②: label 이 없으면 MOD_KIND_LABEL (LC · kind=OL → "유도등")',
     /유도등/.test(nameShown('LC')) && v.LC.ctlLabel === '유도등', nameShown('LC') + ' | ctl:' + v.LC.ctlLabel);
  ok('폴백③: label 도 kind 표도 없으면 name (DR · kind=OZ → "DR")',
     /DR/.test(nameShown('DR')) && v.DR.ctlLabel === 'DR', nameShown('DR') + ' | ctl:' + v.DR.ctlLabel);
  ok('🔴 어느 모듈도 이름이 빈칸이 아니다 (' + declared.length + '개)',
     declared.every((k) => v[k] && typeof v[k].ctlLabel === 'string' && v[k].ctlLabel.trim() !== ''),
     JSON.stringify(declared.map((k) => k + ':' + (v[k] && v[k].ctlLabel))));

  /* ── ③ 🔴 범위 — **막는 것**과 **거절이 도는 것**은 둘 다 필요하다 ── */
  const clickNum = async (val, practice) => {
    await evaluate(client, `(() => {
      window.__sent = [];
      const box = document.querySelector('.zctl[data-module="LC"]');
      const scope = ${practice ? 'box.querySelector(".zctl__prac")' : 'box.querySelector(".zctl__row")'};
      const inp = scope.querySelector('.zctl__num');
      inp.value = ${JSON.stringify(String(val))};
      scope.querySelector('.zctl__btn').click();
      return true;
    })()`);
    await sleep(60);
    return await evaluate(client, `window.__sent.length`);
  };
  ok('🔴 범위 밖 값은 화면이 막는다 — 전선으로 안 나간다 (99999999)', (await clickNum(99999999, false)) === 0,
     '나간 프레임 수가 0 이어야 한다');
  ok('범위 안 값은 나간다 (1234567)', (await clickNum(1234567, false)) === 1);
  const sent = await evaluate(client, `JSON.stringify(window.__sent[0])`);
  ok('🔴 나간 봉투가 명세 그대로다 (type·rid·devid·module·value)',
     /"type":"send_cmd"/.test(sent) && /"devid":"P1"/.test(sent) && /"module":"LC"/.test(sent) && /"value":1234567/.test(sent),
     sent);
  /* 🔑 연습 칸은 **일부러 안 막는다** — 거절이 실제로 도는지 배우려면 넘겨 봐야 한다(루트 지시). */
  ok('🔴 연습 칸은 범위를 무시하고 보낸다 (99999999) — 거절을 밟기 위한 것',
     (await clickNum(99999999, true)) === 1, '이게 0 이면 out_of_range 를 영영 못 밟는다');

  /* ── ④ 🔴 `confirmed` — partial 을 settled 처럼 그리지 않는다 ──── */
  const withConf = (name, conf, extra) => BASE.map((m) => (m.name === name ? Object.assign({}, m, { confirmed: conf }, extra || {}) : m));
  await evaluate(client, inject(ST(withConf('LC', 'partial', { requested: 1234567, value: true }))));
  await sleep(120); v = await evaluate(client, READ);
  const lcMsg = (v.LC.msgs || []).join(' | ');
  ok('🔴 partial 이 "값 확인 불가" 로 그려진다 (거짓 완료를 안 만든다)',
     /1234567 보냄/.test(lcMsg) && /값 확인 불가/.test(lcMsg), lcMsg);
  ok('🔴 partial 을 "설정됨/요청대로" 로 그리지 않는다', !/요청대로/.test(lcMsg), lcMsg);

  await evaluate(client, inject(ST(withConf('LD', 'settled', { requested: 1, value: true }))));
  await sleep(120); v = await evaluate(client, READ);
  ok('settled 는 "요청대로 보고" 로 그린다', /요청대로/.test((v.LD.msgs || []).join(' ')), JSON.stringify(v.LD.msgs));

  await evaluate(client, inject(ST(withConf('DR', 'mismatch', { requested: 1, value: false }))));
  await sleep(120); v = await evaluate(client, READ);
  ok('mismatch 는 경고로 그린다', /다르게 보고/.test((v.DR.msgs || []).join(' ')), JSON.stringify(v.DR.msgs));

  /* ── ⑤ 🔴 명령 결말 셋이 **갈려** 보인다 (§3.3) ─────────────── */
  const res = (outcome, extra) => Object.assign({ type: 'cmd_result', rid: 'w1', devid: 'P1', module: 'LD',
    value: 1, outcome: outcome, result: 0, message: '메시지' }, extra || {});
  await evaluate(client, inject(res('ok'))); await sleep(80); v = await evaluate(client, READ);
  const okMsg = (v.LD.msgs || []).join(' ');
  ok('outcome ok 이 성공으로 보인다', /✅/.test(okMsg), okMsg);
  await evaluate(client, inject(res('rejected', { result: 3 }))); await sleep(80); v = await evaluate(client, READ);
  const rjMsg = (v.LD.msgs || []).join(' ');
  ok('🔴 outcome rejected 가 **거절**로 보이고 "다시 눌러도 같다"를 말한다',
     /거절/.test(rjMsg) && /다시 눌러도/.test(rjMsg), rjMsg);
  await evaluate(client, inject(res('no_answer', { result: -1 }))); await sleep(80); v = await evaluate(client, READ);
  const naMsg = (v.LD.msgs || []).join(' ');
  ok('🔴 outcome no_answer 가 **장치·연결 문제**로 보인다 (거절과 다른 문구)',
     /응답하지 않/.test(naMsg) && !/거절/.test(naMsg), naMsg);

  /* ── ⑥ 🔴 **전선 전 거절**(`error`)과 **전선 뒤 거절**(`cmd_result`)이 갈려 보이는가
     socket 정정(2026-08-20): §5 코드 대부분은 `cmd_result` 가 아니라 **`error` 봉투**로 온다.
     명령이 **아직 안 나갔고 장치는 그 일을 모른다** — 사람이 할 일이 다르므로 문구가 달라야 한다. */
  await evaluate(client, `(() => {
    window.__sent = [];
    const box = document.querySelector('.zctl[data-module="LC"]');
    const prac = box.querySelector('.zctl__prac');
    prac.querySelector('.zctl__num').value = '99999999';
    prac.querySelector('.zctl__btn').click();
    return true;
  })()`);
  await sleep(60);
  const ridSent = await evaluate(client, `window.__sent[0] && window.__sent[0].rid`);
  ok('연습 칸이 보낸 요청의 rid 를 잡았다 (이어서 error 를 그 rid 로 되돌린다)', !!ridSent, String(ridSent));
  await evaluate(client, inject({ type: 'error', rid: ridSent, code: 'out_of_range', message: '범위를 벗어났습니다' }));
  await sleep(100); v = await evaluate(client, READ);
  const preMsg = (v.LC.msgs || []).join(' ');
  ok('🔴 전선 전 거절이 **그 모듈 자리에** 붙는다 (어느 모듈인지 사용자가 고를 수 있다)',
     /보내지 않았습니다/.test(preMsg), preMsg);
  ok('🔴 전선 전 거절은 "보내지 않았다"로 말한다 — 장치를 의심하게 만들지 않는다',
     /보내지 않았습니다/.test(preMsg) && !/장치가 거절/.test(preMsg), preMsg);

  await evaluate(client, inject({ type: 'cmd_result', rid: 'w9', devid: 'P1', module: 'DR',
    value: 9, outcome: 'rejected', result: 3, reason: 'device_refused', message: '장치 거절' }));
  await sleep(80); v = await evaluate(client, READ);
  const postMsg = (v.DR.msgs || []).join(' ');
  ok('🔴 전선 뒤 거절은 "장치가 거절" 로 말한다 (device_refused 가 한국어로 뜬다)',
     /장치가 거절/.test(postMsg) && /장치가 이 명령을 거절/.test(postMsg), postMsg);
  ok('🔴 두 거절의 문구가 서로 다르다 — 뭉치면 고칠 곳을 못 가른다', preMsg !== postMsg);

  /* ── ⑦ 🔴 `settled` 규칙이 **두 번 정정됐다.** 지금 정본(socket 2026-08-20 · build 3e20389):
       ❌ 초안   "숫자 모듈은 settled 를 안 낸다"
       ❌ 1차정정 "요청 값이 0/1 이면 settled 가 가능하다"
       ✅ 지금   **`toggle` 위젯만** settled/mismatch 를 낸다. **`number`·`choice` 는 언제나 `partial`**

     🔑 왜 1차 정정이 틀렸나 — 실측이 답했다:
        `DR 1`(열기) → 에코 비트 1 · `DR 2`(닫기) → 에코 비트 **0**
        **에코 비트는 장치의 *상태*이지 우리가 보낸 *인자*가 아니다.**
        옛 규칙이면 `DR 2` + 비트 0 이 `mismatch` → **정확히 성공한 명령을 실패라고 부른다.**

     🔴 **화면은 이 규칙을 몰라도 된다** — `confirmed` 를 **해석하지 않고 그대로 그리기** 때문이다.
        그게 "판정자를 하나로 둔다"의 값어치다. 규칙이 두 번 바뀌는 동안 화면 코드는 안 바뀌었다.
        아래는 그 성질을 단언한다: **서버가 무엇을 주든 화면은 그 말만 옮긴다.** */
  /* ⚠ **한 봉투에 둘을 같이 넣는다.** `state` 는 매번 **전체를 갈아 끼우므로**, 따로 쏘면
     앞의 것이 `unknown` 으로 돌아간다 — 그걸 모르고 두 번에 나눠 쏴서 빨강을 한 번 냈다.
     🔑 계측기가 계약의 성질(전체 교체)을 안 따르면 제품이 아니라 계측기가 틀린다. */
  const both = BASE.map((m) => (
    m.name === 'LD' ? Object.assign({}, m, { confirmed: 'settled', requested: 1, value: true }) :
    m.name === 'DR' ? Object.assign({}, m, { confirmed: 'partial', requested: 2, value: false }) : m));
  await evaluate(client, inject(ST(both)));
  await sleep(120); v = await evaluate(client, READ);
  const ldMsg = (v.LD.msgs || []).join(' ');
  const drMsg = (v.DR.msgs || []).join(' ');
  ok('toggle 이 settled 면 "요청대로 보고" 로 그린다', /요청대로/.test(ldMsg), ldMsg);
  ok('🔴 choice 가 partial 이면 "2 보냄 · 에코 꺼짐 · 값 확인 불가" 로 그린다 (닫기=2 인데 비트 0 인 그 자리)',
     /2 보냄/.test(drMsg) && /값 확인 불가/.test(drMsg) && !/요청대로/.test(drMsg), drMsg);
  ok('🔑 화면이 confirmed 를 해석하지 않는다 — 같은 봉투의 두 모듈을 서버 말대로 다르게 그린다',
     /2 보냄/.test(drMsg) && /요청대로/.test(ldMsg), 'LD: ' + ldMsg + ' || DR: ' + drMsg);

  /* ── ⑧ 🔴 **입력 칸은 사용자 소유다** — 주기 갱신이 치던 값을 덮으면 안 된다
     사용자 실측(2026-08-20): *"LC 에 입력을 하는 중간에 글자가 없어진다."*
     기전: 격자가 매 `state`(초당 한 장)마다 통째로 다시 만들어진다.
     🔑 **긍정형으로 쓴다** — *"안 지워진다"* 가 아니라 **"친 값 그대로 남아 있다"**.
        (부정형은 입력 칸 자체가 없어도 참이 된다 — 그래서 분모도 같이 단언한다.) */
  const typeInto = async (role, text) => await evaluate(client, `(() => {
    const box = document.querySelector('.zctl[data-module="LC"]');
    const scope = ${role === 'prac' ? 'box.querySelector(".zctl__prac")' : 'box.querySelector(".zctl__row")'};
    const inp = scope.querySelector('.zctl__num');
    if (!inp) return 'NO_INPUT';
    inp.focus();
    inp.value = ${JSON.stringify(text)};
    return inp.value;
  })()`);
  const readBack = async (role) => await evaluate(client, `(() => {
    const box = document.querySelector('.zctl[data-module="LC"]');
    const scope = ${role === 'prac' ? 'box.querySelector(".zctl__prac")' : 'box.querySelector(".zctl__row")'};
    const inp = scope.querySelector('.zctl__num');
    return inp ? inp.value : 'NO_INPUT';
  })()`);

  for (const role of ['main', 'prac']) {
    const before = await typeInto(role, '1234567');
    ok('분모: ' + role + ' 입력 칸이 존재한다 (없으면 아래 대조가 공허하게 참이 된다)',
       before === '1234567', String(before));
    /* 주기 갱신을 **세 번** 쏜다 — 실기에서 초당 한 장씩 오는 그것이다. */
    for (let i = 0; i < 3; i++) { await evaluate(client, inject(ST(BASE))); await sleep(40); }
    const after = await readBack(role);
    ok('🔴 ' + role + ' 입력 칸: 주기 갱신 3회 뒤에도 **친 값 그대로 남아 있다** (1234567)',
       after === '1234567',
       '남은 값: ' + JSON.stringify(after) + ' — 🔴 갱신이 사용자가 치던 것을 덮었다');
  }
  /* 포커스도 지켜지는가 — 값만 살고 포커스가 날아가면 사용자는 계속 못 친다. */
  await typeInto('main', '99');
  await evaluate(client, inject(ST(BASE))); await sleep(60);
  ok('입력 칸에 포커스가 남아 있다 (이어서 칠 수 있다)',
     (await evaluate(client, `document.activeElement && document.activeElement.classList.contains('zctl__num')`)) === true);

  skip('실기에서 control 이 온다', '화면이 **미배포**다 — 서빙본은 옛 판이라 지금 실기로 재면 남의 판을 잰다');
  /* 🔴 §6 확인 항목을 **정본으로 고쳤다**(socket 2026-08-20 · 두 번째 정정):
     ❌ ~~"요청 2 이상에서 settled 가 안 온다"~~  (판별자를 **값**으로 봤다)
     ✅ **"`number`·`choice` 모듈에서 `settled` 가 한 번도 안 온다"**  (판별자는 **선언**이다)
     🔑 `toggle` 을 선언한 사람만 *"이 모듈은 값이 곧 상태다"* 라고 말한 것이다. */
  skip('🔴 number·choice 모듈에서 settled 가 한 번도 안 온다 (§6)',
       '실기 관측 항목이다 — 주입으로는 "서버가 안 낸다"를 못 잰다. --live 로 재라');
  skip('no_answer 갈래', '아직 아무도 못 밟았다(장치가 살아 있다) — socket 도 미측정으로 뒀다');

} catch (e) {
  console.log('\n💥 중단: ' + (e && e.message ? e.message : String(e)));
  fail += 1;
} finally {
  if (client) await client.close().catch(() => {});
}
console.log('\n' + '─'.repeat(60));
console.log('  ' + pass + ' pass / ' + fail + ' fail / ' + skipped + ' 미측정');
process.exit(fail > 0 ? 1 : 0);
