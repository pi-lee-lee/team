/**
 * 🔴 **서버가 실제로 보내는 `queued` 를 브라우저 종단에서 본다** (REQ-0155/0166 · 아무도 안 본 칸).
 *
 * 지금까지 화면 쪽 계약 검증은 **전부 주입**이었다(`queue-contract.mjs`). 이 도구만이
 * *"서버가 그 프레임을 실제로 보낸다"* 를 확인한다 — 루트가 배포 전 필수로 본 항목이다.
 *
 * ⚠ **구성 주의 — 이것이 완전한 종단이 아니다.**
 * 시험 서버의 cwd 에 `index.html` 이 없어 **페이지를 서버가 못 준다**(원장 §4.4).
 * 그래서 페이지는 **별도 정적 서버**가 주고 **WS 만** `?ws=<포트>` 로 시험 서버에 붙인다.
 * → **WS 레그는 진짜다. "서버가 서빙한 페이지"는 아니다.** 보고에서 이 구분을 지우지 마라.
 *
 * 사용: node web/tools/queued-live.mjs --page 8788 --ws 10500
 */
import { launch, evaluate, sleep } from './cdp.mjs';
import { assertServedIsCurrent } from './screen-build.mjs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
/* --served <포트> : 서버가 페이지도 준다(시연 구성). `?ws=` 를 안 붙이므로
   화면이 §1.1 의 불변식(페이지를 준 포트 = WS 포트)대로 스스로 포트를 정한다. **이쪽이 진짜다.**
   --page/--ws : 서버가 페이지를 못 줄 때의 우회(원장 §4.4). 페이지와 WS 가 갈린다. */
const SERVED = arg('--served', null);
const PAGE = SERVED || arg('--page', null);
const WS = SERVED || arg('--ws', null);
if (!PAGE || !WS) { console.error('--served <포트>  또는  --page <정적서버포트> --ws <시험서버포트>'); process.exit(2); }
/* 🔴 운영 포트 거부 — 팀 표준(원장 §5.5). 기본값이 없고 운영은 거부한다. */
for (const p of [PAGE, WS]) {
  if (['9900', '9991', '5500'].includes(String(p))) { console.error('🔴 운영 포트 거부: ' + p); process.exit(2); }
}
const URL_ = SERVED
  ? ('http://127.0.0.1:' + SERVED + '/index.html')      // 시연 구성 — 화면이 스스로 포트를 정한다
  : ('http://127.0.0.1:' + PAGE + '/index.html?ws=' + WS);

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

const tile = (slot) => `(() => {
  const b = document.querySelector('.tile[data-slot=' + ${JSON.stringify(JSON.stringify(slot))} + ']');
  if (!b) return null;
  return { view: b.dataset.view, aria: b.getAttribute('aria-disabled'), action: b.dataset.action,
           state: b.querySelector('.tile__state').textContent,
           meta: b.querySelector('.tile__meta').textContent };
})()`;

/* 🔴 §5.18 — 실패 집계는 catch 에서도 올린다. 예외로 죽은 실행이 "통과"로 찍힌 적이 있다. */
let client = null;
let reservedSlot = null;
try {
  console.log('\n대상 페이지: ' + URL_);
  console.log(SERVED
    ? '✅ 서버가 페이지도 WS 도 준다(시연 구성) — 화면이 §1.1 대로 스스로 포트를 정한다\n'
    : '⚠ 페이지는 정적 서버(' + PAGE + '), WS 는 시험 서버(' + WS + ') — 원장 §4.4 우회\n');

  client = await launch({ headless: true });
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  /* 🔴 **낡은 화면을 재지 않는다** — 서빙본이 저장소 원본과 다르면 던진다(원장 §5.85).
     :9900 이 08-17 사본을 이틀간 내주는 동안 이 하니스들은 아무 말도 안 했다. */
  await assertServedIsCurrent(URL_);
  await client.send('Page.navigate', { url: URL_ });

  let ready = false;
  for (let i = 0; i < 120; i++) {
    ready = await evaluate(client, `document.readyState === 'complete' && !!document.querySelector('.tile')`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  ok('화면이 떴다', ready === true);
  await assertOldGrid(client, ok);

  if (ready !== true) throw new Error('화면 준비 실패');

  /* 서버 프레임을 전부 기록한다. WS 경로(2185행)가 **이름으로** 부르므로 감싸기가 먹는다. */
  await evaluate(client, `(() => {
    window.__frames = [];
    const orig = handleServerMessage;
    handleServerMessage = function (m) {
      try { window.__frames.push({ t: Date.now(), type: m && m.type,
        rid: m && m.rid, slot: m && m.slot,
        ahead: m && m.ahead, expires_ms: m && m.expires_ms,
        ack_budget_ms: m && m.ack_budget_ms, result: m && m.result, code: m && m.code }); } catch (e) {}
      return orig(m);
    };
    return true;
  })()`);

  // 실제 WS 로 붙었는가 — 데모/폴백이면 이 측정은 무의미하다
  let link = null;
  for (let i = 0; i < 150; i++) {
    link = await evaluate(client, `state.link`).catch(() => null);
    if (link === 'ws') break;
    await sleep(100);
  }
  ok('실제 WS 로 연결됐다 (link=' + link + ')', link === 'ws',
     '데모/폴백이면 서버 프레임이 아니다 — 측정 무효');
  if (link !== 'ws') throw new Error('WS 연결 실패 — link=' + link);

  // 스냅샷이 왔는가 + 장치가 붙어 있는가(§3.1.1 절차: last_frame_ts 진동)
  await sleep(400);
  const dev = await evaluate(client, `(() => {
    const s = state.snapshot; if (!s) return null;
    return { online: s.device && s.device.online, lft: s.device && s.device.last_frame_ts, slots: (s.slots||[]).length };
  })()`);
  console.log('  · device → ' + JSON.stringify(dev));
  ok('스냅샷 10칸이 왔다', !!(dev && dev.slots === 10), JSON.stringify(dev));

  /* ⚠ 측정 전에 장치가 정말 붙어 있는지부터 본다 — 값 하나로는 "부재"와 "침묵"이 안 갈린다(§5.11).
     서로 다른 last_frame_ts 가 여럿이면 프레임이 오고 있다는 뜻이다. */
  const seenLft = new Set();
  for (let i = 0; i < 24; i++) {
    const v = await evaluate(client, `state.snapshot && state.snapshot.device ? state.snapshot.device.last_frame_ts : null`).catch(() => null);
    if (v !== null && v !== undefined) seenLft.add(String(v));
    await sleep(250);
  }
  console.log('  · last_frame_ts 서로 다른 값 ' + seenLft.size + '개 / 24표본');
  ok('last_frame_ts 가 진동한다(장치가 붙어 있다)', seenLft.size >= 2,
     '하나로 얼어 있으면 프레임이 없다는 뜻 — 그 구간의 값은 다르게 읽어야 한다(§5.11)');

  // 누를 수 있는 빈 자리 하나
  const free = await evaluate(client, `(() => {
    const b = [...document.querySelectorAll('.tile')].find(x =>
      x.dataset.action === 'reserve' && x.getAttribute('aria-disabled') !== 'true');
    return b ? b.dataset.slot : null;
  })()`);
  ok('예약 가능한 빈 자리를 찾았다 (' + free + ')', !!free);
  if (!free) throw new Error('빈 자리가 없다 — 시험을 시작할 수 없다');

  /* ── 예약: 진짜 클릭 → 확인 대화상자 → 확인 ─────────────────── */
  await evaluate(client, `(() => { window.__frames = []; return true; })()`);
  const t0 = await evaluate(client, `Date.now()`);
  await evaluate(client, `document.querySelector('.tile[data-slot=' + ${JSON.stringify(JSON.stringify(free))} + ']').click()`);

  let dlg = false;
  for (let i = 0; i < 60; i++) {
    dlg = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false);
    if (dlg === true) break;
    await sleep(100);
  }
  ok('확인 대화상자가 열렸다', dlg === true);
  await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);
  reservedSlot = free;

  /* 🔴 여기가 이 도구의 본체 — **서버가 보낸** queued 를 기다린다. */
  let q = null;
  for (let i = 0; i < 80; i++) {
    q = await evaluate(client, `(window.__frames || []).find(f => f.type === 'queued') || null`).catch(() => null);
    if (q) break;
    await sleep(100);
  }
  console.log('\n  🔑 서버가 보낸 queued → ' + JSON.stringify(q));
  ok('서버가 queued 를 보냈다', !!q, '이것이 지금까지 아무도 안 본 칸이었다');

  if (q) {
    ok('queued 에 ack_budget_ms 가 실려 있다 (' + q.ack_budget_ms + ')',
       typeof q.ack_budget_ms === 'number' && q.ack_budget_ms > 0,
       '없으면 화면은 타이머를 안 걸고 "모른다"를 적는다(B7) — 구멍이 되살아난다');
    ok('queued 에 expires_ms 가 실려 있다 (' + q.expires_ms + ')',
       typeof q.expires_ms === 'number' && q.expires_ms >= 0);
    ok('ahead 가 숫자다 (' + q.ahead + ')', typeof q.ahead === 'number');
    // 화면이 그 값을 실제로 반영했는가 — 주입이 아니라 서버 값으로
    const p = await evaluate(client, `(() => {
      for (const [rid, v] of state.pending) if (v.slot === ${JSON.stringify(free)})
        return { status: v.status, hasDeadline: v.deadlineAt !== null, gap: v.deadlineAt - v.expiresAt };
      return null; })()`);
    console.log('  · 화면 pending → ' + JSON.stringify(p));
    ok('화면이 결말 마감을 세웠다(deadlineAt)', !!(p && p.hasDeadline),
       '서버 값으로 타이머가 걸렸다는 뜻이다');
    if (p && p.gap != null) {
      ok('deadlineAt − expiresAt = 서버가 준 ack_budget_ms', Math.abs(p.gap - q.ack_budget_ms) <= 50,
         JSON.stringify([p.gap, q.ack_budget_ms]));
    }
  }

  // 그 뒤 ack — 계약의 보장(queued 뒤에는 반드시 ack 또는 error)
  let a = null;
  for (let i = 0; i < 150; i++) {
    a = await evaluate(client, `(window.__frames || []).find(f => f.type === 'ack' || f.type === 'error') || null`).catch(() => null);
    if (a) break;
    await sleep(100);
  }
  console.log('  🔑 그 뒤 프레임 → ' + JSON.stringify(a));
  ok('queued 뒤에 ack/error 가 왔다(계약 보장)', !!a);
  if (q && a) {
    const gap = a.t - q.t;
    console.log('  · queued → ack 간격 = ' + gap + 'ms');
    ok('간격이 한 슬롯 규모다 (' + gap + 'ms · 슬롯 1200ms)', gap >= 0 && gap <= 4000,
       '슬롯 구조라면 다음 창을 기다린 시간이 보인다');
  }
  const tv = await evaluate(client, tile(free));
  console.log('  · 예약 후 타일 → ' + JSON.stringify(tv && [tv.state, tv.meta]));
  ok('타일이 내 예약이 됐다', !!(tv && /예약|확정/.test(tv.state)), JSON.stringify(tv));
} catch (e) {
  fail++;
  console.log('  💥 예외로 중단: ' + (e && e.message ? e.message : e));
} finally {
  /* 🔴 **반드시 취소까지 하고 끝난다.** 시험이 상태를 남기면 다음 사람이 실제 예약으로 읽는다. */
  if (client && reservedSlot) {
    try {
      console.log('\n[정리] ' + reservedSlot + ' 예약을 취소한다');
      await sleep(600);
      await evaluate(client, `(() => { window.__frames = []; return true; })()`);
      await evaluate(client, `document.querySelector('.tile[data-slot=' + ${JSON.stringify(JSON.stringify(reservedSlot))} + ']').click()`);
      for (let i = 0; i < 60; i++) {
        const d = await evaluate(client, `document.getElementById('confirm-dialog').open === true`).catch(() => false);
        if (d === true) break;
        await sleep(100);
      }
      await evaluate(client, `document.querySelector('#confirm-dialog button[value="ok"]').click()`);
      let done = false;
      for (let i = 0; i < 150; i++) {
        done = await evaluate(client, `(window.__frames || []).some(f => f.type === 'ack' || f.type === 'error')`).catch(() => false);
        if (done === true) break;
        await sleep(100);
      }
      /* ⚠ **`action` 으로 판정하면 안 된다.** 취소 직후는 내가 만든 **1초 아이들**(§5.16)이
         걸려 있어 `action` 이 비고 `aria-disabled=true` 다 — 그걸 "취소 실패"로 읽으면
         제품이 아니라 하니스가 틀린 것이다(§5.17 과 같은 형태로 한 번 걸렸다).
         **자리가 비었는지는 `view`/`state` 로 본다** — 그건 서버 스냅샷에서 온 값이다.
         그리고 아이들이 풀리는 것까지 기다려 `action` 이 돌아오는 것도 같이 확인한다. */
      let tv = await evaluate(client, tile(reservedSlot));
      console.log('  · 취소 직후 타일 → ' + JSON.stringify(tv && [tv.view, tv.state, tv.meta]));
      ok('서버 스냅샷에서 자리가 비었다(취소가 반영됐다)',
         !!(tv && tv.view === 'free' && /빈 자리/.test(tv.state)), JSON.stringify(tv));

      for (let i = 0; i < 30; i++) {
        tv = await evaluate(client, tile(reservedSlot));
        if (tv && tv.action === 'reserve') break;
        await sleep(100);
      }
      console.log('  · 아이들 해제 후 → ' + JSON.stringify(tv && [tv.action, tv.aria]));
      ok('아이들이 풀려 다시 누를 수 있다(상태를 남기지 않는다)',
         !!(tv && tv.action === 'reserve' && tv.aria !== 'true'), JSON.stringify(tv));
    } catch (e) {
      fail++;
      console.log('  💥 정리 실패 — **예약이 남아 있을 수 있다**: ' + (e && e.message ? e.message : e));
    }
  }
  if (client && client.close) { try { await client.close(); } catch { /* 종료 실패는 결과가 아니다 */ } }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
