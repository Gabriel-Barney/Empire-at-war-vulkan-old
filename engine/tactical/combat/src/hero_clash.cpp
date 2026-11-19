// s78n t06 - hero clashes: HEROCLASH.XML and the land mode's HeroClashManager (docs/re/HERO_CLASH.md §1-§6).
#include "alamo/tactical/combat/hero_clash.hpp"
#include "alamo/core/x86_int.hpp"

#include <algorithm>
#include <cmath>

#include "alamo/core/bytes.hpp"
#include "alamo/core/dmath.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/objects/type_fields.hpp"

namespace alamo::tactical::combat {

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

float number(std::string_view v, float fallback) { return v.empty() ? fallback : static_cast<float>(objects::c_atof(v)); }

bool yes(std::string_view v) { return iequal(v, "yes") || iequal(v, "true") || v == "1"; }

std::vector<std::string> split_list(std::string_view v) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : v) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// 0x20acd0's yaw: atan2 in degrees, [0, 360).
float yaw360(float dx, float dy) {
    float y = dmath::atan2(dy, dx) * (180.0f / dmath::kPiF);
    if (y < 0.0f) y += 360.0f;
    if (y >= 360.0f) y -= 360.0f;
    return y;
}

float norm360(float y) {
    while (y < 0.0f) y += 360.0f;
    while (y >= 360.0f) y -= 360.0f;
    return y;
}

float wrap180(float d) {
    while (d > 180.0f) d -= 360.0f;
    while (d <= -180.0f) d += 360.0f;
    return d;
}

float dist2(Vec3 a, Vec3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

Vec3 lerp(Vec3 a, Vec3 b, float s) { return {a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s}; }

void put_vec(ByteWriter& w, Vec3 v) { w.f32(v.x).f32(v.y).f32(v.z); }
Vec3 get_vec(ByteReader& r) {
    Vec3 v;
    v.x = r.f32();
    v.y = r.f32();
    v.z = r.f32();
    return v;
}

// Version 1: melee clashes only (also written while no action-driven clash runs, so those saves stay byte-identical);
// version 2 adds the action-driven clashes.
constexpr uint32_t kSaveVersion = 1;
constexpr uint32_t kSaveVersionActions = 2;

int32_t integer(std::string_view v, int32_t fallback) { return v.empty() ? fallback : objects::c_atoi(v); }

// "x0,y0, x1,y1, ..." (an odd trailing number is dropped [port]); sorted by x, stable (0x2c8660 sorts on first use).
ClashCurve curve(std::string_view v) {
    ClashCurve c;
    const std::vector<std::string> nums = split_list(v);
    for (size_t i = 0; i + 1 < nums.size(); i += 2)
        c.points.emplace_back(static_cast<float>(objects::c_atof(nums[i])), static_cast<float>(objects::c_atof(nums[i + 1])));
    std::stable_sort(c.points.begin(), c.points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return c;
}

int32_t relevance_type(std::string_view v) {
    if (iequal(v, "Constant")) return kRelevanceConstant;
    if (iequal(v, "Distance_From_Target_Curve")) return kRelevanceDistance;
    if (iequal(v, "Health_Percentage_Curve")) return kRelevanceHealth;
    if (iequal(v, "Distance_From_Target_Curve_Times_Health_Percentage_Curve")) return kRelevanceDistanceTimesHealth;
    return -1;
}

int32_t move_type(std::string_view v) {
    if (iequal(v, "Move_To_Target")) return kMoveToTarget;
    if (iequal(v, "Move_Past_Target")) return kMovePastTarget;
    if (iequal(v, "Dodge_Shot_From_Target")) return kMoveDodgeShot;
    return 0;  // Invalid
}

// One Clash_Actions child (the shared parser 0x5e9910 plus the class's fields, then the post-parse slot 12).
bool parse_action(data::XmlNode n, ClashAction& a) {
    const std::string_view kind = n.name();
    if (iequal(kind, "Attack_Action")) {
        a.kind = kActionAttack;
    } else if (iequal(kind, "Move_Action")) {
        a.kind = kActionMove;
    } else if (iequal(kind, "Special_Ability_Action")) {
        a.kind = kActionSpecialAbility;
    } else {
        return false;
    }
    a.name = std::string(n.attribute("Name"));
    // ActionClass fields (+0x28 .. +0x78), the same for the three classes.
    a.object_type = std::string(n.child_text("Action_Object"));
    a.target_type = std::string(n.child_text("Action_Target"));
    if (const data::XmlNode t = n.child("Action_Relevance_Type")) a.relevance_type = relevance_type(t.text_trimmed());
    a.relevance_constant = number(n.child_text("Action_Relevance_Constant"), 0.0f);
    a.distance_curve = curve(n.child_text("Action_Distance_From_Target_Curve"));
    a.health_curve = curve(n.child_text("Action_Health_Percentage_Curve"));
    switch (a.kind) {
        case kActionAttack:
            // Attack_Accuracy_Modifier (+0xe8) is parsed by retail and never read (§11), so the port does not keep it.
            a.projectile_type = std::string(n.child_text("Attack_Projectile_Type"));
            a.attack_anim = std::string(n.child_text("Attack_Animation_Type"));
            a.attack_subindex = integer(n.child_text("Attack_Animation_Subindex"), a.attack_subindex);
            a.attack_speedup = number(n.child_text("Attack_Animation_Speedup"), a.attack_speedup);
            if (const data::XmlNode b = n.child("Attack_Firing_Bone_Name")) a.firing_bone = std::string(b.text_trimmed());
            a.shots = integer(n.child_text("Attack_Number_Of_Shots"), a.shots);
            a.sfx = std::string(n.child_text("Attack_SFXEvent"));
            if (a.attack_subindex < 0) a.attack_subindex = kAnyVariant;  // post-parse 0x71f1a0
            break;
        case kActionMove:
            if (const data::XmlNode t = n.child("Move_Type")) a.move_type = move_type(t.text_trimmed());
            a.move_speed = number(n.child_text("Move_Speed"), a.move_speed);
            a.move_anim = std::string(n.child_text("Move_Animation_Type"));
            a.move_subindex = integer(n.child_text("Move_Animation_Subindex"), a.move_subindex);
            a.move_speedup = number(n.child_text("Move_Animation_Speedup"), a.move_speedup);
            a.anim_override = yes(n.child_text("Move_Animation_Override"));
            a.min_distance = number(n.child_text("Move_Min_Distance"), a.min_distance);
            a.max_distance = number(n.child_text("Move_Max_Distance"), a.max_distance);
            a.use_jetpack = yes(n.child_text("Move_Use_Jetpack"));
            a.wait_for_jetpack = yes(n.child_text("Wait_For_Finish_If_Target_Using_Jetpack"));
            if (a.move_subindex < 0) a.move_subindex = kAnyVariant;  // post-parse 0x5eaca0
            break;
        default:
            a.ability_name = std::string(n.child_text("Ability_Name"));
            a.cooldown_secs = number(n.child_text("Ability_Cooldown_Time_In_Secs"), 0.0f);
            // Post-parse 0x71f5f0: frames = round half away from zero (30 fps x seconds).
            a.cooldown_frames = static_cast<int32_t>(std::round(30.0f * a.cooldown_secs));
            break;
    }
    return true;
}

}  // namespace

float ClashCurve::value(float x) const {
    // 0x2c8660: lower bound (the first point with px >= x); the first point gives its y, else the linear blend with the
    // point before it, written as retail weights the two ends.
    const size_t n = points.size();
    if (n < 2 || x < points.front().first || x > points.back().first) return 0.0f;
    size_t i = 0;
    while (i < n && points[i].first < x) ++i;
    if (i == 0) return points[0].second;
    const auto [x1, y1] = points[i];
    const auto [x0, y0] = points[i - 1];
    const float span = x1 - x0;
    return ((x1 - x) / span) * y0 + ((x - x0) / span) * y1;
}

// ---- HEROCLASH.XML (§1) ---------------------------------------------------------------------------------------------

bool parse_hero_clash_xml(std::span<const uint8_t> bytes, HeroClashData& out, std::vector<std::string>* diag) {
    const data::XmlDocument doc = data::XmlDocument::parse(bytes, "HeroClash.xml");
    const data::XmlNode root = doc.root();
    if (!root || !iequal(root.name(), "Hero_Clashes")) return false;
    for (data::XmlNode n : root.children("Hero_Clash")) {
        HeroClashEntry e;
        e.name = std::string(n.attribute("Name"));
        const std::string_view ct = n.child_text("Clash_Type");
        e.clash_type = iequal(ct, "Melee_Duel") ? kClashMelee : iequal(ct, "Action_Driven") ? kClashActionDriven : -1;
        e.clash_range = number(n.child_text("Clash_Range"), 0.0f);
        e.play_conversation = yes(n.child_text("Play_Conversation_Events"));
        e.combat_distance = number(n.child_text("Combat_Distance"), 0.0f);
        e.damage_amount = number(n.child_text("Damage_Amount"), 0.0f);
        e.damage_percentage = number(n.child_text("Damage_Percentage"), 0.0f);
        e.first_win = number(n.child_text("First_Hero_Win_Exchange_Chance"), 0.0f);
        e.second_win = number(n.child_text("Second_Hero_Win_Exchange_Chance"), 0.0f);
        e.first_multiplier = number(n.child_text("First_Hero_Damage_Multiplier"), 0.0f);
        e.second_multiplier = number(n.child_text("Second_Hero_Damage_Multiplier"), 0.0f);
        e.first_type = std::string(n.child_text("First_Hero_Type"));
        e.second_type = std::string(n.child_text("Second_Hero_Type"));
        e.involved = split_list(n.child_text("Involved_Hero_Types"));
        static constexpr const char* kFirst[4] = {"First_Hero_Win_Anim_Type", "First_Hero_Lose_Anim_Type",
                                                  "First_Hero_Draw_Anim_Type", "First_Hero_Conversation_Anim_Type"};
        static constexpr const char* kSecond[4] = {"Second_Hero_Win_Anim_Type", "Second_Hero_Lose_Anim_Type",
                                                   "Second_Hero_Draw_Anim_Type", "Second_Hero_Conversation_Anim_Type"};
        for (size_t k = 0; k < 4; ++k) {
            e.first_anims[k] = std::string(n.child_text(kFirst[k]));
            e.second_anims[k] = std::string(n.child_text(kSecond[k]));
        }
        e.first_speech = std::string(n.child_text("First_Hero_Win_Speech"));
        e.second_speech = std::string(n.child_text("Second_Hero_Win_Speech"));
        if (const data::XmlNode acts = n.child("Clash_Actions")) {
            e.action_count = static_cast<uint32_t>(acts.child_count());
            for (data::XmlNode c : acts.children()) {
                ClashAction a;
                if (parse_action(c, a)) {
                    e.actions.push_back(std::move(a));
                } else if (diag) {
                    diag->push_back("hero clash " + e.name + ": " + std::string(c.name()) + " is not an action (ignored)");
                }
            }
        }
        // §1.1: first + second > 1 is the loader's error; both chances become 0.3.
        if (e.first_win + e.second_win > 1.0f) {
            if (diag) diag->push_back("hero clash " + e.name + ": win exchange chances add up to more than 1; both set to 0.3");
            e.first_win = 0.3f;
            e.second_win = 0.3f;
        }
        out.entries.push_back(std::move(e));  // file order, duplicate names kept (0x4814d0)
    }
    return true;
}

bool load_hero_clash_data(const FileProvider& files, HeroClashData& out, std::vector<std::string>* diag) {
    out = HeroClashData{};
    std::vector<uint8_t> bytes;
    if (!files.read("Data\\XML\\HEROCLASH.XML", bytes)) {
        if (diag) diag->push_back("hero clash: HEROCLASH.XML not found (no hero clashes)");
        return false;
    }
    return parse_hero_clash_xml(bytes, out, diag);
}

// ---- exchange rules (§3.6) ------------------------------------------------------------------------------------------

uint8_t clash_pick_outcome(uint8_t previous, float first_win, float second_win, SimRng& rng) {
    const float w[4] = {0.0f, first_win, second_win, 1.0f - first_win - second_win};
    float total = 0.0f;
    for (uint8_t k = 1; k <= 3; ++k)
        if (k != previous) total += w[k];
    float r = rng.range_float(0.0f, total);  // the one synced draw of the exchange (0x532480)
    for (uint8_t k = 1; k <= 3; ++k) {
        if (k == previous) continue;
        r -= w[k];
        if (r <= 0.0f) return k;
    }
    return kExchangeDraw;
}

int32_t clash_exchange_damage(const HeroClashEntry& e, float loser_start_health, bool winner_is_first_type, bool winner_is_second_type) {
    const float mult = winner_is_first_type ? e.first_multiplier : winner_is_second_type ? e.second_multiplier : 0.0f;
    const float raw = (loser_start_health * e.damage_percentage + e.damage_amount) * mult;
    return static_cast<int32_t>(std::round(raw));  // round half away from zero
}

// ---- manager (§2) -----------------------------------------------------------------------------------------------------

void HeroClashManager::set_data(const HeroClashData* data, std::vector<std::pair<int32_t, int32_t>> types) {
    data_ = data;
    types_ = std::move(types);
    if (data_) types_.resize(data_->entries.size(), {-1, -1});
}

std::vector<std::pair<ObjectId, ObjectId>> HeroClashManager::pairs() const {
    std::vector<std::pair<ObjectId, ObjectId>> out;
    out.reserve(clashes_.size());
    for (const Clash& c : clashes_) out.emplace_back(c.hero1, c.hero2);
    // An action-driven clash pairs its objects the same way (the pairing rule 0x39a540 asks whether the first clash
    // involving U involves X); the port's clashes hold two objects (team containers are no candidates).
    for (const ActionClash& c : action_clashes_)
        if (c.objects.size() >= 2) out.emplace_back(c.objects[0], c.objects[1]);
    return out;
}

void HeroClashManager::clear() {
    if (!clashes_.empty() || !action_clashes_.empty()) changed_ = true;
    clashes_.clear();
    action_clashes_.clear();
    ended_.clear();
}

void HeroClashManager::update(uint32_t frame, HeroClashHost& host, SimRng& rng) {
    if (!data_) return;
    detect(frame, host);
    // Every running clash in list order; a clash may end itself (removed here, the next one still runs).
    for (size_t i = 0; i < clashes_.size();) {
        if (update_clash(clashes_[i], frame, host, rng)) {
            ++i;
        } else {
            clashes_.erase(clashes_.begin() + static_cast<std::ptrdiff_t>(i));
            changed_ = true;
        }
    }
    for (size_t i = 0; i < action_clashes_.size();) {
        if (update_action_clash(action_clashes_[i], static_cast<int32_t>(frame), host, rng)) {
            ++i;
        } else {
            action_clashes_.erase(action_clashes_.begin() + static_cast<std::ptrdiff_t>(i));
            ++action_ended_;
            changed_ = true;
        }
    }
}

bool HeroClashManager::candidate_ok(HeroClashHost& host, ObjectId id, int32_t other_type) const {
    // 0x56c540 (an object that is not a team container): not on an active jet pack, not dying / a death clone /
    // removed, not in a clash, and no attack target or one of the other hero type.
    if (!host.alive(id) || host.in_clash(id) || host.jetpacking(id)) return false;
    const ObjectId t = host.attack_target(id);
    return t == kNoObject || host.type_of(t) == other_type;
}

void HeroClashManager::detect(uint32_t frame, HeroClashHost& host) {
    if (frame % 15u != 0u) return;  // 0x56c790: every 15th frame; no draws
    std::vector<ObjectId> a_list, b_list;
    for (uint32_t i = 0; i < data_->entries.size(); ++i) {
        const HeroClashEntry& e = data_->entries[i];
        // §4: Action_Driven entries start like melee ones (shipped Clash_Range 0.0: retail needs two coincident positions),
        // only with a host that carries out the action runner's effects (HeroClashHost::action_clashes; else the s78n t06
        // [port] rule "parse, never start" holds). An entry without a known type never starts in retail either.
        const bool action = e.clash_type == kClashActionDriven;
        if (e.clash_type != kClashMelee && !(action && host.action_clashes())) continue;
        const auto [t1, t2] = types_[i];
        if (t1 < 0 || t2 < 0) continue;
        a_list.clear();
        b_list.clear();
        host.candidates(t1, a_list);
        // s78n t06 [port]: ascending object id (retail: the object manager's list order, creation order in practice).
        std::sort(a_list.begin(), a_list.end());
        std::erase_if(a_list, [&](ObjectId id) { return !candidate_ok(host, id, t2); });
        if (a_list.empty()) continue;
        host.candidates(t2, b_list);
        std::sort(b_list.begin(), b_list.end());
        std::erase_if(b_list, [&](ObjectId id) { return !candidate_ok(host, id, t1); });
        const float r2 = e.clash_range * e.clash_range;
        for (const ObjectId a : a_list) {
            for (size_t k = 0; k < b_list.size(); ++k) {
                const ObjectId b = b_list[k];
                if (dist2(host.position(a), host.position(b)) > r2) continue;
                if (host.in_clash(a) || host.in_clash(b)) continue;  // the involved list: any +0x3b4 == 1 -> skip
                if (action ? start_action_clash(i, a, b, host) : start(i, a, b, host, frame)) {
                    b_list.erase(b_list.begin() + static_cast<std::ptrdiff_t>(k));  // each B joins at most one clash
                    break;                                                       // next a
                }
            }
        }
    }
}

bool HeroClashManager::start(uint32_t entry, ObjectId a, ObjectId b, HeroClashHost& host, uint32_t frame) {
    (void)frame;
    // 0x6ac780: hero1 / hero2 = the involved objects of First / Second_Hero_Type (the first match each).
    const auto [t1, t2] = types_[entry];
    const ObjectId involved[2] = {a, b};
    ObjectId h1 = kNoObject, h2 = kNoObject;
    for (const ObjectId id : involved)
        if (h1 == kNoObject && host.type_of(id) == t1) h1 = id;
    for (const ObjectId id : involved)
        if (h2 == kNoObject && host.type_of(id) == t2) h2 = id;
    if (h1 == kNoObject || h2 == kNoObject) return false;
    if (!host.hero_type(t1) || !host.hero_type(t2)) return false;
    const int32_t o1 = host.owner(h1), o2 = host.owner(h2);
    if (o1 == o2 || !host.enemies(o1, o2)) return false;
    // TARGETING suspended and +0x3b4 = 1 on the hero objects only (not their team containers); the host locks the
    // movement orders with it (Begin 0x6abde0's 0x3a8e50(h, 1): the port's coordinator creation never fails).
    host.set_in_clash(h1, true);
    host.set_in_clash(h2, true);
    Clash c;
    c.entry = entry;
    c.hero1 = h1;
    c.hero2 = h2;
    c.type1 = t1;
    c.type2 = t2;
    c.health1 = host.health(h1);
    c.health2 = host.health(h2);
    // Begin 0x6abde0: spots on the world X axis through the midpoint, Combat_Distance apart.
    c.state = kApproach;
    const Vec3 p1 = host.position(h1), p2 = host.position(h2);
    const Vec3 mid{(p1.x + p2.x) / 2.0f, (p1.y + p2.y) / 2.0f, (p1.z + p2.z) / 2.0f};
    const float half = data_->entries[entry].combat_distance / 2.0f;
    if (mid.x <= p1.x) {
        c.spot1 = {mid.x + half, mid.y, mid.z};
        c.spot2 = {mid.x - half, mid.y, mid.z};
    } else {
        c.spot1 = {mid.x - half, mid.y, mid.z};
        c.spot2 = {mid.x + half, mid.y, mid.z};
    }
    host.move_to(h1, c.spot1);
    host.move_to(h2, c.spot2);
    clashes_.push_back(c);
    ++started_;
    changed_ = true;
    return true;
}

bool HeroClashManager::update_clash(Clash& c, uint32_t frame, HeroClashHost& host, SimRng& rng) {
    // §2.3 / §3.8 object removed (0x6ac940). s78n t06 [port]: polled at the clash's update - a hero that is no longer alive
    // (dying, removed, retreated) counts as removed at once (retail: when it leaves the object manager).
    const bool gone1 = !host.alive(c.hero1), gone2 = !host.alive(c.hero2);
    if (gone1 || gone2) {
        end(c, gone1 ? c.hero2 : c.hero1, frame, host, true);
        return false;
    }
    const HeroClashEntry& e = data_->entries[c.entry];
    switch (c.state) {
        case kApproach: {  // 0x6acc30 state 1
            if (host.walking(c.hero1) || host.walking(c.hero2)) return true;
            const float d = e.combat_distance;
            if (std::fabs(dist2(host.position(c.hero1), host.position(c.hero2)) - d * d) > 0.5f) {
                begin_snap(c, frame, host);
            } else {
                fight(c, host);
            }
            return true;
        }
        case kSnap: {  // 0x6acfb0: a direct lerp of position and angles
            float s = 1.0f;
            if (frame < c.snap_end) {
                s = static_cast<float>(frame - c.snap_start) / static_cast<float>(c.snap_end - c.snap_start);
                s = std::clamp(s, 0.0f, 1.0f);
            }
            host.place(c.hero1, lerp(c.snap_pos1, c.spot1, s), norm360(c.snap_yaw1 + wrap180(c.want_yaw1 - c.snap_yaw1) * s));
            host.place(c.hero2, lerp(c.snap_pos2, c.spot2, s), norm360(c.snap_yaw2 + wrap180(c.want_yaw2 - c.snap_yaw2) * s));
            if (frame >= c.snap_end) fight(c, host);
            return true;
        }
        default: break;
    }
    // State 3, the exchanges (0x6ad230).
    if (c.outcome == kExchangeConversation) {
        if (host.speech_idle()) new_exchange(c, frame, host, rng);
        return true;
    }
    if (!c.playing) {  // hero 1 plays no clash animation (the first fight frame, or nothing was played)
        new_exchange(c, frame, host, rng);
        return true;
    }
    // Hero 1's clip position: clip fps x elapsed sim seconds (ANIM_PARTICLES A.5); a one-shot finishes at count - 2.
    const float pos = static_cast<float>(frame - c.ex_start) * c.ex_fps / 30.0f;
    const float last = c.ex_frames >= 2u ? static_cast<float>(c.ex_frames - 2u) : 0.0f;
    if (pos >= last) {  // both clips finished (equal clips: they finish together)
        if (!c.damage_applied && !apply_damage(c, frame, host)) return false;
        new_exchange(c, frame, host, rng);
        return true;
    }
    const float progress = c.ex_frames > 0u ? std::clamp(pos / static_cast<float>(c.ex_frames), 0.0f, 1.0f) : 1.0f;
    if (progress < 0.8f || c.damage_applied) return true;
    return apply_damage(c, frame, host);
}

void HeroClashManager::begin_snap(Clash& c, uint32_t frame, HeroClashHost& host) {
    // 0x6aba30.
    c.state = kSnap;
    c.snap_pos1 = host.position(c.hero1);
    c.snap_pos2 = host.position(c.hero2);
    c.snap_yaw1 = norm360(host.yaw_deg(c.hero1));  // [port] the stored yaw read in [0, 360) like 0x20acd0's
    c.snap_yaw2 = norm360(host.yaw_deg(c.hero2));
    const float err_pos = std::clamp(std::max(dmath::sqrt(dist2(c.snap_pos1, c.spot1)), dmath::sqrt(dist2(c.snap_pos2, c.spot2))), 0.0f, 3.0f);
    c.want_yaw1 = yaw360(c.spot2.x - c.spot1.x, c.spot2.y - c.spot1.y);
    c.want_yaw2 = yaw360(c.spot1.x - c.spot2.x, c.spot1.y - c.spot2.y);
    float err_ang = std::max(std::fabs(c.snap_yaw1 - c.want_yaw1), std::fabs(c.snap_yaw2 - c.want_yaw2));
    if (err_ang > 180.0f) err_ang -= 180.0f;  // retail subtracts 180 (not 360 - x) [H quirk, kept]
    err_ang = std::clamp(err_ang, 0.0f, 180.0f);
    const float t = std::max(0.1f + 0.4f * err_pos / 3.0f, 0.1f + 0.9f * err_ang / 180.0f);
    c.snap_start = frame;
    c.snap_end = frame + static_cast<uint32_t>(trunc_i32(30.0f * t));
}

void HeroClashManager::fight(Clash& c, HeroClashHost& host) {
    // 0x6abcd0: state 3; each hero snaps to face the other; IDLE and the locomotor suspended.
    c.state = kFight;
    const Vec3 p1 = host.position(c.hero1), p2 = host.position(c.hero2);
    host.face(c.hero1, yaw360(p2.x - p1.x, p2.y - p1.y));
    host.face(c.hero2, yaw360(p1.x - p2.x, p1.y - p2.y));
    host.hold(c.hero1, true);
    host.hold(c.hero2, true);
    c.playing = false;
}

void HeroClashManager::new_exchange(Clash& c, uint32_t frame, HeroClashHost& host, SimRng& rng) {
    // 0x6ac1b0.
    const HeroClashEntry& e = data_->entries[c.entry];
    c.damage_applied = false;
    c.ex_winner = kNoObject;
    c.ex_loser = kNoObject;
    uint8_t o = kExchangeDraw;
    // [port] "the previous outcome was not 4" reads the current outcome (+0xb8) as the one just finished: a conversation is
    // never followed by another; the weights below exclude the last fought outcome (+0xbc).
    if (c.outcome != kExchangeConversation && e.play_conversation && host.speech_idle()) {
        o = kExchangeConversation;  // no draw
    } else {
        o = clash_pick_outcome(c.previous, e.first_win, e.second_win, rng);
        c.previous = o;
    }
    size_t k1 = 2, k2 = 2;  // Draw
    if (o == kExchangeFirstWins) {
        k1 = 0;
        k2 = 1;
        c.ex_winner = c.hero1;
        c.ex_loser = c.hero2;
    } else if (o == kExchangeSecondWins) {
        k1 = 1;
        k2 = 0;
        c.ex_winner = c.hero2;
        c.ex_loser = c.hero1;
    } else if (o == kExchangeConversation) {
        k1 = k2 = 3;
    }
    const std::string& a1 = e.first_anims[k1];
    const std::string& a2 = e.second_anims[k2];
    uint32_t n1 = 0, n2 = 0, f1 = 0, f2 = 0;
    float fps1 = 0.0f, fps2 = 0.0f;
    const bool m1 = host.clip(c.hero1, a1, 0, n1, f1, fps1);
    const bool m2 = host.clip(c.hero2, a2, 0, n2, f2, fps2);
    const bool fallback = !m1 || !m2;  // [port] no model data: the shipped clips (one variant, 101 frames @ 30 fps)
    if (fallback) {
        n1 = n2 = 1;
        f1 = f2 = kClashFallbackClipFrames;
        fps1 = fps2 = kClashFallbackClipFps;
    }
    bool play = false;
    uint32_t v = 0;
    if (n1 == n2 && n1 > 0) {
        if (n1 >= 2) v = static_cast<uint32_t>(rng.range_int(0, static_cast<int32_t>(n1) - 1));  // 0x1ffb40
        if (v != 0 && !fallback) {
            uint32_t vn = 0;
            const bool ok1 = host.clip(c.hero1, a1, v, vn, f1, fps1);
            const bool ok2 = host.clip(c.hero2, a2, v, vn, f2, fps2);
            if (!ok1 || !ok2) f1 = f2 = 0;
        }
        // Both clips exist with equal frame counts and rates: both heroes play them (one-shot, speed 1).
        play = f1 > 0 && f1 == f2 && fps1 == fps2 && fps1 > 0.0f;
    }
    // Otherwise nothing is played: the next frame starts a new exchange (and draws again) [H; M in practice].
    // r3h t02: both heroes start their exchange clips (variant v, one-shot, blend 0.1, speed 1).
    if (play) {
        host.play(c.hero1, a1, v);
        host.play(c.hero2, a2, v);
    }
    c.playing = play;
    c.ex_start = frame;
    c.ex_frames = play ? f1 : 0u;
    c.ex_fps = play ? fps1 : 0.0f;
    c.outcome = o;
    ++c.exchanges;
}

bool HeroClashManager::apply_damage(Clash& c, uint32_t frame, HeroClashHost& host) {
    c.damage_applied = true;
    if (c.ex_loser == kNoObject || c.ex_winner == kNoObject) return true;
    const HeroClashEntry& e = data_->entries[c.entry];
    const float start_health = c.ex_loser == c.hero1 ? c.health1 : c.health2;  // health at the clash start, not max
    const int32_t wt = c.ex_winner == c.hero1 ? c.type1 : c.type2;
    const int32_t dmg = clash_exchange_damage(e, start_health, wt == c.type1, wt == c.type2);
    host.damage(c.ex_loser, static_cast<float>(dmg), c.ex_winner);
    if (!host.alive(c.ex_loser)) {
        end(c, c.ex_winner, frame, host, false);
        return false;
    }
    return true;
}

void HeroClashManager::release(const Clash& c, ObjectId hero, HeroClashHost& host) const {
    // 0x6acb80: state 3: IDLE and the locomotor resumed first; states 1 / 3: unlock, TARGETING resumed, +0x3b4 = 0.
    // State 2 (snap): nothing - retail quirk kept (s78n t06 [port: parity]): a hero whose opponent is removed during the
    // snap stays locked with TARGETING suspended and +0x3b4 = 1 for the rest of the battle.
    if (c.state == kSnap) return;
    if (c.state == kFight) host.hold(hero, false);
    host.set_in_clash(hero, false);
}

void HeroClashManager::end(Clash& c, ObjectId winner, uint32_t frame, HeroClashHost& host, bool by_removal) {
    // 0x6ac660: release both heroes (per state); the winner's speech is audio only.
    for (const ObjectId h : {c.hero1, c.hero2}) {
        if (by_removal && !host.alive(h)) {
            // [port] the removed hero is gone in retail (the handler clears its slot, no release); the port keeps the dead
            // unit's record, so only its +0x3b4 flag is cleared.
            host.set_in_clash(h, false);
            continue;
        }
        release(c, h, host);
    }
    EndEvent ev;
    ev.entry = c.entry;
    ev.winner = winner;
    ev.loser = winner == c.hero1 ? c.hero2 : c.hero1;
    ev.frame = frame;
    ev.by_removal = by_removal;
    ev.first_won = winner == c.hero1;
    ended_.push_back(ev);
}

// ---- action-driven clash (§4; MCX4_T10_RE_ANSWERS §11, MCX4_T11_T12_RE_ANSWERS §B3) -----------------------------------

namespace {
constexpr std::string_view kFallbackAttackClip = "Attack";  // ModelAnimType 5 (Attack fire 0x71ed40 step 2)
constexpr std::string_view kEndAttackClip = "Attack Idle";  // ModelAnimType 6 (Attack end 0x71ec30)
constexpr float kPastTargetSwing = 1.0471976f;              // 60 degrees in radians (0x3f860a92)

int32_t action_interval(uint8_t kind) {
    return kind == kActionAttack ? kAttackInterval : kind == kActionMove ? kMoveInterval : kSpecialAbilityInterval;
}
}  // namespace

bool HeroClashManager::start_action_clash(uint32_t entry, ObjectId a, ObjectId b, HeroClashHost& host) {
    // 0x6ab200: the involved objects have exactly two owners (a third fails), and the first is an enemy of the second.
    const int32_t o1 = host.owner(a), o2 = host.owner(b);
    if (o1 == -1 || o2 == -1 || o1 == o2 || !host.enemies(o1, o2)) return false;
    ActionClash c;
    c.entry = entry;
    c.objects = {a, b};
    // Each object: held (locomotor and behaviours 9 / 10 / 11 / 7 / 0x20 suspended), +0x3b4 := 1 on it and its team
    // container, a new coordinator with a Move_To its own position.
    for (const ObjectId id : c.objects) host.action_hold(id, true);
    action_clashes_.push_back(std::move(c));
    ++started_;
    changed_ = true;
    return true;
}

bool HeroClashManager::update_action_clash(ActionClash& c, int32_t frame, HeroClashHost& host, SimRng& rng) {
    // §2.3 / 0x6ab4d0 object removed. [port] polled at the clash's update, as for the melee clash: an object that is no
    // longer alive counts as removed at once (retail: when it leaves the object manager).
    for (size_t i = 0; i < c.objects.size();) {
        if (host.alive(c.objects[i])) {
            ++i;
            continue;
        }
        if (!object_removed(c, c.objects[i], host, rng)) return false;  // erased c.objects[i]: i stays
    }
    if (c.winner != kNoObject) return true;  // 0x6ab6f0 runs only without a winner
    const HeroClashEntry& e = data_->entries[c.entry];
    // Every object without a running action picks one (retail prints "Cannot find something for %s(id=%d) to do!" when
    // none fits), in the clash's object order.
    std::vector<ObjectId> idle;
    for (const ObjectId id : c.objects) {
        bool running = false;
        for (const RunningAction& r : c.running) running = running || r.actor == id;
        if (!running) idle.push_back(id);
    }
    for (const ObjectId id : idle) pick_action(c, id, host, rng);
    // Each running action in start order: serviced when its interval has passed since the last service (the first service
    // comes once frame >= interval, since the last-service frame starts at 0), then ended and dropped when finished.
    for (size_t i = 0; i < c.running.size();) {
        RunningAction& r = c.running[i];
        const ClashAction& a = e.actions[r.action];
        if (r.interval > 0 && r.last_service + r.interval <= frame) {
            service_action(r, a, frame, host, rng);
            r.last_service = frame;
        }
        if (r.actor == kNoObject || r.target == kNoObject || r.finished) {  // slot 8
            end_action(r, a, host, rng);
            c.running.erase(c.running.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    return true;
}

bool HeroClashManager::object_removed(ActionClash& c, ObjectId id, HeroClashHost& host, SimRng& rng) {
    // 0x6ab4d0.
    if (c.winner == id) c.winner = kNoObject;
    const auto it = std::find(c.objects.begin(), c.objects.end(), id);
    if (it == c.objects.end()) return true;
    c.objects.erase(it);
    // [port] the removed object's record stays in the port (retail deletes it), so only its +0x3b4 flag is cleared, as
    // the melee clash does.
    host.set_in_clash(id, false);
    const HeroClashEntry& e = data_->entries[c.entry];
    // Every running action forgets the object (vf10 0x5e98a0: actor / target cleared); one that is now finished ends.
    for (size_t i = 0; i < c.running.size();) {
        RunningAction& r = c.running[i];
        if (r.actor == id) r.actor = kNoObject;
        if (r.target == id) r.target = kNoObject;
        if (r.actor == kNoObject || r.target == kNoObject || r.finished) {
            end_action(r, e.actions[r.action], host, rng);
            c.running.erase(c.running.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    if (c.winner != kNoObject) return true;
    // The remaining objects share one owner: the winner is one of them (one synced Get_Int(0, n - 1)), every running
    // action ends (0x6aae00), each object is released, and the manager removes the clash.
    int32_t owner = -1;
    for (const ObjectId o : c.objects) {
        const int32_t w = host.owner(o);
        if (owner == -1) {
            owner = w;
        } else if (w != owner) {
            return true;  // still two sides: the clash goes on
        }
    }
    // [port] no object left: the clash ends (retail keeps an empty clash object that does nothing).
    if (c.objects.empty()) return false;
    if (owner == -1) return true;
    c.winner = c.objects[static_cast<size_t>(rng.range_int(0, static_cast<int32_t>(c.objects.size()) - 1))];
    for (RunningAction& r : c.running) end_action(r, e.actions[r.action], host, rng);
    c.running.clear();
    for (const ObjectId o : c.objects) host.action_hold(o, false);  // 0x63f060(obj, 0, 1), +0x3b4 := 0 (and container)
    return false;
}

float HeroClashManager::relevance(const ClashAction& a, ObjectId actor, const std::vector<ObjectId>& objects, HeroClashHost& host,
                                  ObjectId& target) const {
    // 0x5e9300: only an actor whose type is Action_Object (pointer equality: an empty Action_Object never matches). The
    // candidates are the clash's objects whose type is Action_Target (none without one); the highest class relevance
    // (slot 3) wins, the first one on a tie, and only a value > 0.
    target = kNoObject;
    const int32_t ot = host.type_by_name(a.object_type);
    if (ot < 0 || host.type_of(actor) != ot) return 0.0f;
    const int32_t tt = host.type_by_name(a.target_type);
    if (tt < 0) return 0.0f;
    float best = 0.0f;
    for (const ObjectId o : objects) {
        if (host.type_of(o) != tt) continue;
        // Attack slot 3 (0x71eac0): the candidate must be an enemy of the actor (0x39a450). Move (0x5ea310) and
        // Special_Ability [M: the ActionClass slot] use the base relevance only.
        if (a.kind == kActionAttack && !host.enemies(host.owner(actor), host.owner(o))) continue;
        // 0x5e91a0 by Action_Relevance_Type (-1 and unknown values: 0).
        float v = 0.0f;
        switch (a.relevance_type) {
            case kRelevanceConstant:
                v = a.relevance_constant;
                break;
            case kRelevanceDistance:
                v = a.distance_curve.value(dmath::sqrt(dist2(host.position(actor), host.position(o))));
                break;
            case kRelevanceHealth:
                v = a.health_curve.value(host.health_fraction(actor));
                break;
            case kRelevanceDistanceTimesHealth: {
                const float dv = a.distance_curve.value(dmath::sqrt(dist2(host.position(actor), host.position(o))));
                v = a.health_curve.value(host.health_fraction(actor)) * dv;
                break;
            }
            default:
                break;
        }
        if (best < v) {
            best = v;
            target = o;
        }
    }
    return best;
}

bool HeroClashManager::pick_action(ActionClash& c, ObjectId actor, HeroClashHost& host, SimRng& rng) {
    // 0x6aae80: the actions with relevance > 0 (entry order), one synced draw r = Get_Float(0, total) clamped to
    // [0, total]; the first action whose running sum reaches r is cloned and started. A failed start deletes the clone.
    const HeroClashEntry& e = data_->entries[c.entry];
    struct Choice {
        uint32_t action;
        float relevance;
        ObjectId target;
    };
    std::vector<Choice> choices;
    for (uint32_t i = 0; i < e.actions.size(); ++i) {
        ObjectId target = kNoObject;
        const float v = relevance(e.actions[i], actor, c.objects, host, target);
        if (v > 0.0f) choices.push_back({i, v, target});
    }
    if (choices.empty()) return false;
    float total = 0.0f;
    for (const Choice& ch : choices) total += ch.relevance;
    if (!(total > 0.0f)) return false;
    const float r = std::clamp(rng.range_float(0.0f, total), 0.0f, total);  // 0x532480
    float sum = 0.0f;
    for (const Choice& ch : choices) {
        sum += ch.relevance;
        if (r > sum) continue;
        const ClashAction& a = e.actions[ch.action];
        RunningAction run;
        run.action = ch.action;
        run.actor = actor;
        run.target = ch.target;  // the other objects are kept by retail for removal notices only
        run.interval = action_interval(a.kind);
        if (!begin_action(run, a, host, rng)) return false;
        c.running.push_back(run);
        ++c.picks;
        return true;
    }
    return false;
}

bool HeroClashManager::begin_action(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng) {
    if (r.actor == kNoObject) return false;
    if (a.kind == kActionAttack) return true;  // 0x71ea20: the actor, the target and the other objects are stored
    if (a.kind == kActionSpecialAbility) {
        // 0x71f2e0: the actor's ability named Ability_Name is activated on the target; the clone keeps the template's
        // post-parsed frame count (Ability_Cooldown_Time_In_Secs x 30).
        r.frames_left = a.cooldown_frames;
        return host.activate_ability(r.actor, a.ability_name, r.target);
    }
    // Move_Action begin (slot 5, 0x5e9ce0).
    if (host.busy(r.actor)) return false;
    if (host.has_locomotor(r.actor)) r.loco_suspended = host.locomotor_suspended(r.actor);
    if (a.use_jetpack) host.jetpack_locomotor(r.actor, true);
    bool ok = false;
    if (a.move_type == kMoveToTarget) {
        ok = move_begin(r, a, host, rng);
    } else if (a.move_type == kMovePastTarget) {
        Vec3 goal;
        ok = move_goal(r, a, host, rng, goal) && move_start(r, a, host, goal);
    }  // Invalid / Dodge_Shot_From_Target: the start fails
    if (ok) {
        if (host.has_locomotor(r.actor) && host.locomotor_suspended(r.actor)) host.suspend_locomotor(r.actor, false);
        return true;
    }
    if (a.use_jetpack) {  // retail re-suspends the locomotor only inside the jet-pack branch
        host.jetpack_locomotor(r.actor, false);
        if (r.loco_suspended && host.has_locomotor(r.actor)) host.suspend_locomotor(r.actor, true);
    }
    return false;
}

bool HeroClashManager::move_begin(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng) {
    // 0x5e9ed0: with Wait_For_Finish_If_Target_Using_Jetpack and the target in a jet-pack jump the actor waits (the start
    // counts as made [M: the return value is the jump test's]); else a destination and the move start.
    if (a.wait_for_jetpack && host.jetpacking(r.target)) {
        r.waiting = true;
        return true;
    }
    Vec3 goal;
    return move_goal(r, a, host, rng, goal) && move_start(r, a, host, goal);
}

bool HeroClashManager::move_goal(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng, Vec3& goal) {
    // Move_To_Target 0x5ea190 / Move_Past_Target 0x5e9f30, up to 5 tries: d = the distance to the target, r = U(Move_Min,
    // Move_Max). To: the goal lies d - r along the line (the actor stops r short); Past: d + r, the offset turned by
    // U(-60, +60) degrees about the vertical (r drawn first). d -/+ r <= 0 finishes the action.
    const bool past = a.move_type == kMovePastTarget;
    for (int tries = 0; tries < 5; ++tries) {
        const Vec3 p = host.position(r.actor), q = host.position(r.target);
        Vec3 dir{q.x - p.x, q.y - p.y, q.z - p.z};
        const float d = dmath::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (d > 0.0f) dir = {dir.x / d, dir.y / d, dir.z / d};  // 0x139800 normalises in place, returns the length
        const float rr = rng.range_float(a.min_distance, a.max_distance);  // 0x1ffbb0
        const float f = past ? d + rr : d - rr;
        if (f <= 0.0f) {
            r.finished = true;
            return false;
        }
        Vec3 off{dir.x * f, dir.y * f, dir.z * f};
        if (past) {
            const float ang = rng.range_float(-kPastTargetSwing, kPastTargetSwing);
            const float cs = dmath::cos(ang), sn = dmath::sin(ang);
            off = {cs * off.x - sn * off.y, cs * off.y + sn * off.x, off.z};  // 0x2bab90
        } else {
            r.last_d2 = d * d;
        }
        goal = {p.x + off.x, p.y + off.y, p.z + off.z};
        if (host.move_goal(r.actor, goal, past)) return true;
    }
    return false;
}

bool HeroClashManager::move_start(RunningAction& r, const ClashAction& a, HeroClashHost& host, Vec3 goal) {
    // 0x5ea910: needs the actor's coordinator; Move_Animation_Override sets the move clip, Move_Speed > 0 the speed
    // override, then the move order and the movement lock.
    if (!host.can_move(r.actor)) return false;
    if (a.anim_override) host.move_anim_override(r.actor, a.move_anim, a.move_subindex, a.move_speedup);
    if (a.move_speed > 0.0f) host.speed_override(r.actor, a.move_speed);
    host.order_move(r.actor, goal);
    host.lock_orders(r.actor, true);
    return true;
}

void HeroClashManager::attack_fire(RunningAction& r, const ClashAction& a, int32_t frame, HeroClashHost& host) {
    // 0x71ed40: the clip (one-shot, blend 0.1 s, speed Attack_Animation_Speedup); without it clip type 5 any variant,
    // remembered; without that too the action is finished. Then the sound, the shot, the flash shown.
    const std::string_view anim = r.fallback_clip ? kFallbackAttackClip : std::string_view(a.attack_anim);
    const int32_t sub = r.fallback_clip ? kAnyVariant : a.attack_subindex;
    if (!host.play_clip(r.actor, anim, sub, false, 0.1f, a.attack_speedup)) {
        if (!host.play_clip(r.actor, kFallbackAttackClip, kAnyVariant, false, 0.1f, a.attack_speedup)) {
            r.finished = true;
            return;
        }
        r.fallback_clip = true;
    }
    if (!a.sfx.empty()) host.sound(r.actor, a.sfx);
    host.fire_projectile(r.actor, a.firing_bone, r.target, a.projectile_type);  // 0x54ade0
    host.flash(r.actor, a.firing_bone, true);
    ++r.shots_fired;
    r.last_shot = frame;
}

void HeroClashManager::service_action(RunningAction& r, const ClashAction& a, int32_t frame, HeroClashHost& host, SimRng& rng) {
    if (r.actor == kNoObject || r.target == kNoObject || r.finished) return;
    if (a.kind == kActionSpecialAbility) {
        // 0x71f5a0: the frame count drops by the interval each service; below 1 the action is finished.
        r.frames_left -= r.interval;
        if (r.frames_left < 1) r.finished = true;
        return;
    }
    if (a.kind == kActionAttack) {
        // 0x71ef10: face the target; while the actor's current clip is the attack clip, hide the flash 4 frames after the
        // last shot, and once the clip ended finish after Attack_Number_Of_Shots shots; otherwise fire.
        const Vec3 p = host.position(r.actor), q = host.position(r.target);
        host.face(r.actor, yaw360(q.x - p.x, q.y - p.y));
        const std::string_view anim = r.fallback_clip ? kFallbackAttackClip : std::string_view(a.attack_anim);
        const int32_t state = host.clip_state(r.actor, anim);
        if (state != 0) {
            if (state != 2) {
                if (r.last_shot + 4 == frame) host.flash(r.actor, a.firing_bone, false);
                return;
            }
            if (a.shots <= r.shots_fired) {
                r.finished = true;
                return;
            }
        }
        attack_fire(r, a, frame, host);
        return;
    }
    // Move_Action update (slot 7, 0x5eaab0).
    if (a.move_type == kMoveToTarget) {
        if (!host.has_locomotor(r.actor)) {
            r.finished = true;
            return;
        }
        if (r.waiting) {  // the target's jump: face it until it lands, then a destination and the move start
            if (host.jetpacking(r.target)) {
                const Vec3 p = host.position(r.actor), q = host.position(r.target);
                host.face(r.actor, yaw360(q.x - p.x, q.y - p.y));
                return;
            }
            r.waiting = false;
            Vec3 goal;
            if (!move_goal(r, a, host, rng, goal) || !move_start(r, a, host, goal)) r.finished = true;
            return;
        }
        if (host.walking(r.actor)) {
            // The target moves away: the squared distance grew by more than 10 since the last service; up to 3 restarts.
            const float d2 = dist2(host.position(r.target), host.position(r.actor));
            if (d2 <= r.last_d2 + 10.0f) {
                r.last_d2 = d2;
                return;
            }
            if (r.restarts < 3) {
                r.last_d2 = 0.0f;
                host.lock_orders(r.actor, false);
                if (a.anim_override) host.clear_move_anim_override(r.actor);
                if (a.move_speed > 0.0f) host.clear_speed_override(r.actor);
                move_begin(r, a, host, rng);
                ++r.restarts;
                return;
            }
        }
        r.finished = true;  // the locomotor stopped, or the restarts are used up
        return;
    }
    if (a.move_type == kMovePastTarget && (!host.has_locomotor(r.actor) || !host.walking(r.actor))) r.finished = true;
}

void HeroClashManager::end_action(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng) {
    if (r.actor == kNoObject) return;  // retail ends do nothing without an actor
    if (a.kind == kActionAttack) {
        // 0x71ec30: clip type 6 any variant, looped, blend U(0.1, 0.5) (synced); without it the type's default clip, blend
        // 0.1. Then the flash is hidden.
        const float blend = rng.range_float(0.1f, 0.5f);
        if (!host.play_clip(r.actor, kEndAttackClip, kAnyVariant, true, blend, 1.0f)) host.play_default_clip(r.actor, 0.1f);
        host.flash(r.actor, a.firing_bone, false);
    } else if (a.kind == kActionMove) {
        // 0x5ea820: the overrides cleared, unlocked, the jet pack swapped back, the locomotor suspended again if it was.
        if (a.anim_override) host.clear_move_anim_override(r.actor);
        if (a.move_speed > 0.0f) host.clear_speed_override(r.actor);
        host.lock_orders(r.actor, false);
        if (a.use_jetpack) host.jetpack_locomotor(r.actor, false);
        if (r.loco_suspended && host.has_locomotor(r.actor)) host.suspend_locomotor(r.actor, true);
    }  // Special_Ability_Action: no end of its own
}

// ---- state ------------------------------------------------------------------------------------------------------------

void HeroClashManager::hash(StateHasher& h) const {
    h.u32(static_cast<uint32_t>(clashes_.size()));
    for (const Clash& c : clashes_) {
        h.u32(c.entry);
        h.i32(c.hero1);
        h.i32(c.hero2);
        h.i32(c.type1);
        h.i32(c.type2);
        h.u8(c.state);
        h.f32(c.health1);
        h.f32(c.health2);
        // s78n t06 review: every saved field is hashed (spots, snap start / target poses, the exchange clip rate).
        for (const Vec3& v : {c.spot1, c.spot2, c.snap_pos1, c.snap_pos2}) {
            h.f32(v.x);
            h.f32(v.y);
            h.f32(v.z);
        }
        h.f32(c.snap_yaw1);
        h.f32(c.snap_yaw2);
        h.f32(c.want_yaw1);
        h.f32(c.want_yaw2);
        h.u32(c.snap_start);
        h.u32(c.snap_end);
        h.u8(c.outcome);
        h.u8(c.previous);
        h.i32(c.ex_winner);
        h.i32(c.ex_loser);
        h.boolean(c.damage_applied);
        h.boolean(c.playing);
        h.u32(c.ex_start);
        h.u32(c.ex_frames);
        h.f32(c.ex_fps);
        h.u32(c.exchanges);
    }
    // Hashed only while an action-driven clash runs (the CRC of battles without one is unchanged).
    if (action_clashes_.empty()) return;
    h.u32(static_cast<uint32_t>(action_clashes_.size()));
    for (const ActionClash& c : action_clashes_) {
        h.u32(c.entry);
        h.u32(static_cast<uint32_t>(c.objects.size()));
        for (const ObjectId o : c.objects) h.i32(o);
        h.i32(c.winner);
        h.u32(c.picks);
        h.u32(static_cast<uint32_t>(c.running.size()));
        for (const RunningAction& r : c.running) {
            h.u32(r.action);
            h.i32(r.actor);
            h.i32(r.target);
            h.i32(r.last_service);
            h.i32(r.interval);
            h.boolean(r.finished);
            h.boolean(r.fallback_clip);
            h.i32(r.shots_fired);
            h.i32(r.last_shot);
            h.boolean(r.loco_suspended);
            h.boolean(r.waiting);
            h.f32(r.last_d2);
            h.i32(r.restarts);
            h.i32(r.frames_left);
        }
    }
}

void HeroClashManager::write(ByteWriter& w) const {
    w.u32(action_clashes_.empty() ? kSaveVersion : kSaveVersionActions).u32(started_).u32(static_cast<uint32_t>(clashes_.size()));
    for (const Clash& c : clashes_) {
        w.u32(c.entry).i32(c.hero1).i32(c.hero2).i32(c.type1).i32(c.type2).f32(c.health1).f32(c.health2).u8(c.state);
        put_vec(w, c.spot1);
        put_vec(w, c.spot2);
        put_vec(w, c.snap_pos1);
        put_vec(w, c.snap_pos2);
        w.f32(c.snap_yaw1).f32(c.snap_yaw2).f32(c.want_yaw1).f32(c.want_yaw2).u32(c.snap_start).u32(c.snap_end);
        w.u8(c.outcome).u8(c.previous).i32(c.ex_winner).i32(c.ex_loser).boolean(c.damage_applied).boolean(c.playing);
        w.u32(c.ex_start).u32(c.ex_frames).f32(c.ex_fps).u32(c.exchanges);
    }
    if (action_clashes_.empty()) return;
    w.u32(action_ended_).u32(static_cast<uint32_t>(action_clashes_.size()));
    for (const ActionClash& c : action_clashes_) {
        w.u32(c.entry).u32(static_cast<uint32_t>(c.objects.size()));
        for (const ObjectId o : c.objects) w.i32(o);
        w.i32(c.winner).u32(c.picks).u32(static_cast<uint32_t>(c.running.size()));
        for (const RunningAction& a : c.running) {
            w.u32(a.action).i32(a.actor).i32(a.target).i32(a.last_service).i32(a.interval).boolean(a.finished);
            w.boolean(a.fallback_clip).i32(a.shots_fired).i32(a.last_shot).boolean(a.loco_suspended).boolean(a.waiting);
            w.f32(a.last_d2).i32(a.restarts).i32(a.frames_left);
        }
    }
}

bool HeroClashManager::read(ByteReader& r) {
    const uint32_t version = r.u32();
    if (!r.ok() || (version != kSaveVersion && version != kSaveVersionActions)) return false;
    const uint32_t started = r.u32();
    const uint32_t n = r.u32();
    if (!r.ok() || n > r.remaining()) return false;
    std::vector<Clash> list(n);
    for (Clash& c : list) {
        c.entry = r.u32();
        c.hero1 = r.i32();
        c.hero2 = r.i32();
        c.type1 = r.i32();
        c.type2 = r.i32();
        c.health1 = r.f32();
        c.health2 = r.f32();
        c.state = r.u8();
        c.spot1 = get_vec(r);
        c.spot2 = get_vec(r);
        c.snap_pos1 = get_vec(r);
        c.snap_pos2 = get_vec(r);
        c.snap_yaw1 = r.f32();
        c.snap_yaw2 = r.f32();
        c.want_yaw1 = r.f32();
        c.want_yaw2 = r.f32();
        c.snap_start = r.u32();
        c.snap_end = r.u32();
        c.outcome = r.u8();
        c.previous = r.u8();
        c.ex_winner = r.i32();
        c.ex_loser = r.i32();
        c.damage_applied = r.boolean();
        c.playing = r.boolean();
        c.ex_start = r.u32();
        c.ex_frames = r.u32();
        c.ex_fps = r.f32();
        c.exchanges = r.u32();
        if (!r.ok()) return false;
        if (c.state < kApproach || c.state > kFight || c.outcome > kExchangeConversation || c.previous > kExchangeConversation) return false;
        if (!data_ || c.entry >= data_->entries.size()) return false;  // set_data before read (the host's data)
    }
    std::vector<ActionClash> actions;
    uint32_t action_ended = 0;
    if (version == kSaveVersionActions) {
        action_ended = r.u32();
        const uint32_t na = r.u32();
        if (!r.ok() || na > r.remaining() || !data_) return false;
        actions.resize(na);
        for (ActionClash& c : actions) {
            c.entry = r.u32();
            const uint32_t no = r.u32();
            if (!r.ok() || c.entry >= data_->entries.size() || no > r.remaining()) return false;
            c.objects.resize(no);
            for (ObjectId& o : c.objects) o = r.i32();
            c.winner = r.i32();
            c.picks = r.u32();
            const uint32_t nr = r.u32();
            if (!r.ok() || nr > r.remaining()) return false;
            c.running.resize(nr);
            for (RunningAction& a : c.running) {
                a.action = r.u32();
                a.actor = r.i32();
                a.target = r.i32();
                a.last_service = r.i32();
                a.interval = r.i32();
                a.finished = r.boolean();
                a.fallback_clip = r.boolean();
                a.shots_fired = r.i32();
                a.last_shot = r.i32();
                a.loco_suspended = r.boolean();
                a.waiting = r.boolean();
                a.last_d2 = r.f32();
                a.restarts = r.i32();
                a.frames_left = r.i32();
                if (!r.ok() || a.action >= data_->entries[c.entry].actions.size()) return false;
            }
        }
    }
    clashes_ = std::move(list);
    action_clashes_ = std::move(actions);
    action_ended_ = action_ended;
    started_ = started;
    ended_.clear();
    changed_ = true;
    return true;
}

}  // namespace alamo::tactical::combat
