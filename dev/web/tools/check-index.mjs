/**
 * index.html 정적 검사 — 브라우저 없이 돌릴 수 있는 부분만 본다.
 *
 * 이 파일이 확인하는 것은 두 가지뿐이다:
 *   1. 인라인 스크립트가 **구문상 파싱되는가** (실행하지 않는다)
 *   2. WS 포트 해석 규칙이 의도대로인가 — 실제 파일에서 함수를 뽑아 단언한다
 *
 * ⚠ 여기서 통과하는 것은 "브라우저에서 동작한다"는 뜻이 **아니다.**
 *    렌더링·이벤트·실접속은 web/tools/e2e.mjs 가 진짜 크롬으로 확인한다.
 *
 * 사용: node web/tools/check-index.mjs
 */
import { readFileSync } from 'node:fs';

/* 🔴 `--file <경로>` — 기본은 **지금 쓰는 화면**(서머리)이다.
   동결 트리(§3-D)에 돌리면 **동결된 사실만 확인**하게 되므로 기본으로는 안 간다.
   굳이 옛 화면을 보려면 경로를 명시해라. */
const fileArg = (() => { const i = process.argv.indexOf('--file'); return i >= 0 ? process.argv[i + 1] : null; })();
const TARGET = fileArg
  ? new URL('file://' + (fileArg.startsWith('/') ? fileArg : process.cwd() + '/' + fileArg))
  : new URL('../../서머리/server/index.html', import.meta.url);
const HTML = TARGET;
const html = readFileSync(HTML, 'utf8');

let pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; console.log('  ✅ ' + name); }
  else { fail++; console.log('  ❌ ' + name + (detail ? '  → ' + detail : '')); }
}
function eq(name, got, want) {
  ok(name + '  (' + JSON.stringify(got) + ')', got === want, '기대 ' + JSON.stringify(want));
}

/* ── 1. 인라인 스크립트 구문 검사 ───────────────────────────────── */

/* 마지막 <script> 블록이 본체다. src= 가 붙은 것은 없다(의존성 없는 정적 페이지).
   🔴 **HTML 주석을 먼저 걷어낸다.** 주석 안에 태그 이름이 글자로 적혀 있으면
   (예: 파일 머리에서 "구획은 script 첫머리의 목차가 말한다" 처럼) 정규식이 그것을
   **진짜 여는 태그로 잡아** 주석·style·body 전체를 JS 로 컴파일하려 든다 →
   `Invalid left-hand side expression` 같은 엉뚱한 문법 오류가 난다.
   ⚠ 브라우저는 안 걸린다(주석 안이니까). **도구만 걸린다** — 2026-08-26 실측. */
const htmlNoComment = html.replace(/<!--[\s\S]*?-->/g, '');
const blocks = [...htmlNoComment.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g)].map(m => m[1]);
console.log('\n[1] 인라인 스크립트 구문');
ok('script 블록을 찾았다 (' + blocks.length + '개)', blocks.length > 0);
const src = blocks.join('\n;\n');
try {
  new Function(src);                      // 컴파일만 — 실행하지 않는다
  ok('전체 파싱 통과 (' + src.length + '자)', true);
} catch (e) {
  ok('전체 파싱 통과', false, e.message);
}

/* ── 2. WS 포트 해석 규칙 ──────────────────────────────────────── */

// 실제 파일에서 함수 본문을 그대로 뽑는다 — 여기에 복사본을 두면 검사가 거짓말을 하게 된다.
function grab(re, what) {
  const m = src.match(re);
  if (!m) { console.log('  ❌ ' + what + ' 를 파일에서 찾지 못했다 — 이름이 바뀌었나?'); fail++; return ''; }
  return m[0];
}
const parts = [
  grab(/const WS_PORT_DEFAULT = \d+;/, 'WS_PORT_DEFAULT'),
  grab(/const WS_PATH\s*=\s*'[^']*';/, 'WS_PATH'),
  grab(/function resolveWsPort\(\)[\s\S]*?\n}/, 'resolveWsPort()'),
  grab(/function wsPortFromPage\(bad\)[\s\S]*?\n}/, 'wsPortFromPage()'),
  grab(/function wsTargetUrl\(\)[\s\S]*?\n}/, 'wsTargetUrl()'),
].join('\n');

const make = new Function('window', parts + '\nreturn { resolveWsPort, wsTargetUrl };');
const at = (search, port, hostname = 'localhost', protocol = 'http:') =>
  make({ location: { search, port, hostname, protocol } });

console.log('\n[2] WS 포트 해석 — 기본 규칙');
eq('페이지가 :10000 이면 WS 도 10000',        at('', '10000').resolveWsPort().port, '10000');
eq('페이지가 :9900 이면 WS 도 9900',          at('', '9900').resolveWsPort().port,  '9900');
eq('포트 없이 열리면 문서화된 기본값 9900',    at('', '').resolveWsPort().port,      '9900');
eq('URL 전체가 페이지 포트를 따른다',
   at('', '10000', '127.0.0.1').wsTargetUrl(), 'ws://127.0.0.1:10000/ws');

console.log('\n[3] ?ws= 명시 지정');
eq('?ws=9900 은 페이지 포트를 이긴다',        at('?ws=9900', '10000').resolveWsPort().port, '9900');
eq('출처가 query 로 표시된다',                 at('?ws=9900', '10000').resolveWsPort().from, 'query');
eq('?demo=1 만 있으면 페이지 포트',            at('?demo=1', '10000').resolveWsPort().port, '10000');

console.log('\n[4] 잘못된 ?ws= 는 조용히 넘기지 않는다');
for (const bad of ['abc', '0', '65536', '-1', '99.5', '', ' ']) {
  const r = at('?ws=' + encodeURIComponent(bad), '10000').resolveWsPort();
  // bad 의 "없음"은 null 로만 본다 — 빈 문자열은 실제로 오는 잘못된 값이라
  // 참/거짓 검사로는 `?ws=` 하나가 경고 없이 새어 나간다(실제로 이 검사가 그걸 잡았다).
  ok('?ws=' + JSON.stringify(bad) + ' → 페이지 포트로 떨어지고 bad 를 들고 간다',
     r.port === '10000' && r.bad !== null, JSON.stringify(r));
}

console.log('\n[5] 🔴 회귀 방지 — 시험 인스턴스에서 운영 포트로 새지 않는다');
{
  // 이 한 줄이 이 파일이 존재하는 이유다. 깨지면 관측 창이 깨진다.
  const r = at('', '10000').wsTargetUrl();
  ok('시험 페이지(:10000)가 운영(:9900)으로 붙지 않는다', !r.includes(':9900'), r);
}
{
  const r = at('', '10000', '127.0.0.1').wsTargetUrl();
  ok('하드코딩된 포트 상수를 쓰지 않는다 (connect 가 wsTargetUrl 을 부른다)',
     /const url = wsTargetUrl\(\);/.test(src) && r === 'ws://127.0.0.1:10000/ws');
}

console.log('\n[6] 🔴 하행 큐 거절 코드 둘이 자기 문구를 갖는다 (REQ-0155 계약 · 원본 DESIGN-server-slot-queue.md §4-B)');
{
  /* 파일에서 실제 객체를 뽑는다 — 복사본을 두면 검사가 거짓말을 한다([2] 와 같은 이유).
     이 검사의 형태가 중요하다: 코드가 없던 동안 빨간불이고 넣으면 저절로 초록이며
     그 뒤로는 회귀 감시가 된다. "결함이 있다"를 단언하는 검사는 고치는 순간
     거짓 경보가 된다 — 그 함정을 한 번 밟았다(LEDGER §5.3). */
  const lit = grab(/const ERROR_TEXT = \{[\s\S]*?\n\};/, 'ERROR_TEXT');
  let T = null;
  try { T = new Function(lit + '\nreturn ERROR_TEXT;')(); }
  catch (e) { ok('ERROR_TEXT 를 파일에서 뽑아 평가했다', false, e.message); }

  if (T) {
    for (const code of ['queue_full', 'already_pending']) {
      const v = T[code];
      ok(code + ' 에 문구가 있다', typeof v === 'string' && v.trim().length > 0,
         '없으면 화면이 "오류가 발생했습니다" 로 떨어져 사용자 잘못으로 읽힌다 → 재클릭 → 큐 증폭');
    }
    // 계약이 요구하는 것은 "있다"가 아니라 **갈린다**는 것이다(시스템 사정 대 사용자 잘못 아님).
    ok('두 문구가 서로 다르다', T.queue_full !== T.already_pending, JSON.stringify(T.queue_full));
    /* 복사-붙여넣기로 다른 코드의 문구를 재사용하면 원인을 못 가른다.
       🔵 다만 **같은 상태에 이름이 둘인 쌍**은 예외다 — 문구가 같아야 맞다.
          `already_pending`(예약 이름공간) 과 `pending`(actions 이름공간) 이 그것이다:
          socket 이 사유 코드가 **두 이름공간**이라고 정정했고 통일은 다음 개정으로 미뤘다.
          그때까지 화면은 **두 이름을 다 받고 같은 문장을 낸다** — 그것이 계약이다.
       ⚠ 예외를 늘릴 때는 "같은 상태인가" 로 판단해라. **문구가 같아서** 넣으면 검사가 죽는다. */
    const SAME_STATE_PAIRS = [['already_pending', 'pending']];
    const vals = Object.entries(T)
      .filter(([k]) => !SAME_STATE_PAIRS.some((pair) => pair.indexOf(k) > 0))   /* 쌍의 둘째 이름만 뺀다 */
      .map(([, v]) => v);
    ok('모든 error code 가 서로 다른 문구를 쓴다 (' + vals.length + '개 · 같은 상태의 두 이름 '
       + SAME_STATE_PAIRS.length + '쌍 제외)',
       new Set(vals).size === vals.length, JSON.stringify(vals));
    /* 🔑 예외가 **실제로 그 쌍인지** 확인한다 — 안 그러면 예외가 검사를 조용히 무력화한다 */
    for (const [a2, b2] of SAME_STATE_PAIRS) {
      if (T[a2] !== undefined && T[b2] !== undefined) {
        ok('🔑 `' + a2 + '` 와 `' + b2 + '` 는 **같은 문장**이다 (같은 상태의 두 이름)',
           T[a2] === T[b2], JSON.stringify([T[a2], T[b2]]));
      }
    }
  }
}

console.log('\n' + (fail === 0 ? '통과' : '실패') + ' — ' + pass + ' pass / ' + fail + ' fail\n');
process.exit(fail === 0 ? 0 : 1);
