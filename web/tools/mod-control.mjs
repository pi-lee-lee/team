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

const HEAD = process.argv.includes('--head');
/* `--file <경로>` — 🔑 **음성 대조용**. 고치기 전 판(예: 배포본)에 대고 돌려
   **이 검사가 실제로 빨강이 되는지** 본다. 실패할 수 없는 검사의 초록은 아무 말도 안 한다(원장 §5.88). */
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
const URL_ = (fileArg
  ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg)).href
  : new URL('../../조별과제샘플/web/index.html', import.meta.url).href) + '?demo=1';

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
    { devid: 'P1', name: 'LD', kind: 'OG', idx: 2,
      control: { widget: 'toggle', label: '표시등' } },
    { devid: 'P1', name: 'LC', kind: 'OL', idx: 3,
      control: { widget: 'number', label: '7자리', min: 0, max: 9999999 } },
    { devid: 'P1', name: 'DR', kind: 'OB', idx: 4,
      control: { widget: 'choice', label: '차단봉',
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
  for (const li of document.querySelectorAll('#zone-grid .zmod')) {
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
      msgs: ctl ? [...ctl.querySelectorAll('.zctl__msg')].map(m => m.dataset.kind + ':' + m.textContent) : [],
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

  /* ── ⑦ 🔴 `settled` 규칙 — socket 이 정정했다: **조건은 모듈이 아니라 값이다** ──
     초안: *"숫자 모듈은 settled 를 안 낸다"* → 부정확.
     정정: **요청 값이 0 이나 1 이 아니면** settled 가 안 나온다. `number` 에 1 을 보내면
           비트가 그 값을 증명하므로 `settled` 가 맞다. */
  await evaluate(client, inject(ST(withConf('LC', 'settled', { requested: 1, value: true }))));
  await sleep(100); v = await evaluate(client, READ);
  ok('요청 값이 1 이면 number 위젯에서도 settled 를 그대로 그린다 (모듈이 아니라 값이 조건이다)',
     /요청대로/.test((v.LC.msgs || []).join(' ')), JSON.stringify(v.LC.msgs));

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
  skip('🔴 요청 2 이상에서 settled 가 한 번도 안 온다 (§6)',
       '실기 관측 항목이다. 주입으로는 "서버가 안 낸다"를 못 잰다 — 배포 뒤 --live 로 재라');
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
