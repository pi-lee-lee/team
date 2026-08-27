// lot.h — 🔴 **주차장 지형이 스스로 답한다**
//
// 사용자 확정: *"주차자리와 디바이스노드는 **개별구조다. 연결되지 않는다.**"*
// 이 파일이 그 문장의 코드 판본이다 — 여기에는 소켓도, 프레임도, 전송도 없다.
//
// 🔑 무엇을 안에 넣나 : 지형 목록 · 판(epoch) · **결속 규칙**(어느 센서가 어느 자리인가)
// 🔑 무엇을 밖에 남기나 : **로그와 방송.** 판이 올랐다는 것을 *알리는* 일은 전송이고 서버의 몫이다
//   ⚠ `bump_epoch()` 안에서 `logf` 와 `push_map()` 을 같이 부르지 마라.
//     그대로 옮기면 이 클래스가 **다시 서버를 알게 된다.** 그래서 갈랐다.
//
// ⚠ **전선에 안 닿는다** — 프레임 형식·타이밍을 하나도 안 건드린다.
//   그래서 잠금 계약(`firmware-esp-link`)과 무관하고, 그 축부터 쪼개는 순서에 맞는다.
#ifndef LOT_H
#define LOT_H

#include <string>
#include <vector>
#include <utility>

class Lot {
public:
    Lot() : epoch_(0) {}

    // ── 지형 ────────────────────────────────────────────────────────────────
    void clear()                        { zones_.clear(); }
    void add(const Zone& z)             { zones_.push_back(z); }
    std::vector<Zone>&       zones()       { return zones_; }
    const std::vector<Zone>& zones() const { return zones_; }
    int  epoch() const                  { return epoch_; }
    int  bumpEpoch()                    { return ++epoch_; }   // 🔑 **순수 증가.** 로그·방송은 부르는 쪽

    Zone* find(const std::string& id) {
        for (size_t i = 0; i < zones_.size(); i++)
            if (zones_[i].id == id) return &zones_[i];
        return 0;
    }
    // 🔑 const 판 — **읽기만 하는 판정**(자리 활성 여부 등)이 쓴다.
    //   이것이 없으면 그런 판정이 전부 non-const 가 되어 **호출자까지 번져 나간다.**
    const Zone* find(const std::string& id) const {
        for (size_t i = 0; i < zones_.size(); i++)
            if (zones_[i].id == id) return &zones_[i];
        return 0;
    }

    // ── 결속 규칙 — **어느 센서가 어느 자리인가** ───────────────────────────
    // 표(조립 API)가 있으면 **표가 답한다.** 없으면 이름 규칙으로 떨어진다.
    // 🔑 표가 생기면 *"B3 은 A3 의 둘째 센서"* 라는 **추측이 사실로 바뀐다.**
    // 🔴 **`devid` 를 같이 받는다**. 조립 표가 장치를 못 박을 수 있게 됐다.
    //   `Attach::devid` 가 비어 있으면 **아무 장치나** — 1인자 선언이 그 뜻이다.
    // ⚠ **첫 번째로 맞는 것을 돌려준다.** 같은 이름이 두 자리에 있으면 뒤엣 자리는 못 받는다 —
    //   그래서 `validate_assembly()` 가 기동 때 그것을 말한다.
    // 🔴🔴 **`claimedOther` 는 "표에 그 이름이 있는데 devid 가 남의 것" 을 알린다**
    //. 아래 폴백(`return nm`)은 원래 *"표에 없는 이름"* 을 위한 건데,
    //   **"아는 이름인데 남의 것"** 도 같은 갈래로 떨어진다 — 🔑 **그 둘은 다른 사실이다.**
    //   ⚠ 그리고 모듈 이름이 자리 id 와 같으면 폴백이 **그 자리를 찾아내** 조용히 붙인다:
    //     표에 `(F4,"A4")` 로 못 박아도 **`P2` 의 `A4` 가 자리 A4 에 붙었다.**
    //   🔑 **라우팅은 안 바꾼다**(실기 직전 · 거동 변화 0). **사실만 밖으로 내보낸다** —
    //     부르는 쪽이 그것을 시끄럽게 찍으면 **조용한 오결합이 보이는 오결합**이 된다.
    std::string zoneOfModule(const std::string& devid, const std::string& nm,
                             const ParkingLot* table, bool* claimedOther = 0) const {
        if (claimedOther) *claimedOther = false;
        if (table && !table->empty()) {
            const std::vector<ParkingLot::Area>& as = table->areas();
            bool nameDeclared = false;               // 표에 그 이름이 **있기는 했나**
            for (size_t i = 0; i < as.size(); i++)
                for (size_t k = 0; k < as[i].modules.size(); k++) {
                    const ParkingLot::Attach& at = as[i].modules[k];
                    if (at.name != nm) continue;
                    nameDeclared = true;
                    // 🔑 devid 가 비면 아무 장치나. 있으면 **그 장치일 때만**.
                    if (!at.devid.empty() && at.devid != devid) continue;
                    return as[i].id;
                }
            // 여기 왔다 = 표에서 **(devid, 이름) 짝**을 못 찾았다.
            // 🔴 그런데 **이름은 있었다면** 그것은 "모르는 이름" 이 아니라 **"남의 것을 주장"** 이다.
            if (nameDeclared && claimedOther) *claimedOther = true;
            // 🔴🔴 **표가 있으면 표가 전부다 — 이름 폴백을 안 탄다** (사용자 확정)
            //   *"기본구조상 모듈의 고유값은 **아두이노id + 모듈id** 이다. 그래서 C1 으로 통일하거나
            //     **임의로 변경하여도 정상구성**되어야 한다."*
            //
            // ⚠ 여기서 `return nm;` 으로 두지 마라 — **모듈 이름을 자리 id 로 삼게** 되고,
            //   그 경로는 **`devid` 를 아예 안 봤다.** 그래서 결과가 **이름에 따라 달라졌다**:
            //     `C1`(자리 id 와 안 겹침) → 못 찾는다 → **우연히** 안전
            //     `A3`(자리 id 와 겹침)   → 🔴 **아무 보드나 자리 A3 에 붙었다**
            //   ★ 즉 고유성이 **그 이름 집합에 대해서만** 성립했다 — 사용자가 *"단편적"* 이라고 한 것이 이것이다.
            //
            // 🔑 **빈 값을 돌려준다** → 부르는 쪽의 `find()` 가 실패해 **어디에도 안 붙는다.**
            //   그리고 그 사실이 `claimed_other`/`unbound` 로 **시끄럽게** 보고된다.
            return std::string();
        }
        // 🔑 **표 자체가 없을 때만** 이름 규칙으로 떨어진다(조립을 아직 안 한 판).
        return nameRule(nm);
    }

    // 이름 규칙(표가 없을 때) : `B3` → 자리 `A3` 의 둘째 센서
    static std::string nameRule(const std::string& nm) {
        if (nm.size() == 2 && nm[0] == 'B' && nm[1] >= '1' && nm[1] <= '5')
            return std::string("A") + nm[1];
        return nm;
    }

    // 노드의 모듈들을 자리에 결속한다.
    // 🔴 **로그를 여기서 찍지 않는다** — 충돌을 *결과로 돌려주고* 문구는 부르는 쪽이 만든다.
    //   그래야 이 클래스가 로그 형식을 모르고, 부르는 쪽이 얼마나 크게 알릴지 정한다.
    struct BindResult {
        bool changed;                                        // 지형이 바뀌었나(판을 올릴 이유)
        std::vector<std::pair<std::string, std::string> > conflicts;  // (자리 id, 이름)
        // 🔴🔴 **어느 자리에도 안 붙은 모듈** — (idx, 이름)
        //   ⚠ `if (!z) continue;` 로 **조용히 건너뛰지 마라.** 그러면 기여자가 이름을
        //   잘못 쓰면 **등록은 성공하고 자리에는 아무것도 안 붙고 아무도 안 알려 줬다.**
        //   ⚠ 다인원 협업에서 가장 나쁜 형태다 — **코드는 도는데 결과가 없다.**
        //   🔑 여기서는 *사실만* 실어 보낸다. 얼마나 크게 알릴지는 부르는 쪽이 정한다.
        std::vector<std::pair<size_t, std::string> > unbound;
        // 🔴 **선언된 것과 다른 보드가 그 이름으로 붙었다** — (자리 id, 이름).
        //   🔑 **거절이 아니다.** 라우팅 결과는 그대로이고, 이건 *"그런 일이 있었다"* 는 사실이다.
        //   ⚠ 실기에서 가장 밟기 쉬운 사고(보드를 바꿔 꽂아 굽기)가 **여기서만 보인다.**
        std::vector<std::pair<std::string, std::string> > claimed_other;
        BindResult() : changed(false) {}
    };
    BindResult bind(const std::string& devid,
                    const std::vector<std::pair<std::string, std::string> >& mods,
                    const ParkingLot* table) {
        BindResult r;
        for (size_t i = 0; i < mods.size(); i++) {
            bool claimedOther = false;
            Zone* z = find(zoneOfModule(devid, mods[i].first, table, &claimedOther));
            if (!z) {
                // 🔴 **둘 다 "안 붙었다" 지만 사유가 다르다** — 고치는 곳이 다르다:
                //   `claimed_other` : 표에 **그 이름이 있는데 devid 가 남의 것** → **스케치/굽기**를 봐라
                //   `unbound`       : 표에 **그 이름 자체가 없다**             → **조립표**를 봐라
                if (claimedOther) r.claimed_other.push_back(std::make_pair(std::string("(없음)"), mods[i].first));
                else              r.unbound.push_back(std::make_pair(i, mods[i].first));
                continue;
            }
            std::pair<std::string, std::string> key(devid, mods[i].first);
            bool dup = false, takenByOther = false;
            for (size_t k = 0; k < z->modules.size(); k++) {
                if (z->modules[k] == key) dup = true;
                else if (z->modules[k].second == key.second) takenByOther = true;
            }
            // 🔴 **먼저 잡은 노드를 유지한다.** 둘 다 받으면 그 자리 값이 어느 노드 것인지 못 가르고,
            //   조용히 덮으면 뒤엣것이 앞엣것을 지워 원인을 못 찾는다. `first-S-wins` 와 같은 규율.
            if (takenByOther) { r.conflicts.push_back(std::make_pair(z->id, key.second)); continue; }
            if (!dup) { z->modules.push_back(key); r.changed = true; }
        }
        return r;
    }

    // MULTI: module을 다른 Arduino로 옮길 수 있도록 끊긴 node의 실제 결속을 제거한다.
    bool unbindDevice(const std::string& devid) {
        bool changed = false;
        for (size_t zi = 0; zi < zones_.size(); zi++) {
            std::vector<std::pair<std::string, std::string> >& mods = zones_[zi].modules;
            for (size_t i = 0; i < mods.size(); ) {
                if (mods[i].first == devid) {
                    mods.erase(mods.begin() + i);
                    changed = true;
                } else {
                    i++;
                }
            }
        }
        return changed;
    }

private:
    std::vector<Zone> zones_;
    int epoch_;
};

#endif  // LOT_H
