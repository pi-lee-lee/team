/**
 * release-sync.mjs — **`릴리즈/web/` 이 정본과 같은지** 값으로 답한다.
 *
 * ## 🔴 왜 있나 — 사본이 둘이면 갈린다
 * ```
 * 정본 : 서머리/server/*.html   ← **지금 도는 서버가 읽는 파일**
 * 사본 : 릴리즈/web/*.html      ← 릴리즈의 사진
 * ```
 * 🔴 갈리면 **릴리즈가 조용히 옛것이 된다.** 그리고 아무도 모른다 —
 * 릴리즈 폴더는 평소에 아무도 안 열기 때문이다.
 *
 * > ★ 루트 완료 기준의 *"지금 도는 것과 같은 소스인가"* 가 이 검산이다.
 * > 🔑 **믿음이 아니라 값으로 지킨다.**
 *
 * ```
 * node web/tools/release-sync.mjs           검사만 (기본) — 다르면 exit 1
 * node web/tools/release-sync.mjs --sync    정본 → 릴리즈로 맞춘다
 * ```
 * ⚠ **방향은 한쪽뿐이다** — 정본이 `서머리/server/` 다. 릴리즈에서 고치고 되돌리지 마라.
 *   🔑 릴리즈는 **사진**이고 사진을 고쳐서 원본을 바꾸지 않는다.
 */
import { readFile, writeFile, stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';

const SYNC = process.argv.includes('--sync');
/* 🔴 **사본이 늘면 갈릴 자리도 는다.** 지금 둘이다:
   ```
   릴리즈/web/              폴더 배포용 (서버가 `<실행파일>/../web` 으로 읽는다)
   릴리즈/VS_server/server/  🔴 **윈도우 VS 로 여는 트리** — 사용자가 거기서 빌드·실행한다
   ```
   ⚠ VS 쪽이 **8/22 판으로 닷새 낡아 있었다**(정본보다 123,810 B 적었다).
     소유권 note 가 그것을 예언하고 있었다 — *"갱신 주체와 소유자를 갈라 놓으면 반드시 낡는다."*

   🔴 그리고 **`mine: false` 는 web 이 그 파일을 못 쓴다는 뜻이다.**
   소유권 규칙이 **`VS_server` 아래의 `index.html` 만** web 으로 갈라 놨고,
   ⚠ (그 규칙을 글롭 그대로 못 적는다 — 별표·슬래시가 **이 블록 주석을 닫는다.**
      `.claude/ownership.json` 을 보라. 오늘 이 함정을 세 번째로 밟았다)
   `user8080/8081.html` 은 **그 규칙이 생길 때 없던 파일**이라 socket 소유로 떨어진다.
   ★ **우회하지 않는다.** 검사는 하고 **고치지는 않는다** — 갈렸으면 담당에게 REQ 를 낸다.
   🔑 도구가 소유권을 **알고 있어야** 한다. 모르면 훅에 막혀 죽고, 그때 사람은 도구를 의심한다. */
const PAIRS = [
  { src: '서머리/server/index.html',    dst: '릴리즈/web/index.html',    what: '관제(9900)',              mine: true },
  { src: '서머리/server/user8080.html', dst: '릴리즈/web/user8080.html', what: '이용자 · 주차위치 확인(8080)', mine: true },
  { src: '서머리/server/user8081.html', dst: '릴리즈/web/user8081.html', what: '이용자 · 자리 예약(8081)',    mine: true },
  { src: '서머리/server/index.html',    dst: '릴리즈/VS_server/server/index.html',    what: 'VS · 관제',          mine: true },
  { src: '서머리/server/user8080.html', dst: '릴리즈/VS_server/server/user8080.html', what: 'VS · 주차위치 확인', mine: false, who: 'socket' },
  { src: '서머리/server/user8081.html', dst: '릴리즈/VS_server/server/user8081.html', what: 'VS · 자리 예약',    mine: false, who: 'socket' },
];
const sha = (b) => createHash('sha256').update(b).digest('hex').slice(0, 12);

let same = 0, diff = 0, missing = 0;
console.log('\n🔎 릴리즈 화면이 정본과 같은가 — 정본은 **서머리/server/**');
for (const p of PAIRS) {
  let a = null, b = null;
  try { a = await readFile(p.src); } catch { console.log('  🔴 정본이 없다: ' + p.src); missing++; continue; }
  try { b = await readFile(p.dst); } catch { b = null; }
  if (b === null) {
    missing++;
    console.log('  🔴 없다: ' + p.dst + '  (' + p.what + ')');
    if (!p.mine) { console.log('     ⛔ **' + p.who + ' 소유라 web 이 못 만든다** — REQ 로 요청해라'); continue; }
    if (SYNC) { await writeFile(p.dst, a); console.log('     → 넣었다  ' + sha(a)); }
    continue;
  }
  if (a.equals(b)) { same++; console.log('  ✅ ' + p.what.padEnd(28) + sha(a)); }
  else {
    diff++;
    console.log('  🔴 ' + p.what.padEnd(28) + '정본 ' + sha(a) + '  ≠  릴리즈 ' + sha(b));
    /* 🔑 **어느 쪽이 새것인지 값으로 보인다** — 사람이 "아마 정본이 새것" 이라고 짐작하지 않게.
       ⚠ 그래도 방향은 언제나 정본 → 릴리즈다. 이 줄은 **얼마나 벌어졌나**를 말할 뿐이다. */
    try {
      const [sa, sb] = [await stat(p.src), await stat(p.dst)];
      const gap = Math.round((sa.mtimeMs - sb.mtimeMs) / 1000);
      console.log('     · 정본이 ' + (gap >= 0 ? gap + '초 새것' : (-gap) + '초 옛것')
                + ' · 크기 ' + a.length + ' vs ' + b.length + 'B');
    } catch { }
    if (!p.mine) { console.log('     ⛔ **' + p.who + ' 소유라 web 이 못 고친다** — REQ 로 요청해라'); continue; }
    if (SYNC) { await writeFile(p.dst, a); console.log('     → 맞췄다  ' + sha(a)); }
  }
}

console.log('\n' + '─'.repeat(56));
if (SYNC) {
  console.log('  맞췄다 — 같음 ' + same + ' · 고침 ' + (diff + missing));
  console.log('  ⚠ **고친 뒤 하니스를 릴리즈 파일로 다시 돌려라** — 복사만으로 도는 것을 증명하지 않는다:');
  console.log('     node web/tools/boards.mjs --file 릴리즈/web/index.html');
} else if (diff || missing) {
  console.log('  🔴 갈렸다 — 같음 ' + same + ' · 다름 ' + diff + ' · 없음 ' + missing);
  console.log('     → `node web/tools/release-sync.mjs --sync` 로 맞춘다 (방향: 정본 → 릴리즈)');
} else {
  console.log('  ✅ ' + same + '/' + PAIRS.length + ' 다 같다 — 릴리즈가 지금 도는 화면과 같은 소스다');
}
process.exit(!SYNC && (diff || missing) ? 1 : 0);
