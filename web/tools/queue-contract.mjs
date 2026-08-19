/**
 * 하행 슬롯 큐 계약(REQ-0155)의 **화면 수신 처리**를 측정한다. REQ-0159.
 *
 *   A. 거절 코드 둘 — `queue_full` · `already_pending` 이 자기 문구로 갈리는가
 *   B. `queued` 수신 — `ahead` 표시 · `expires_ms` 타이머 재설정 · 상한 미제공 · 뒤이은 `ack`
 *
 * 🔴 **왜 서버 없이 재나.** 서버는 아직 `queued` 도 거절 코드도 **보내지 않는다**
 * (socket 의 큐 구현이 `rxmax` 뒤에 있다). 시험 인스턴스는 배타적 자원이라 루트 신호 전에는
 * 못 띄운다. 그래서 `file://` + `?demo=1` 로 연다 — **서버도 포트도 트래픽도 없다**
 * (`transport.start()` 가 데모 분기에서 `connect()`·`startPolling()` 앞에 반환한다 · LEDGER §4.5).
 * 운영 포트로 샐 경로가 **구조적으로** 없다(포트 인자가 아예 없다).
 *
 * ⚠ **주입이다. 서버가 보낸 것이 아니다.** 보고에서 이 구분을 지우지 마라 —
 *   확인되는 것은 "화면이 그 프레임을 받으면 이렇게 한다"까지고,
 *   "서버가 그 프레임을 그 상황에서 보낸다"는 **아직 아무도 안 봤다.**
 *   전송 계층만 건너뛰고 `handleServerMessage → tileView/rollback → render` 는 진짜 경로다.
 *
 * 사용: node web/tools/queue-contract.mjs            (--head 를 붙이면 창이 보인다)
 */
import { launch, evaluate, sleep } from './cdp.mjs';

const argv = process.argv.slice(2);
const HEAD = argv.includes('--head');
const URL_ = new URL('../../조별과제샘플/web/index.html', import.meta.url).href + '?demo=1';

const ACK_TIMEOUT = 6000;      // index.html:564 — 화면의 자체 타임아웃. 아래 B4 의 기준선이다.

let pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; console.log('  ✅ ' + name); }
  else { fail++; console.log('  ❌ ' + name + (detail ? '  → ' + detail : '')); }
}
/**
 * 🔴 **어느 격자가 켜져 있나** — 이 하니스는 **옛 선택자**(`.tile[data-slot]` · `data-action`)로 고른다.
 * 개정 격자(`#zone-grid`)가 켜지면 그 선택자가 **아무것도 못 찾고**, 그러면 뒤의 단언들이
 * **제품 실패로 위장한다**(원장 §5.30 — 하니스 결함이 제품 판정으로 읽힌 그 형태).
 *
 * 🔑 **그래서 "조용히 0건"을 막는다.** 완전한 이중 지원은 나중이고, **조용한 실패는 그 사이에
 * 결론을 오염시킨다.** 못 밟는 것을 초록으로 넘기지 않는 것이 먼저다.
 * (개정 격자 자체는 `web/tools/map-epoch.mjs` 가 잰다.) *
 * ⚠ **이 가드에 힘이 있는지는 여기서 재지 않는다.** 발화 조건(`zoneOn`)이 참이 되는 것은
 *   `web/tools/map-epoch.mjs` **사례 [7]** 이 단언한다(*"새 격자가 보이고 옛 격자가 숨는다"*).
 *   🔴 **그 사례가 바뀌면 이 가드의 검증이 조용히 사라진다** — 중복 측정을 안 만든 대가이므로
 *   의존을 여기 적어 둔다(루트 지적).
 */
async function assertOldGrid(client, ok) {
  const mode = await evaluate(client, `(() => {
    const zg = document.getElementById('zone-grid'), og = document.getElementById('grid');
    return { zoneOn: !!zg && !zg.hidden, oldOn: !!og && !og.hidden,
             tiles: document.querySelectorAll('.tile[data-slot]').length };
  })()`).catch(() => null);
  if (!mode) { ok('[하니스] 격자 종류를 확인했다', false, 'evaluate 실패'); return false; }
  if (mode.zoneOn || mode.tiles === 0) {
    ok('[하니스] 이 하니스가 밟을 수 있는 격자인가', false,
       JSON.stringify(mode) + ' — 🔴 **개정 격자가 켜져 있다. 이 하니스는 옛 선택자를 쓴다.** '
       + '뒤의 결과를 제품 판정으로 읽지 마라. map-epoch.mjs 로 재고, 이중 지원을 넣어라');
    return false;
  }
  ok('[하니스] 옛 격자를 밟는다 (타일 ' + mode.tiles + '개)', true);
  return true;
}


/**
 * 화면이 그린 메시지 중 **그 자리의 것**을 골라 읽는다.
 *
 * 🔴 처음에는 `#messages .msg` 첫 줄(=가장 새 메시지)을 읽었고 **거짓 초록이 났다.**
 * 데모 전송 계층이 시작 메시지를 **비동기로** 넣는데 그것이 내 주입 뒤에 도착해,
 * 첫 사례에서 `"§5.3 예제 스냅샷을 적용했습니다…"` 를 읽고도 **14 pass / 0 fail** 이 찍혔다.
 * 문구 비교(서로 다른가)까지 통과했다 — 한쪽이 남의 메시지였기 때문이다.
 * 즉 **아무것도 못 쟀는데 초록을 내는 검사기**였다(LEDGER §5.18 과 같은 뿌리, 새 모양).
 * → 순서에 기대지 않는다. `rollback()` 이 붙이는 `"<자리> · "` 접두로 **내 것만** 고른다.
 */
const readMsg = (slot) => `(() => {
  const pre = ${JSON.stringify(slot + ' · ')};
  const all = [...document.querySelectorAll('#messages .msg')].map(b => b.querySelector('span').textContent);
  return { mine: all.find(t => t.startsWith(pre)) || null, all: all };
})()`;

/** 그 자리 타일이 실제로 그린 것 — DOM 에서 읽는다.
    ⚠ `b.disabled` 를 보면 안 된다. 이 화면은 일부러 `aria-disabled` 를 쓴다(LEDGER §3.2). */
const readTile = (slot) => `(() => {
  const b = document.querySelector('.tile[data-slot=' + ${JSON.stringify(JSON.stringify(slot))} + ']');
  if (!b) return null;
  return { view: b.dataset.view, ariaDisabled: b.getAttribute('aria-disabled'),
           reason: b.dataset.reason,
           state: b.querySelector('.tile__state').textContent,
           meta: b.querySelector('.tile__meta').textContent };
})()`;

/** pending 항목의 내부 상태 — 타이머가 실제로 갈렸는지 보려면 이것을 봐야 한다. */
const readPending = (rid) => `(() => {
  const p = state.pending.get(${JSON.stringify(rid)});
  if (!p) return null;
  return { status: p.status, ahead: p.ahead, timer: p.timer,
           expiresAt: p.expiresAt, hasExpiry: p.expiresAt !== null,
           /* deadlineAt = 결말까지의 절대 마감(expires_ms + ack_budget_ms). expiresAt 과 다르다 —
              하나는 "큐에서 나갈 때까지", 하나는 "결말까지"다(REQ-0166). */
           deadlineAt: p.deadlineAt, hasDeadline: p.deadlineAt !== null };
})()`;

const mkPending = (rid, slot) => `(() => {
  markPending(${JSON.stringify(rid)}, ${JSON.stringify(slot)}, 'reserve');
  return true;
})()`;

const inject = (frame) => `(() => { handleServerMessage(${JSON.stringify(frame)}); return true; })()`;

/* 🔴 LEDGER §5.18 — 실패 집계는 catch 에서도 올린다.
   예외로 죽은 실행이 "실패 0건 = 통과" 로 찍힌 적이 있다. 아무것도 못 쟀는데 초록을 내는
   검사기가 제일 위험하다. */
let client = null;
try {
  console.log('\n대상: ' + URL_);
  console.log('(서버 미사용 · 포트 없음 · 트래픽 0 · 주입 측정)\n');

  client = await launch({ headless: !HEAD });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Page.navigate', { url: URL_ });

  let ready = false;
  for (let i = 0; i < 100; i++) {
    // ⚠ 불리언을 기다린다 — 빈 문자열이 아닌 모든 문자열은 참이라 문구를 기다리면 즉시 걸린다(LEDGER §4.2).
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.querySelector('.tile')`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  ok('데모 화면이 떴다', ready === true);
  await assertOldGrid(client, ok);

  if (ready !== true) throw new Error('화면이 준비되지 않았다 — 이후 측정은 의미가 없다');
  await sleep(400);                    // 데모의 비동기 시작 메시지를 먼저 흘려보낸다

  /* ══ A. 거절 코드 둘 ═══════════════════════════════════════════════ */
  console.log('\n[A] 거절 코드가 자기 문구로 갈린다 (원본 DESIGN-server-slot-queue.md §4-B)');
  const seen = {};
  const REJECTS = [
    { code: 'queue_full',      slot: 'A1' },
    { code: 'already_pending', slot: 'A2' },
    /* 대조군 — 표에 없는 코드. 여기 나오는 문구가 "코드가 없을 때의 모습"이고,
       위 둘이 이것과 **달라야** 계약이 지켜진 것이다. 이 대조군이 없으면
       내가 넣은 키가 실제로 걸리는지 증명하지 못한다. */
    { code: 'zzz_unknown',     slot: 'A3' },
  ];
  for (const c of REJECTS) {
    const rid = 'rej-' + c.code;
    await evaluate(client, mkPending(rid, c.slot));
    await evaluate(client, inject({ type: 'error', rid: rid, code: c.code }));
    await sleep(150);

    const got = await evaluate(client, readMsg(c.slot));
    const alive = await evaluate(client, `state.pending.has(${JSON.stringify(rid)})`);
    console.log('  · ' + c.code + ' → ' + JSON.stringify(got && got.mine));
    // 접두를 떼고 담는다 — 자리 이름이 다르면 문구 비교가 항상 "다르다"로 통과해 버린다.
    seen[c.code] = got && got.mine ? got.mine.slice((c.slot + ' · ').length) : null;

    ok(c.code + ' — 그 자리의 문구를 찾았다', !!(seen[c.code] && seen[c.code].trim()),
       '못 찾았다. 화면에 있던 것: ' + JSON.stringify(got && got.all));
    // 거절은 접수가 **안 된** 것이다. pending 이 남으면 타일이 영원히 대기로 보인다.
    ok(c.code + ' — pending 이 풀렸다(롤백)', alive === false);
  }
  ok('두 거절 문구가 서로 다르다', seen.queue_full !== seen.already_pending);
  ok('queue_full 이 대조군과 다르다', seen.queue_full !== seen.zzz_unknown, '같으면 코드가 표에 없다는 뜻이다');
  ok('already_pending 이 대조군과 다르다', seen.already_pending !== seen.zzz_unknown, '같으면 코드가 표에 없다는 뜻이다');
  /* 🔴 계약의 요점: 둘 다 **사용자 잘못이 아니다.** "잘못된 요청"으로 읽히면 사용자가
     자기 입력을 의심하고 다시 누른다 — 닫으려는 증폭 고리다(LEDGER §5.14). */
  ok('두 문구 어디에도 "잘못"이 없다',
     !/잘못/.test(String(seen.queue_full)) && !/잘못/.test(String(seen.already_pending)),
     JSON.stringify([seen.queue_full, seen.already_pending]));

  /* ══ B. queued 수신 ═══════════════════════════════════════════════ */
  console.log('\n[B] queued 수신 — ahead 표시 · expires_ms + ack_budget_ms 타이머');

  /* 🔴 서버가 실제로 보내는 값. 리터럴로 쓰지만 **화면은 이 숫자를 코드에 갖고 있지 않다** —
     받아서 쓴다. 서버가 상수에서 계산하므로 값이 바뀌면 화면이 따라온다.
     · REQ-0166: 9900  (ACK_TIMEOUT_MS 1500)
     · REQ-0199: **12600** (ACK_TIMEOUT_MS 2400 = 2×DOWN_SLOT_MS — sendAck 이 슬롯 창을 지키게 되어
       정상 왕복이 135ms→1230ms 가 됐고 옛 문턱 1500 은 여유가 271ms 뿐이라 늦은 ACK 이
       상시 재전송을 만들고 있었다)

     🔴 **맹점을 적어 둔다**: 이 값은 **내가 주입하고 같은 값으로 단언**하므로,
     **서버가 보내는 값이 바뀌어도 이 하니스는 절대 빨간불이 되지 않는다.**
     → 서버 값 변화를 잡는 것은 `queued-live.mjs`(실제 프레임을 읽는다)뿐이고,
     거기서도 **특정 값을 단언하지 않고 내부 일관성만** 본다(값을 박으면 서버가 바뀔 때마다 깨진다).
     **그래서 "지금 서버가 보내는 값"의 기록은 원장 §5.38.3 이고 이 상수는 그 사본이다.** */
  const SRV_BUDGET = 12600;

  // B1 — ahead 가 있고 두 상한이 다 있는 정상 경우
  {
    const rid = 'q-normal';
    await evaluate(client, mkPending(rid, 'B1'));
    const before = await evaluate(client, readPending(rid));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B1', ahead: 2, expires_ms: 4800, ack_budget_ms: SRV_BUDGET }));
    await sleep(150);
    const after = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B1'));
    console.log('  · B1 타일 → ' + JSON.stringify(tile && [tile.state, tile.meta]));

    ok('queued 가 status 를 바꾼다', after && after.status === 'queued', JSON.stringify(after));
    ok('ahead=2 가 문구에 그대로 나온다', !!(tile && /앞에 2건/.test(tile.meta)), JSON.stringify(tile && tile.meta));
    ok('타일이 "접수됨"으로 그려진다', !!(tile && /접수됨/.test(tile.state)), JSON.stringify(tile && tile.state));
    ok('아직 누를 수 없다', tile && tile.ariaDisabled === 'true');
    ok('expiresAt 이 생겼다(큐 이탈 예상)', !!(after && after.hasExpiry));
    /* 🔑 REQ-0166 의 핵심 — 결말 마감은 두 구간의 합이다. 이탈분만 알고 끝내면 이탈 후가 비어
       화면이 서버보다 먼저 롤백한다(그게 3900ms 구멍이었다). */
    ok('deadlineAt 이 생겼다(결말 마감)', !!(after && after.hasDeadline));
    ok('deadlineAt − expiresAt ≈ ack_budget_ms (' + SRV_BUDGET + ')',
       !!(after && Math.abs((after.deadlineAt - after.expiresAt) - SRV_BUDGET) <= 50),
       JSON.stringify(after && [after.expiresAt, after.deadlineAt]));
    /* 🔑 타이머 id 가 갈렸다 = 자체 타임아웃을 **버리고** 새로 걸었다.
       같은 id 면 clearTimeout 이 안 된 것이고, 그러면 6초에 그대로 터진다. */
    ok('타이머가 새로 걸렸다(id 가 갈렸다)', !!(before && after && before.timer !== after.timer),
       JSON.stringify([before && before.timer, after && after.timer]));
  }

  // B2 — ahead 0 은 "다음 차례"로 말한다(0 을 빈 값으로 흘리지 않는다)
  {
    const rid = 'q-next';
    await evaluate(client, mkPending(rid, 'B2'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B2', ahead: 0, expires_ms: 1200, ack_budget_ms: SRV_BUDGET }));
    await sleep(150);
    const tile = await evaluate(client, readTile('B2'));
    console.log('  · B2 타일 → ' + JSON.stringify(tile && tile.meta));
    ok('ahead=0 → "다음 차례입니다"', !!(tile && /다음 차례/.test(tile.meta)), JSON.stringify(tile && tile.meta));
  }

  // B3 — 🔴 expires_ms 가 없을 때 숫자를 지어내지 않는다 (ack_budget_ms 는 줬다)
  {
    const rid = 'q-noexp';
    await evaluate(client, mkPending(rid, 'B3'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B3', ahead: 1, ack_budget_ms: SRV_BUDGET }));
    await sleep(150);
    const p = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B3'));
    console.log('  · B3 reason → ' + JSON.stringify(tile && tile.reason));
    ok('expires_ms 가 없으면 expiresAt 을 안 만든다', !!(p && p.hasExpiry === false), JSON.stringify(p));
    ok('expires_ms 가 없으면 타이머를 안 건다', !!(p && p.timer === 0), JSON.stringify(p && p.timer));
    ok('"모른다"를 화면에 적는다', !!(tile && /알려 주지 않았습니다/.test(String(tile.reason))),
       JSON.stringify(tile && tile.reason));
  }

  /* B7 — 🔴 **한쪽만 온 경우.** expires_ms 는 있고 ack_budget_ms 가 없다.
     이탈 예상은 알지만 **결말 상한을 모른다** → 타이머를 안 걸고 "모른다"를 적어야 한다.
     ⚠ 여기서 ACK_TIMEOUT(6000)으로 메우면 REQ-0166 으로 막은 그 구멍이 되살아난다.
     그리고 이 경우 화면이 `expiresAt` 만 보고 "안다"고 말하면 안 된다 —
     판단 기준이 `deadlineAt` 이어야 하는 이유가 이 케이스다. */
  {
    const rid = 'q-nobudget';
    await evaluate(client, mkPending(rid, 'A5'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'A5', ahead: 1, expires_ms: 1200 }));
    await sleep(150);
    const p = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('A5'));
    console.log('  · B7 → ' + JSON.stringify(p && { hasExpiry: p.hasExpiry, hasDeadline: p.hasDeadline, timer: p.timer }));
    ok('ack_budget_ms 가 없으면 deadlineAt 을 안 만든다', !!(p && p.hasDeadline === false), JSON.stringify(p));
    ok('ack_budget_ms 가 없으면 타이머를 안 건다(6000 으로 메우지 않는다)',
       !!(p && p.timer === 0), JSON.stringify(p && p.timer));
    ok('이탈 예상만 알아도 "모른다"를 적는다', !!(tile && /알려 주지 않았습니다/.test(String(tile.reason))),
       JSON.stringify(tile && tile.reason));
  }

  /* B4 — 🔴 이 계약의 존재 이유. 큐에서 정상 대기 중인 요청을 자체 6초에 걸어 되돌리면
     사용자가 다시 누르고 큐가 한 건 더 쌓인다(증폭 고리). 그것이 **실제로 안 일어나는지**
     시간을 흘려 본다. expires_ms=4800 이면 타이머는 4800+6000=10800ms 이므로
     6초를 넘겨도 살아 있어야 한다. **판독이 아니라 시계로 확인하는 유일한 항목이다.** */
  {
    const rid = 'q-survive';
    await evaluate(client, mkPending(rid, 'B4'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B4', ahead: 3, expires_ms: 4800, ack_budget_ms: SRV_BUDGET }));
    const waitMs = ACK_TIMEOUT + 500;
    console.log('  · B4 자체 타임아웃(' + ACK_TIMEOUT + 'ms)을 넘겨 ' + waitMs + 'ms 기다린다…');
    await sleep(waitMs);
    const p = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B4'));
    ok('자체 6초를 넘겨도 롤백되지 않는다 (증폭 고리가 닫혔다)', p !== null && p.status === 'queued',
       '롤백됐다 = 타이머 재설정이 안 먹었다는 뜻이다');
    ok('그동안 타일은 "접수됨"을 유지한다', !!(tile && /접수됨/.test(tile.state)), JSON.stringify(tile && tile.state));
  }

  /* B6 — 🔴 **산식 자체를 시계로 잰다.** B4 는 "안 터진다"를 보였을 뿐이고 발화 시각이
     `expires_ms + ack_budget_ms` 라는 것은 **판독이다.** 작은 값 둘을 주고 합에 터지는지 본다.
     🔑 **`ACK_TIMEOUT`(6000)이 산식에서 빠진 것을 이 케이스가 증명한다** — 6000 이 아직 섞여
     있으면 발화가 6800ms 쯤이 되어 기대(800ms)와 크게 어긋난다. 그게 REQ-0166 의 구멍이었다. */
  {
    const rid = 'q-formula';
    const EXP = 300, BUD = 500;
    await evaluate(client, mkPending(rid, 'A4'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'A4', ahead: 0, expires_ms: EXP, ack_budget_ms: BUD }));
    const t0 = Date.now();
    console.log('  · B6 expires_ms=' + EXP + ' + ack_budget_ms=' + BUD + ' → ' + (EXP + BUD) + 'ms 에 터져야 한다. 재는 중…');

    let firedAt = null;
    while (Date.now() - t0 < 9000) {
      const alive = await evaluate(client, `state.pending.has(${JSON.stringify(rid)})`).catch(() => null);
      if (alive === false) { firedAt = Date.now() - t0; break; }
      await sleep(100);
    }
    const msg = await evaluate(client, readMsg('A4'));
    console.log('  · B6 실제 발화 → ' + firedAt + 'ms · 문구 ' + JSON.stringify(msg && msg.mine));

    const want = EXP + BUD;
    ok('발화 시각이 expires_ms + ack_budget_ms 다 (' + firedAt + 'ms, 기대 ' + want + 'ms ±400)',
       firedAt !== null && Math.abs(firedAt - want) <= 400,
       'ACK_TIMEOUT(6000)이 아직 산식에 섞여 있으면 여기서 크게 어긋난다');
    /* ⚠ 자체 타임아웃과 다른 문구여야 한다 — 원인이 다르다(서버가 아무 말도 안 함 대 상한 초과).
       ⚠ **"보내지 못해"를 뺐다**(REQ-0166): 서버는 큐 마감을 넘겼다고 항목을 버리지 않으므로
       "큐에서 못 나가서 취소"라는 결말이 **서버에 없다.** 이 타이머가 터지면 진짜로 답이 안 온
       것이다 — 화면은 어느 구간에서 막혔는지 모르니 단정하지 않는다. */
    ok('"보내지 못해"라고 단정하지 않는다',
       !!(msg && msg.mine && !/보내지 못해/.test(msg.mine) && /결과가 오지 않아/.test(msg.mine)),
       JSON.stringify(msg && msg.mine));
    ok('자체 타임아웃과 다른 문구다', !!(msg && msg.mine && !/자체 타임아웃/.test(msg.mine)),
       JSON.stringify(msg && msg.mine));
  }

  // B5 — queued 를 거친 rid 도 ack 로 정상 확정된다(중간 상태가 영구가 되지 않는다)
  {
    const rid = 'q-ack';
    await evaluate(client, mkPending(rid, 'B5'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B5', ahead: 1, expires_ms: 2000, ack_budget_ms: SRV_BUDGET }));
    await sleep(120);
    await evaluate(client, inject({ type: 'ack', rid: rid, slot: 'B5', result: 0 }));
    await sleep(150);
    const p = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B5'));
    console.log('  · B5 타일 → ' + JSON.stringify(tile && tile.state));
    ok('queued 뒤의 ack 가 확정으로 간다', !!(p && p.status === 'confirmed'), JSON.stringify(p));
    ok('타일이 "예약 확정됨"이 된다', !!(tile && /확정됨/.test(tile.state)), JSON.stringify(tile && tile.state));
  }
  /* ══ C. 🔴 명령 경로(cmdPending)의 queued — **하니스가 안 밟던 자리** ═════════
     REQ-0202. **사용자가 실물로 재현한 결함이고 이 하니스는 못 잡았다.**
     `queued` 분기가 `state.pending` 만 봐서 테스트·시뮬 명령의 프레임을 조용히 버리고
     6초 고정 타이머로 **거짓 실패**("응답이 없습니다 (자체 타임아웃)")를 냈다.
     🔑 **하니스가 예약 경로만 밟은 것이 이 결함을 6시간 이상 숨겼다** — 사람이 찾았다.
     그래서 이 경로를 넣는다. **다음에는 사람이 찾지 않게.**

     ⚠ **구성 주의**: `cmdPending` 항목을 **직접 만든다.** 데모 전송 계층이 명령을 즉시 ack 해
     버려서 실제 sender 로는 대기 상태를 만들 수 없다. **전송 계층만 건너뛰고
     `handleServerMessage → clearCmdPending` 은 진짜 경로다**(A·B 와 같은 방식). */
  console.log('\n[C] 명령 경로의 queued — 거짓 실패가 안 나는가 (REQ-0202)');
  {
    const rid = 'cmd-queued';
    await evaluate(client, `(() => {
      state.cmdPending.set(${JSON.stringify(rid)}, { rid: ${JSON.stringify(rid)}, kind: 'sim_step', slot: null,
        timer: window.setTimeout(function () { clearCmdPending(${JSON.stringify(rid)}, true); }, ${ACK_TIMEOUT}) });
      document.getElementById('messages').textContent = '';
      return true;
    })()`);
    const before = await evaluate(client, `(() => { const c = state.cmdPending.get(${JSON.stringify(rid)}); return c ? c.timer : null; })()`);

    await evaluate(client, inject({ type: 'queued', rid: rid, slot: null, ahead: 3, expires_ms: 4800, ack_budget_ms: SRV_BUDGET }));
    await sleep(150);
    const after = await evaluate(client, `(() => { const c = state.cmdPending.get(${JSON.stringify(rid)}); return c ? c.timer : null; })()`);
    const msgs = await evaluate(client, `[...document.querySelectorAll('#messages .msg')].map(b => b.querySelector('span').textContent)`);
    const sim = await evaluate(client, `state.simOutcome ? state.simOutcome.text : null`);
    console.log('  · C 메시지 → ' + JSON.stringify(msgs && msgs[0]));
    console.log('  · C 시뮬 패널 → ' + JSON.stringify(sim));

    ok('명령 경로에서 타이머가 다시 걸렸다(id 가 갈렸다)', before !== null && after !== null && before !== after,
       JSON.stringify([before, after]) + ' — 같으면 queued 를 버린 것이다(고치기 전 상태)');
    /* 🔴 **보이는가.** 안 보이면 "그냥 빨라졌나"와 구분이 안 된다 — 이 계약의 목적이 그것이다. */
    ok('queued 가 메시지로 보인다', !!(msgs && msgs.some(t => /서버가 받았습니다/.test(t))), JSON.stringify(msgs));
    ok('ahead 가 문구에 나온다', !!(msgs && msgs.some(t => /앞에 3건/.test(t))), JSON.stringify(msgs));
    ok('시뮬 패널에도 보인다', !!(sim && /서버가 받았습니다/.test(sim)), JSON.stringify(sim));

    /* 🔴 결함의 정확한 서명: **6초에 거짓 실패가 나는가.** 예산이 걸렸으면 살아 있어야 한다. */
    console.log('  · C 자체 타임아웃(' + ACK_TIMEOUT + 'ms)을 넘겨 6500ms 기다린다…');
    await sleep(6500);
    const alive = await evaluate(client, `state.cmdPending.has(${JSON.stringify(rid)})`);
    const after6 = await evaluate(client, `[...document.querySelectorAll('#messages .msg')].map(b => b.querySelector('span').textContent)`);
    ok('6초를 넘겨도 거짓 실패가 안 난다', alive === true,
       '사라졌다 = 자체 타임아웃이 터졌다는 뜻이다 — 사용자가 본 그 증상이다');
    ok('"자체 타임아웃" 문구가 안 떴다', !!(after6 && !after6.some(t => /자체 타임아웃/.test(t))),
       JSON.stringify(after6));
  }

} catch (e) {
  fail++;                                       // ← §5.18: 예외도 실패다
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 측정 결과가 아니다 */ } }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
