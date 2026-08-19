/**
 * screen-build.mjs — 화면(index.html)의 **판본 표지** 한 곳 (REQ-0240 · 2026-08-19)
 *
 * 🔴 왜 있나 — `srv_id` 의 화면 판이다.
 * 서버는 자기 판본을 `srv_id` 로 말해 주는데 **화면에는 그런 수단이 없었다.**
 * 그래서 :9900 이 08-17 사본을 이틀간 내주는 동안 **아무 신호도 없었다**(REQ-0240).
 * 이제 "화면이 최신인가"가 **기억이 아니라 값**으로 답해진다.
 *
 * ⚠ **표지를 재는 규칙이 두 곳에 있으면 두 판정자가 생긴다**(CLAUDE.md §"계산 주체").
 * 그래서 정규화·해시·파싱을 **이 파일 하나에만** 두고 배포 도구와 하니스가 같이 쓴다.
 */
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

/** 저장소 원본의 표지 값. 배포 도구를 안 거쳤다는 뜻. */
export const UNSTAMPED = '__UNSTAMPED__';

/** `<meta name="screen-build" content="...">` — 표지 한 줄. */
const META_RE = /(<meta\s+name="screen-build"\s+content=")([^"]*)(">)/;

/**
 * 🔴 **표지 자리를 `__UNSTAMPED__` 로 되돌린 뒤** 해시를 잰다.
 *
 * 자기 자신을 포함해 재면 **찍는 순간 값이 바뀌어 영원히 안 맞는다**(자기참조).
 * 정규화하면 저장소 원본과 배포본이 **같은 바이트로 수렴**하므로 둘을 직접 비교할 수 있다.
 * 부수 효과로 **배포본을 손으로 고친 것**도 잡힌다 — 표지 밖이 바뀌면 해시가 갈린다.
 */
export function canonicalize(html) {
  return html.replace(META_RE, (_, a, __, c) => a + UNSTAMPED + c);
}

/** 정규화한 내용의 sha256 앞 12자리. 이것이 "이 화면이 무엇인가"의 정본이다. */
export function contentSha(html) {
  return createHash('sha256').update(canonicalize(html), 'utf8').digest('hex').slice(0, 12);
}

/**
 * HTML 에서 표지를 읽는다.
 * @returns {{present:boolean, raw:string|null, src:string|null, git:string|null, at:string|null}}
 */
export function readStamp(html) {
  const m = html.match(META_RE);
  if (!m) return { present: false, raw: null, src: null, git: null, at: null };
  const raw = m[2];
  const pick = (k) => {
    const mm = raw.match(new RegExp('(?:^|\\s)' + k + '=([^\\s]+)'));
    return mm ? mm[1] : null;
  };
  return { present: true, raw, src: pick('src'), git: pick('git'), at: pick('at') };
}

/** 배포본에 찍을 표지 문자열을 만든다. */
export function makeStamp({ src, git, at }) {
  return 'src=' + src + ' git=' + git + ' at=' + at;
}

/** HTML 의 표지를 주어진 값으로 바꾼다. 표지가 없으면 null (= 원본에 표지를 먼저 넣어야 한다). */
export function applyStamp(html, stamp) {
  if (!META_RE.test(html)) return null;
  return html.replace(META_RE, (_, a, __, c) => a + stamp + c);
}

/**
 * 두 HTML 이 **같은 화면인가**를 판정한다.
 *
 * 🔑 **긍정형이다** — *"옛 판이 아니다"* 가 아니라 **`받은 내용 == 기대 내용`**.
 * 부정형은 대상이 사라져도 참이 된다(원장 §5.68 · 헛통과 셋째 형태).
 */
export function compare(servedHtml, sourceHtml) {
  const served = contentSha(servedHtml);
  const source = contentSha(sourceHtml);
  const stamp = readStamp(servedHtml);
  return {
    same: served === source,
    servedSha: served,
    sourceSha: source,
    stamp,
    /* 표지가 자기 내용과 맞는가. 배포 뒤 내용만 바뀌면 표지가 거짓말을 한다. */
    stampHonest: stamp.src === null ? null : stamp.src === served,
  };
}

export async function readHtml(path) {
  return readFile(path, 'utf8');
}

/**
 * 저장소 원본의 절대 경로. 🔑 **cwd 가 아니라 이 파일 위치 기준**이라 어디서 실행해도 같은 것을 본다.
 * (cwd 상대경로가 무엇을 만드는지는 REQ-0240 이 보여 줬다 — 서버가 바로 그것 때문에 08-17 판을 냈다.)
 */
export const SOURCE_HTML = fileURLToPath(new URL('../../조별과제샘플/index.html', import.meta.url));

/**
 * 🔴 **브라우저로 재는 하니스의 첫 줄에 넣어라.**
 *
 * *"지금 보고 있는 페이지가 내가 만든 그 페이지인가"* 를 **내용 해시로** 묻는다.
 * 아니면 **던진다** — 낡은 화면으로 잰 값은 제품 판정이 아니라 남의 판 판정이기 때문이다.
 *
 * ⚠ **존재형(`zone-grid` 가 있나?)으로는 부족하다**(원장 §5.85): 기능이 통째로 없는 08-17 사본은
 * 잡지만 **`zone-grid` 를 가진 어제 사본은 통과한다.** 물어야 할 것은 "있나"가 아니라 "같은가"다.
 *
 * @param {string} url 페이지 URL. 쿼리(`?demo=1`)와 `/` 끝은 알아서 정리한다.
 */
const _checked = new Map();

export function assertServedIsCurrent(url, opts = {}) {
  /* 🔑 URL 당 한 번만 잰다 — 페이지를 여러 번 여는 하니스(blink 등)가 매번 긁지 않게. */
  let u = url.split('?')[0];
  if (u.endsWith('/')) u += 'index.html';
  if (!_checked.has(u)) _checked.set(u, _assertServedIsCurrent(u, opts));
  return _checked.get(u);
}

async function _assertServedIsCurrent(u, { log = console.log, source = SOURCE_HTML } = {}) {
  const [servedHtml, sourceHtml] = await Promise.all([
    fetch(u).then((r) => {
      if (!r.ok) throw new Error('HTTP ' + r.status + ' — ' + u);
      return r.text();
    }),
    readFile(source, 'utf8'),
  ]);
  const c = compare(servedHtml, sourceHtml);
  log('  · 서빙 판본 ' + c.servedSha + ' / 원본 ' + c.sourceSha
    + '   표지: ' + (c.stamp.present ? c.stamp.raw : '(표지 없음)'));
  if (!c.same) {
    throw new Error(
      '🔴 서빙된 화면이 저장소 원본과 다르다 (' + c.servedSha + ' ≠ ' + c.sourceSha + ') — '
      + '이 관측은 무효다. `node web/tools/deploy-screen.mjs --check` 로 어느 파일이 서빙되는지 봐라. '
      + '제품 판정으로 읽지 마라.');
  }
  return c;
}
