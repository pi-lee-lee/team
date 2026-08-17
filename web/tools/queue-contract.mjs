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
const URL_ = new URL('../../조별과제샘플/index.html', import.meta.url).href + '?demo=1';

const ACK_TIMEOUT = 6000;      // index.html:564 — 화면의 자체 타임아웃. 아래 B4 의 기준선이다.

let pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; console.log('  ✅ ' + name); }
  else { fail++; console.log('  ❌ ' + name + (detail ? '  → ' + detail : '')); }
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
           expiresAt: p.expiresAt, hasExpiry: p.expiresAt !== null };
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
  console.log('\n[B] queued 수신 — ahead 표시 · expires_ms 타이머 재설정');

  // B1 — ahead 가 있고 상한도 있는 정상 경우
  {
    const rid = 'q-normal';
    await evaluate(client, mkPending(rid, 'B1'));
    const before = await evaluate(client, readPending(rid));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B1', ahead: 2, expires_ms: 4800 }));
    await sleep(150);
    const after = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B1'));
    console.log('  · B1 타일 → ' + JSON.stringify(tile && [tile.state, tile.meta]));

    ok('queued 가 status 를 바꾼다', after && after.status === 'queued', JSON.stringify(after));
    ok('ahead=2 가 문구에 그대로 나온다', !!(tile && /앞에 2건/.test(tile.meta)), JSON.stringify(tile && tile.meta));
    ok('타일이 "접수됨"으로 그려진다', !!(tile && /접수됨/.test(tile.state)), JSON.stringify(tile && tile.state));
    ok('아직 누를 수 없다', tile && tile.ariaDisabled === 'true');
    ok('expiresAt 이 생겼다', !!(after && after.hasExpiry));
    /* 🔑 타이머 id 가 갈렸다 = 자체 타임아웃을 **버리고** 새로 걸었다.
       같은 id 면 clearTimeout 이 안 된 것이고, 그러면 6초에 그대로 터진다. */
    ok('타이머가 새로 걸렸다(id 가 갈렸다)', !!(before && after && before.timer !== after.timer),
       JSON.stringify([before && before.timer, after && after.timer]));
  }

  // B2 — ahead 0 은 "다음 차례"로 말한다(0 을 빈 값으로 흘리지 않는다)
  {
    const rid = 'q-next';
    await evaluate(client, mkPending(rid, 'B2'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B2', ahead: 0, expires_ms: 1200 }));
    await sleep(150);
    const tile = await evaluate(client, readTile('B2'));
    console.log('  · B2 타일 → ' + JSON.stringify(tile && tile.meta));
    ok('ahead=0 → "다음 차례입니다"', !!(tile && /다음 차례/.test(tile.meta)), JSON.stringify(tile && tile.meta));
  }

  // B3 — 🔴 서버가 상한을 안 줬을 때 숫자를 지어내지 않는다
  {
    const rid = 'q-noexp';
    await evaluate(client, mkPending(rid, 'B3'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B3', ahead: 1 }));
    await sleep(150);
    const p = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B3'));
    console.log('  · B3 reason → ' + JSON.stringify(tile && tile.reason));
    ok('expires_ms 가 없으면 expiresAt 을 안 만든다', !!(p && p.hasExpiry === false), JSON.stringify(p));
    ok('expires_ms 가 없으면 타이머를 안 건다', !!(p && p.timer === 0), JSON.stringify(p && p.timer));
    ok('"모른다"를 화면에 적는다', !!(tile && /알려 주지 않았습니다/.test(String(tile.reason))),
       JSON.stringify(tile && tile.reason));
  }

  /* B4 — 🔴 이 계약의 존재 이유. 큐에서 정상 대기 중인 요청을 자체 6초에 걸어 되돌리면
     사용자가 다시 누르고 큐가 한 건 더 쌓인다(증폭 고리). 그것이 **실제로 안 일어나는지**
     시간을 흘려 본다. expires_ms=4800 이면 타이머는 4800+6000=10800ms 이므로
     6초를 넘겨도 살아 있어야 한다. **판독이 아니라 시계로 확인하는 유일한 항목이다.** */
  {
    const rid = 'q-survive';
    await evaluate(client, mkPending(rid, 'B4'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B4', ahead: 3, expires_ms: 4800 }));
    const waitMs = ACK_TIMEOUT + 500;
    console.log('  · B4 자체 타임아웃(' + ACK_TIMEOUT + 'ms)을 넘겨 ' + waitMs + 'ms 기다린다…');
    await sleep(waitMs);
    const p = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B4'));
    ok('자체 6초를 넘겨도 롤백되지 않는다 (증폭 고리가 닫혔다)', p !== null && p.status === 'queued',
       '롤백됐다 = 타이머 재설정이 안 먹었다는 뜻이다');
    ok('그동안 타일은 "접수됨"을 유지한다', !!(tile && /접수됨/.test(tile.state)), JSON.stringify(tile && tile.state));
  }

  // B5 — queued 를 거친 rid 도 ack 로 정상 확정된다(중간 상태가 영구가 되지 않는다)
  {
    const rid = 'q-ack';
    await evaluate(client, mkPending(rid, 'B5'));
    await evaluate(client, inject({ type: 'queued', rid: rid, slot: 'B5', ahead: 1, expires_ms: 2000 }));
    await sleep(120);
    await evaluate(client, inject({ type: 'ack', rid: rid, slot: 'B5', result: 0 }));
    await sleep(150);
    const p = await evaluate(client, readPending(rid));
    const tile = await evaluate(client, readTile('B5'));
    console.log('  · B5 타일 → ' + JSON.stringify(tile && tile.state));
    ok('queued 뒤의 ack 가 확정으로 간다', !!(p && p.status === 'confirmed'), JSON.stringify(p));
    ok('타일이 "예약 확정됨"이 된다', !!(tile && /확정됨/.test(tile.state)), JSON.stringify(tile && tile.state));
  }
} catch (e) {
  fail++;                                       // ← §5.18: 예외도 실패다
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 측정 결과가 아니다 */ } }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
