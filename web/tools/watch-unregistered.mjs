/**
 * 🔴 **`node_unregistered` 를 실물에서 잡는다** — ⑥의 마지막 미측정 항목. (2026-08-19)
 *
 * 이 상태는 **노드 접속과 등록 완료 사이**에만 있고 그 창이 **1초**다
 * (socket 실측: `05:16:18` 접속 → `05:16:19` 등록 완료).
 * `live-map.mjs` 는 붙는 데 그보다 오래 걸려 **다섯 회차 전부 이미 끝난 뒤에 도착했다** —
 * 그래서 통과가 아니라 **미측정**으로 적었다.
 *
 * 🔑 그래서 이 도구는 **재기 순서를 뒤집는다**: 먼저 붙어서 기록을 켜고 **기다린다.**
 *    그 사이에 socket 이 세션을 끊으면 장치가 곧바로 다시 붙고, 그 1초가 내 기록 안으로 들어온다.
 *    **잴 창이 짧은 것은 "나중에 재기"가 아니라 "그 순간 표본 뜨기"다**(원장 §5.57·§5.60).
 *
 * ⚠ **화면을 조작하지 않는다.** 클릭도 하행도 없다 — 순수 관측이다. 그래서 아이들 티커도 안 돈다.
 * ⚠ **못 잡으면 "없다"가 아니라 "미측정"이다.** 이 도구가 침묵하는 것은 **그 코드가 안 온다는 뜻이
 *    아니라 내가 그 창에 못 있었다는 뜻**일 수 있다 — 출력이 그 둘을 갈라 말한다.
 *
 * 사용: node web/tools/watch-unregistered.mjs --port 9900 --allow-prod --seconds 180
 */
import { launch, evaluate, sleep, localStamp } from './cdp.mjs';
import { assertServedIsCurrent } from './screen-build.mjs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const PORT = arg('--port', null);
const SECONDS = Number(arg('--seconds', '180'));
if (!PORT) { console.error('--port <포트>'); process.exit(2); }
/* 운영 포트 가드는 `live-map.mjs` 와 같은 규칙이다 — 지우지 않고 예외를 손으로 준다(§5.5). */
if (['9900', '9991', '5500'].includes(String(PORT)) && !argv.includes('--allow-prod')) {
  console.error('🔴 운영 포트 거부: ' + PORT + ' — --allow-prod 를 명시해라'); process.exit(2);
}
const URL_ = 'http://127.0.0.1:' + PORT + '/index.html';
/* 🔴 **현지 시각으로 찍는다** — UTC 로 찍었더니 서버 로그(KST)와 시(hour)가 어긋났다(§5.64). */
const stamp = () => localStamp();

/** 이 판본이 내 것인지 — §5.43: `200` 은 "내 코드가 서빙된다"가 아니다. */
async function fingerprint(client) {
  return await evaluate(client, `(() => ({
    zoneGrid: !!document.getElementById('zone-grid'),
    hasGetMap: typeof requestMap === 'function',
    hasSrvId: (typeof state !== 'undefined') && ('srvId' in state)
  }))()`).catch(() => null);
}

let client = null;
try {
  console.log('\n🔎 ' + URL_ + ' 에 붙어 `node_unregistered` 를 기다린다 (' + SECONDS + '초)\n');
  client = await launch({ headless: true });
  await client.send('Page.enable');
  await client.send('Runtime.enable');

  /* 🔴 기록기를 **항행 전에** 설치한다 — 접속 시 프레임이 그 전에 오면 놓친다(§5.30 의 그 함정). */
  const rx = [];
  client.on((method, p) => {
    if (method !== 'Network.webSocketFrameReceived') return;
    const d = p.response && p.response.payloadData;
    if (typeof d === 'string' && d.charAt(0) === '{') {
      try { rx.push({ t: stamp(), m: JSON.parse(d) }); } catch (e) {}
    }
  });
  await client.send('Network.enable');
  await client.send('Page.navigate', { url: URL_ });

  let ready = false;
  for (let i = 0; i < 120; i++) {
    ready = await evaluate(client, `document.readyState === 'complete'`).catch(() => false);
    if (ready === true) break;
    await sleep(100);
  }
  if (ready !== true) throw new Error('화면 준비 실패');
  const fp = await fingerprint(client);
  console.log('· 서빙된 판본 지표 → ' + JSON.stringify(fp));
  if (!fp || !fp.zoneGrid || !fp.hasGetMap || !fp.hasSrvId) {
    throw new Error('서빙된 판본이 내 것이 아니다 — 이 관측은 무효다(§5.43)');
  }
  /* 🔴 **위 지표만으로는 부족하다**(원장 §5.85). 저것은 *기능이 있나*를 묻는 **존재형**이라
     08-17 사본처럼 통째로 없으면 잡지만 **`zone-grid` 를 가진 어제 사본은 그대로 통과한다.**
     🔑 물어야 할 것은 "있나"가 아니라 "같은가"다 — 내용 해시로 원본과 대조한다. */
  await assertServedIsCurrent(URL_);
  console.log('✅ 붙었다. 기록 중 — 지금 재접속을 유도해도 된다.\n');

  /** 어떤 자리의 어떤 조작이 이 코드로 막혔나. `reason` 은 `actions[act].reason` 에 온다. */
  const findCode = (msg, code) => {
    const hit = [];
    for (const z of (msg.zones || [])) {
      for (const [act, a] of Object.entries(z.actions || {})) {
        if (a && a.reason === code) hit.push(z.id + '/' + act);
      }
    }
    return hit;
  };

  let seen = null, seenAt = null, states = 0;
  const codes = new Map();
  const t0 = Date.now();
  while (Date.now() - t0 < SECONDS * 1000) {
    for (const e of rx.splice(0)) {
      if (!e.m || e.m.type !== 'state') continue;
      states += 1;
      for (const z of (e.m.zones || [])) {
        for (const a of Object.values(z.actions || {})) {
          if (a && a.reason) codes.set(a.reason, (codes.get(a.reason) || 0) + 1);
        }
      }
      const hit = findCode(e.m, 'node_unregistered');
      if (hit.length && !seen) {
        seen = hit; seenAt = e.t;
        /* 🔴 **그 순간 화면을 같이 뜬다.** 나중에 읽으면 등록이 끝나 배너가 이미 사라진다. */
        const dom = await evaluate(client, `(() => {
          const b = document.getElementById('slots-banner');
          return { bannerHidden: b ? b.hidden : null, bannerText: b ? b.textContent.slice(0, 140) : null,
                   locked: [...document.querySelectorAll('#zone-grid .zbtn')].filter(x => x.getAttribute('aria-disabled') === 'true').length,
                   marks: [...new Set([...document.querySelectorAll('#zone-grid .zbtn')].map(x => (x.textContent.match(/[⏱⚠⏳]/) || [''])[0]).filter(Boolean))] };
        })()`).catch(e2 => ({ err: String(e2) }));
        console.log('🔴 ' + seenAt + ' — `node_unregistered` 잡혔다 → ' + JSON.stringify(hit));
        console.log('   그 순간 화면 → ' + JSON.stringify(dom));
        /* 🔑 배너 규칙(원장 §5.59): `node_unregistered` 는 `unknown` 계열 → **배너가 센다.** */
        const okBanner = dom && dom.bannerHidden === false;
        console.log((okBanner ? '   ✅' : '   ❌') + ' 배너가 이 자리를 미상으로 센다 (분류 §5.59: unknown)');
        break;
      }
    }
    if (seen) break;
    await sleep(50);
  }

  console.log('\n· 관측한 state 봉투 ' + states + '장 · 나온 거절 코드 → '
    + JSON.stringify(Object.fromEntries(codes)));
  if (seen) console.log('\n결과: ✅ 실물에서 `node_unregistered` 를 봤다 (' + seenAt + ')');
  else {
    console.log('\n결과: ⏭ **미측정** — 이 창에서 `node_unregistered` 가 안 왔다.');
    console.log('       ⚠ "그 코드가 안 온다"가 아니다. **재접속이 없었거나 내가 그 1초를 놓쳤다**는 뜻이다.');
    console.log('       (재접속이 있었는지는 서버 로그가 답한다 — socket 에 확인해라.)');
  }
} catch (e) {
  console.log('\n💥 중단: ' + (e && e.message ? e.message : String(e)));
  process.exitCode = 1;
} finally {
  if (client) await client.close().catch(() => {});
}
