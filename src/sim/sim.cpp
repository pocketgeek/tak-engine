#include "sim/sim.h"

#include "tdf/tdf.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace tak::sim {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTick = 30.0f;               // FBI per-tick values -> per-second
constexpr float kCobAngle = 2 * kPi / 65536.0f;

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

float angleDiff(float a, float b) {
    float d = std::fmod(a - b + kPi, 2 * kPi);
    if (d < 0) d += 2 * kPi;
    return d - kPi;
}

} // namespace

void TypeRegistry::loadDir(const std::filesystem::path& unitsDir) {
    for (const auto& e : std::filesystem::directory_iterator(unitsDir)) {
        if (lower(e.path().extension().string()) != ".fbi") continue;
        try {
            auto root = tdf::parse(e.path());
            const auto* info = root.child("UNITINFO");
            if (!info) continue;
            UnitType t;
            t.id = lower(info->valueOr("objectname", e.path().stem().string()));
            // Don't let a later dir (e.g. the Iron Plague data, whose
            // tarnecr2.fbi also claims objectname TARNECRO) clobber a unit
            // the base game already defined. First definition wins.
            if (types_.count(t.id)) continue;
            t.name = info->valueOr("name", t.id);
            t.side = info->valueOr("side", "");
            // Buildings often declare canmove=1; bmcode (0 = building,
            // 1 = mobile unit) is the authoritative mobility flag.
            t.canMove = info->numberOr("canmove", 0) != 0 &&
                        info->numberOr("bmcode", 1) != 0;
            t.maxVel = float(info->numberOr("maxvelocity", 0)) * kTick;
            t.accel = float(info->numberOr("acceleration", 0.5)) * kTick;
            t.brake = float(info->numberOr("brakerate", 0.5)) * kTick;
            // FBI turnrate is COB angle units per tick.
            float tr = float(info->numberOr("turnrate", 500));
            t.turnRate = tr * kCobAngle * kTick;
            t.maxHp = float(info->numberOr("maxdamage", 100));
            t.isBuilder = info->numberOr("builder", 0) != 0;
            t.buildCost = float(info->numberOr("buildcost", 0));
            t.buildTime = float(info->numberOr("buildtime", 0));
            t.workerTime = float(info->numberOr("workertime", 1));
            t.income = float(info->numberOr("mogriumincome", 0));
            t.storage = float(info->numberOr("mogriumstorage", 0));
            t.footX = int(info->numberOr("footprintx", 1));
            t.footZ = int(info->numberOr("footprintz", 1));
            {
                std::string ym = info->valueOr("yardmap", "");
                std::erase_if(ym, [](char c) { return c == ' ' || c == '\t'; });
                if (int(ym.size()) == t.footX * t.footZ) t.yardMap = ym;
            }
            t.canTransport = info->numberOr("cantransport", 0) != 0;
            t.transportCap = int(info->numberOr("transportcapacity", 0));
            t.soundClass = lower(info->valueOr("soundcategory",
                                               info->valueOr("soundclass", "")));
            t.sight = float(info->numberOr("sightdistance", 180));
            t.corpse = lower(info->valueOr("corpse", ""));
            t.canFly = info->numberOr("canfly", 0) != 0;
            std::string mc = lower(info->valueOr("movementclass", ""));
            if (mc.rfind("water", 0) == 0) t.domain = UnitType::Domain::Water;
            else if (mc.rfind("hover", 0) == 0) t.domain = UnitType::Domain::Hover;
            t.cruiseAlt = float(info->numberOr("cruisealt", 0)) / 4;
            for (int slot = 1; slot <= 3; ++slot) {
                const auto* w = root.child("WEAPON" + std::to_string(slot));
                if (!w) continue;
                Weapon wp;
                wp.name = w->valueOr("name", "");
                wp.range = float(w->numberOr("range", 0));
                wp.reload = float(w->numberOr("reloadtime", 1));
                wp.projVel = float(w->numberOr("weaponvelocity", 0));
                wp.melee = lower(w->valueOr("type", "")) == "melee";
                if (const auto* dmg = w->child("DAMAGE"))
                    wp.damage = float(dmg->numberOr("default", 0));
                if (wp.damage > 0) t.weapons.push_back(wp);
            }
            if (!t.weapons.empty()) t.weapon = t.weapons[0];
            types_[t.id] = std::move(t);
        } catch (const std::exception&) {
            // Skip malformed definitions rather than fail the registry.
        }
    }
}

void TypeRegistry::loadBuildTree(const std::filesystem::path& canbuildDir) {
    for (const auto& b : std::filesystem::directory_iterator(canbuildDir)) {
        if (!b.is_directory()) continue;
        std::string builder = lower(b.path().filename().string());
        std::vector<std::pair<double, std::string>> entries;
        for (const auto& e : std::filesystem::directory_iterator(b.path())) {
            if (lower(e.path().extension().string()) != ".tdf") continue;
            double prio = 99;
            try {
                auto root = tdf::parse(e.path());
                if (const auto* m = root.child("Menu")) prio = m->numberOr("priority", 99);
            } catch (const std::exception&) {}
            entries.push_back({prio, lower(e.path().stem().string())});
        }
        std::sort(entries.begin(), entries.end());
        auto& list = buildTree_[builder];
        for (auto& [p, id] : entries)
            if (std::find(list.begin(), list.end(), id) == list.end())
                list.push_back(id);
    }
}

const std::vector<std::string>& TypeRegistry::buildable(const std::string& builderId) const {
    static const std::vector<std::string> kEmpty;
    auto it = buildTree_.find(lower(builderId));
    return it == buildTree_.end() ? kEmpty : it->second;
}

const UnitType* TypeRegistry::find(const std::string& id) const {
    auto it = types_.find(lower(id));
    return it == types_.end() ? nullptr : &it->second;
}

int World::spawn(const UnitType* type, float x, float z, float heading, int team) {
    Unit u;
    u.id = nextId_++;
    u.team = team;
    u.type = type;
    u.x = x;
    u.z = z;
    u.heading = heading;
    u.hp = type ? type->maxHp : 100;
    units_.push_back(u);
    return u.id;
}

Unit* World::unit(int id) {
    for (auto& u : units_)
        if (u.id == id) return &u;
    return nullptr;
}

void World::setTerrain(const std::vector<uint8_t>& heights, int w, int h, int seaLevel) {
    // Ground: no cliffs, water at most ankle deep (moveinfo MaxWaterDepth ~20).
    nav_ = NavGrid(heights, w, h, 20);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            if (seaLevel - int(heights[size_t(z) * w + x]) > 20)
                nav_.block(x, z, 1, 1, true);
    // Water: needs depth (moveinfo MinWaterDepth ~13); slope irrelevant.
    navWater_ = NavGrid(heights, w, h, 255);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            if (seaLevel - int(heights[size_t(z) * w + x]) < 13)
                navWater_.block(x, z, 1, 1, true);
    // Hover: land without cliffs, or any water.
    navHover_ = NavGrid(heights, w, h, 20);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            if (seaLevel - int(heights[size_t(z) * w + x]) > 0)
                navHover_.block(x, z, 1, 1, false);
}

void blockFootprint(NavGrid& nav, const UnitType& t, float x, float z, bool blocked) {
    int cx = int(x) / 16 - t.footX / 2, cz = int(z) / 16 - t.footZ / 2;
    if (t.yardMap.empty()) {
        nav.block(cx, cz, t.footX, t.footZ, blocked);
        return;
    }
    for (int j = 0; j < t.footZ; ++j)
        for (int i = 0; i < t.footX; ++i) {
            char c = t.yardMap[size_t(j) * t.footX + i];
            if (c == 'o' || c == 'O')
                nav.block(cx + i, cz + j, 1, 1, blocked);
        }
}

void NavGrid::block(int cx, int cz, int w, int h, bool blocked) {
    for (int z = cz; z < cz + h; ++z)
        for (int x = cx; x < cx + w; ++x)
            if (x >= 0 && z >= 0 && x < w_ && z < h_)
                cells_[size_t(z) * w_ + x] = blocked ? 0 : 1;
}

NavGrid::NavGrid(const std::vector<uint8_t>& heights, int w, int h, int cliff)
    : w_(w), h_(h) {
    cells_.assign(size_t(w) * h, 1);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            int lo = 255, hi = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    int nz = std::clamp(z + dz, 0, h - 1);
                    int v = heights[size_t(nz) * w + nx];
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            if (hi - lo > cliff) cells_[size_t(z) * w + x] = 0;
        }
}

bool NavGrid::lineClear(int x0, int z0, int x1, int z1) const {
    int dx = std::abs(x1 - x0), dz = -std::abs(z1 - z0);
    int sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1, err = dx + dz;
    while (true) {
        if (!walkable(x0, z0)) return false;
        if (x0 == x1 && z0 == z1) return true;
        int e2 = 2 * err;
        if (e2 >= dz) { err += dz; x0 += sx; }
        if (e2 <= dx) { err += dx; z0 += sz; }
    }
}

std::vector<Order> NavGrid::findPath(float wx0, float wz0, float wx1, float wz1) const {
    constexpr int kCell = 16;
    int sx = int(wx0) / kCell, sz = int(wz0) / kCell;
    int tx = int(wx1) / kCell, tz = int(wz1) / kCell;
    if (!walkable(tx, tz) || !walkable(sx, sz)) return {};
    if (lineClear(sx, sz, tx, tz)) return {{wx1, wz1, 0}};

    // A* over cells, octile heuristic.
    struct Node { float f; int idx; };
    auto cmp = [](const Node& a, const Node& b) { return a.f > b.f; };
    std::vector<Node> open;
    std::vector<float> g(size_t(w_) * h_, 1e30f);
    std::vector<int> from(size_t(w_) * h_, -1);
    auto hcost = [&](int x, int z) {
        float ax = float(std::abs(x - tx)), az = float(std::abs(z - tz));
        return std::max(ax, az) + 0.41421f * std::min(ax, az);
    };
    int start = sz * w_ + sx, goal = tz * w_ + tx;
    g[size_t(start)] = 0;
    open.push_back({hcost(sx, sz), start});
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    int expansions = 0;
    while (!open.empty() && expansions++ < 400000) {
        std::pop_heap(open.begin(), open.end(), cmp);
        Node n = open.back();
        open.pop_back();
        if (n.idx == goal) break;
        int cx = n.idx % w_, cz = n.idx / w_;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + DX[d], nz = cz + DZ[d];
            if (!walkable(nx, nz)) continue;
            if (d >= 4 && (!walkable(cx + DX[d], cz) || !walkable(cx, cz + DZ[d])))
                continue;   // no diagonal corner cutting
            float step = d >= 4 ? 1.41421f : 1.0f;
            float ng = g[size_t(n.idx)] + step;
            int ni = nz * w_ + nx;
            if (ng < g[size_t(ni)]) {
                g[size_t(ni)] = ng;
                from[size_t(ni)] = n.idx;
                open.push_back({ng + hcost(nx, nz), ni});
                std::push_heap(open.begin(), open.end(), cmp);
            }
        }
    }
    if (from[size_t(goal)] < 0) return {};

    std::vector<std::pair<int, int>> cells;
    for (int i = goal; i >= 0; i = from[size_t(i)]) {
        cells.push_back({i % w_, i / w_});
        if (i == start) break;
    }
    std::reverse(cells.begin(), cells.end());

    // Simplify: greedily skip waypoints with a clear straight line.
    std::vector<Order> out;
    size_t anchor = 0;
    for (size_t i = 2; i < cells.size(); ++i) {
        if (!lineClear(cells[anchor].first, cells[anchor].second, cells[i].first,
                       cells[i].second)) {
            anchor = i - 1;
            out.push_back({cells[anchor].first * 16.0f + 8, cells[anchor].second * 16.0f + 8, 0});
        }
    }
    out.push_back({wx1, wz1, 0});
    return out;
}

void World::order(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    if (!queue) u->orders.clear();
    if (u->type->canFly) {
        u->orders.push_back({x, z, 0});
        return;
    }
    const NavGrid& grid = navFor(u->type);
    if (!grid.empty()) {
        auto path = grid.findPath(u->x, u->z, x, z);
        if (!path.empty()) {
            for (const auto& o : path) u->orders.push_back(o);
            return;
        }
    }
    u->orders.push_back({x, z, 0});
}

void World::loadInto(int unitId, int transportId) {
    Unit* u = unit(unitId);
    Unit* t = unit(transportId);
    if (!u || !t || !u->alive() || !t->alive() || u->embarked()) return;
    if (!u->type || !u->type->canMove || u->type->domain != UnitType::Domain::Ground)
        return;
    if (!t->type || !t->type->canTransport || u->team != t->team) return;
    if (int(t->cargo.size()) >= t->type->transportCap) return;
    u->orders.clear();
    Order o;
    o.targetId = transportId;
    o.load = true;
    u->orders.push_back(o);
}

void World::unloadAt(int transportId, float x, float z) {
    Unit* t = unit(transportId);
    if (!t || !t->alive() || t->cargo.empty()) return;
    t->orders.clear();
    // Sail there through the transport's own domain, then disembark.
    const NavGrid& grid = navFor(t->type);
    if (!grid.empty()) {
        auto path = grid.findPath(t->x, t->z, x, z);
        for (size_t i = 0; i + 1 < path.size(); ++i) t->orders.push_back(path[i]);
    }
    Order o;
    o.x = x;
    o.z = z;
    o.unload = true;
    t->orders.push_back(o);
}

void World::tickTransport(Unit& u, float dt) {
    (void)dt;
    Order& o = u.orders.front();
    if (o.load) {
        Unit* t = unit(o.targetId);
        if (!t || !t->alive() || !t->type ||
            int(t->cargo.size()) >= t->type->transportCap) {
            u.orders.pop_front();
            return;
        }
        float dx = t->x - u.x, dz = t->z - u.z;
        o.x = t->x;
        o.z = t->z;
        if (dx * dx + dz * dz < 70 * 70) {
            u.inTransport = t->id;
            t->cargo.push_back(u.id);
            u.orders.clear();
            u.speed = 0;
        }
        return;
    }
    // unload: sail close to the point, then place cargo on nearby land.
    float dx = o.x - u.x, dz = o.z - u.z;
    if (dx * dx + dz * dz > 150 * 150) return;   // keep sailing
    int cx = int(o.x) / 16, cz = int(o.z) / 16;
    for (int id : u.cargo) {
        Unit* c = unit(id);
        if (!c) continue;
        // Spiral out for a free ground cell.
        for (int r = 0; r < 12 && c->inTransport; ++r)
            for (int j = -r; j <= r && c->inTransport; ++j)
                for (int i = -r; i <= r && c->inTransport; ++i) {
                    if (std::max(std::abs(i), std::abs(j)) != r) continue;
                    if (!nav_.walkable(cx + i, cz + j)) continue;
                    c->x = float(cx + i) * 16 + 8;
                    c->z = float(cz + j) * 16 + 8;
                    c->inTransport = 0;
                }
    }
    std::erase_if(u.cargo, [&](int id) {
        Unit* c = unit(id);
        return !c || !c->inTransport;
    });
    if (u.cargo.empty()) u.orders.pop_front();
}

void World::attackMove(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    size_t before = queue ? u->orders.size() : 0;
    order(unitId, x, z, queue);
    for (size_t i = before; i < u->orders.size(); ++i) u->orders[i].attackMove = true;
}

void World::patrol(int unitId, float x, float z) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    u->orders.clear();
    Order a;
    a.x = u->x; a.z = u->z; a.patrol = true; a.attackMove = true;
    Order b;
    b.x = x; b.z = z; b.patrol = true; b.attackMove = true;
    u->orders.push_back(b);
    u->orders.push_back(a);
}

void World::guard(int unitId, int targetId, bool queue) {
    Unit* u = unit(unitId);
    Unit* t = unit(targetId);
    if (!u || !t || !u->alive() || !t->alive() || u->id == t->id) return;
    if (!u->type || !u->type->canMove || u->team != t->team) return;
    if (!queue) u->orders.clear();
    Order o;
    o.targetId = targetId;
    o.guard = true;
    o.x = t->x;
    o.z = t->z;
    u->orders.push_back(o);
}

void World::stop(int unitId) {
    Unit* u = unit(unitId);
    if (u) u->orders.clear();
}

void World::attack(int unitId, int targetId, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || u->type->weapon.damage <= 0) return;
    if (!queue) u->orders.clear();
    u->orders.push_back({0, 0, targetId});
}

void World::fire(Unit& u, Unit& target, int slot) {
    const Weapon& w = u.type->weapons[size_t(slot)];
    u.reloads[slot] = w.reload;
    u.reloadLeft = w.reload;
    u.justFired = true;
    if (w.melee || w.projVel <= 0) {
        target.hp -= w.damage;
        return;
    }
    Projectile p;
    p.x = u.x;
    p.z = u.z;
    float dx = target.x - u.x, dz = target.z - u.z;
    float dist = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f);
    float vel = w.projVel * kTick / 30.0f;   // weaponvelocity is already px/s-ish
    p.vx = dx / dist * vel;
    p.vz = dz / dist * vel;
    p.damage = w.damage;
    p.targetId = target.id;
    p.fromTeam = u.team;
    p.life = dist / vel + 0.5f;
    p.flight = dist / vel;
    projectiles_.push_back(p);
}

void World::tickCombat(Unit& u, float dt) {
    if (u.reloadLeft > 0) u.reloadLeft -= dt;
    for (auto& r : u.reloads)
        if (r > 0) r -= dt;

    // Auto-acquire: idle armed units engage the nearest enemy in reach;
    // attack-movers and patrollers interrupt their route to fight.
    bool acquiring = u.orders.empty() ||
                     (u.orders.front().targetId == 0 &&
                      (u.orders.front().attackMove || u.orders.front().patrol)) ||
                     u.orders.front().guard;
    if (acquiring && u.type->weapon.damage > 0) {
        float ar = u.type->maxRange() + 90;
        int best = 0;
        float bestD = ar * ar;
        for (auto& e : units_) {
            if (!e.alive() || e.embarked() || e.team == u.team || !e.type) continue;
            float dx = e.x - u.x, dz = e.z - u.z;
            float d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; best = e.id; }
        }
        if (best) u.orders.push_front({0, 0, best});
    }
    if (u.orders.empty() || u.orders.front().targetId == 0) return;

    if (u.orders.front().guard) {
        Order& o = u.orders.front();
        Unit* t = unit(o.targetId);
        if (!t || !t->alive()) {
            u.orders.pop_front();
            return;
        }
        o.x = t->x;
        o.z = t->z;
        float dx = t->x - u.x, dz = t->z - u.z;
        if (dx * dx + dz * dz <= 70 * 70)
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
        return;   // movement walks toward o when out of reach
    }

    Unit* target = unit(u.orders.front().targetId);
    if (!target || !target->alive()) {
        u.orders.pop_front();
        return;
    }
    float dx = target->x - u.x, dz = target->z - u.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    float best = u.type->maxRange();
    if (!u.type->canMove && dist > best) {      // static units can't chase
        u.orders.pop_front();
        return;
    }
    if (dist > best * 0.95f) {
        // Advance toward the target, steering around impassable terrain.
        u.repathLeft -= dt;
        Order& o = u.orders.front();
        o.x = target->x;
        o.z = target->z;
        const NavGrid& grid = navFor(u.type);
        if (!u.type->canFly && !grid.empty() && u.repathLeft <= 0) {
            u.repathLeft = 0.7f;
            auto path = grid.findPath(u.x, u.z, target->x, target->z);
            if (!path.empty()) {
                o.x = path.front().x;
                o.z = path.front().z;
            }
        }
        return;   // movement handled by the normal move logic
    }
    // In range: stop and face the target.
    u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
    float want = std::atan2(dx, dz);
    float diff = angleDiff(want, u.heading);
    float maxTurn = u.type->turnRate * dt;
    u.heading += std::clamp(diff, -maxTurn, maxTurn);
    if (std::abs(diff) < 0.2f)
        for (size_t i = 0; i < u.type->weapons.size() && i < 3; ++i)
            if (u.reloads[i] <= 0 && dist <= u.type->weapons[i].range * 1.0f)
                fire(u, *target, int(i));
}

bool World::canPlace(const UnitType* type, float x, float z) const {
    if (!type) return false;
    int cx = int(x) / 16 - type->footX / 2, cz = int(z) / 16 - type->footZ / 2;
    for (int j = 0; j < type->footZ; ++j)
        for (int i = 0; i < type->footX; ++i)
            if (!nav_.walkable(cx + i, cz + j)) return false;
    for (const auto& u : units_) {
        if (!u.alive()) continue;
        float dx = u.x - x, dz = u.z - z;
        float min = 16.0f * float(std::max(type->footX, type->footZ)) / 2 + 12;
        if (dx * dx + dz * dz < min * min) return false;
    }
    return true;
}

int World::startBuild(int builderId, const UnitType* type, float x, float z) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || !b->type->canMove)
        return 0;
    if (!canPlace(type, x, z)) return 0;
    int id = spawn(type, x, z, 3.14159f, b->team);
    Unit* site = unit(id);
    site->underConstruction = true;
    site->hp = type->maxHp * 0.05f;
    if (!type->canMove) blockFootprint(nav_, *type, x, z, true);
    b = unit(builderId);   // spawn may have reallocated units_
    b->buildSiteId = id;
    order(builderId, x, z + float(type->footZ) * 8 + 24, false);
    return id;
}

void World::tickConstruction(Unit& b, float dt) {
    Unit* site = unit(b.buildSiteId);
    if (!site || !site->alive() || !site->underConstruction) {
        b.buildSiteId = 0;
        return;
    }
    float dx = site->x - b.x, dz = site->z - b.z;
    float reach = 16.0f * float(std::max(site->type->footX, site->type->footZ)) / 2 + 40;
    if (dx * dx + dz * dz > reach * reach) return;   // still walking there
    b.orders.clear();
    b.speed = 0;
    float total = site->type->buildTime / std::max(b.type->workerTime, 0.01f);
    Team& tm = teams_[size_t(b.team)];
    float cost = site->type->buildCost * dt / std::max(total, 0.01f);
    if (tm.mana < cost) return;
    tm.mana -= cost;
    site->hp += site->type->maxHp * 0.95f * dt / std::max(total, 0.01f);
    if (site->hp >= site->type->maxHp) {
        site->hp = site->type->maxHp;
        site->underConstruction = false;
        b.buildSiteId = 0;
    }
}

void World::train(int builderId, const UnitType* type) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !type) return;
    b->buildQueue.push_back(type);
}

void World::updateVisibility() {
    if (nav_.empty()) return;
    if (vis_.empty()) {
        visW_ = nav_.width();
        visH_ = nav_.height();
        vis_.assign(size_t(visW_) * visH_, 0);
    }
    for (auto& v : vis_)
        if (v == 2) v = 1;
    for (const auto& u : units_) {
        if (!u.alive() || u.team != visTeam_ || !u.type) continue;
        int r = int(u.type->sight) / 16 + 1;
        int cx = int(u.x) / 16, cz = int(u.z) / 16;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dz * dz > r * r) continue;
                int x = cx + dx, z = cz + dz;
                if (x >= 0 && z >= 0 && x < visW_ && z < visH_)
                    vis_[size_t(z) * visW_ + x] = 2;
            }
    }
}

void World::tickProduction(Unit& u, float dt) {
    if (u.underConstruction || u.buildQueue.empty()) return;
    const UnitType* t = u.buildQueue.front();
    float total = t->buildTime / std::max(u.type->workerTime, 0.01f);
    Team& tm = teams_[size_t(u.team)];
    float cost = t->buildCost * dt / std::max(total, 0.01f);
    if (tm.mana < cost) return;   // stalled: no mana
    tm.mana -= cost;
    u.buildProgress += dt;
    if (u.buildProgress >= total) {
        u.buildProgress = 0;
        u.buildQueue.pop_front();
        // Spawn just south of the footprint, walk to a rally point.
        float sx = u.x, sz = u.z + float(u.type->footZ) * 8 + 20;
        int id = spawn(t, sx, sz, 3.14159f, u.team);
        order(id, sx + float((id % 5) - 2) * 22, sz + 60, false);
        u.justBuilt = id;
    }
}

void World::tick(float dt) {
    for (auto& u : units_) { u.justFired = false; u.justBuilt = 0; }

    // Economy: recompute income/storage, apply income.
    for (auto& tm : teams_) { tm.income = 0; tm.storage = 0; }
    for (auto& u : units_) {
        if (!u.alive() || !u.type) continue;
        auto& tm = teams_[size_t(u.team)];
        tm.income += u.type->income;
        tm.storage += u.type->storage;
    }
    for (auto& tm : teams_)
        tm.mana = std::min(tm.mana + tm.income * dt, std::max(tm.storage, 100.0f));

    for (auto& u : units_)
        if (u.alive() && u.type) tickProduction(u, dt);
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (u.alive() && u.type && u.buildSiteId) tickConstruction(u, dt);
    }

    visTimer_ -= dt;
    if (visTimer_ <= 0) {
        visTimer_ = 0.25f;
        updateVisibility();
    }

    // Projectiles.
    for (auto& p : projectiles_) {
        p.x += p.vx * dt;
        p.z += p.vz * dt;
        p.life -= dt;
        p.age += dt;
        Unit* t = unit(p.targetId);
        if (t && t->alive() && !t->embarked()) {
            float dx = t->x - p.x, dz = t->z - p.z;
            if (dx * dx + dz * dz < 8 * 8) {
                t->hp -= p.damage;
                p.life = -1;
            }
        }
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return p.life <= 0; });

    for (auto& u : units_) {
        if (!u.type) continue;
        if (!u.alive()) { u.deadFor += dt; continue; }
        if (u.hp <= 0) { u.deadFor = 0; u.orders.clear(); u.speed = 0; continue; }

        if (u.underConstruction) continue;   // silent until finished
        if (u.embarked()) {                  // riding a transport
            Unit* t = unit(u.inTransport);
            if (t && t->alive()) { u.x = t->x; u.z = t->z; }
            else u.deadFor = 0;              // transport lost with all hands
            continue;
        }
        if (!u.orders.empty() && (u.orders.front().load || u.orders.front().unload))
            tickTransport(u, dt);
        else
            tickCombat(u, dt);

        bool combatHold =
            !u.orders.empty() && u.orders.front().targetId != 0 &&
            !u.orders.front().load && !u.orders.front().guard && [&] {
                Unit* t = unit(u.orders.front().targetId);
                if (!t) return false;
                float dx = t->x - u.x, dz = t->z - u.z;
                return std::sqrt(dx * dx + dz * dz) <= u.type->maxRange() * 0.95f;
            }();
        if (combatHold) continue;

        if (u.orders.empty()) {
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
        } else {
            const Order& o = u.orders.front();
            float dx = o.x - u.x, dz = o.z - u.z;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (o.guard && dist <= 70.0f) continue;   // in escort position
            if (dist < 3.0f) {
                if (o.targetId == 0) {
                    Order done = o;
                    u.orders.pop_front();
                    if (done.patrol) u.orders.push_back(done);
                }
                continue;
            }
            float want = std::atan2(dx, dz);
            float diff = angleDiff(want, u.heading);
            float maxTurn = u.type->turnRate * dt;
            u.heading += std::clamp(diff, -maxTurn, maxTurn);

            // Brake into the waypoint if it's the last one; slow for big turns.
            float target = u.type->maxVel;
            if (std::abs(diff) > 0.8f) target *= 0.3f;
            bool last = u.orders.size() == 1;
            if (last) {
                float stopDist = u.speed * u.speed / (2 * u.type->brake);
                if (dist < stopDist) target = 0;
            }
            if (u.speed < target)
                u.speed = std::min(u.speed + u.type->accel * dt, target);
            else
                u.speed = std::max(u.speed - u.type->brake * dt, target);

            u.x += std::sin(u.heading) * u.speed * dt;
            u.z += std::cos(u.heading) * u.speed * dt;
        }
    }

    // Separation: push overlapping mobile units apart.
    constexpr float kSep = 13.0f;
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& a = units_[i];
        if (!a.alive() || a.embarked() || !a.type || !a.type->canMove || a.type->canFly) continue;
        for (size_t j = i + 1; j < units_.size(); ++j) {
            Unit& b = units_[j];
            if (!b.alive() || b.embarked() || !b.type || !b.type->canMove || b.type->canFly) continue;
            float dx = b.x - a.x, dz = b.z - a.z;
            float d2 = dx * dx + dz * dz;
            if (d2 >= kSep * kSep || d2 < 1e-6f) {
                if (d2 < 1e-6f) { b.x += 1.0f; }   // exactly stacked: nudge
                continue;
            }
            float d = std::sqrt(d2);
            float push = (kSep - d) * 0.5f;
            dx /= d; dz /= d;
            a.x -= dx * push; a.z -= dz * push;
            b.x += dx * push; b.z += dz * push;
        }
    }
}

uint64_t World::stateHash() const {
    // FNV-1a over the quantities that must agree between lockstep peers.
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFF;
            h *= 1099511628211ULL;
        }
    };
    auto mixf = [&](float f) {
        uint32_t b;
        static_assert(sizeof b == sizeof f);
        std::memcpy(&b, &f, 4);
        mix(b);
    };
    for (const auto& u : units_) {
        mix(uint64_t(u.id));
        mix(uint64_t(u.team));
        mixf(u.x);
        mixf(u.z);
        mixf(u.hp);
        mixf(u.heading);
        mix(uint64_t(u.orders.size()));
        mix(uint64_t(u.alive() ? 1 : 0));
    }
    mix(uint64_t(projectiles_.size()));
    for (const auto& t : teams_) mixf(t.mana);
    return h;
}

} // namespace tak::sim
