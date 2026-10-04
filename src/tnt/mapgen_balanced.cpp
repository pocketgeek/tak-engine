#include "tnt/mapgen_internal.h"
#include "hpi/hpi.h"
#include "sim/sim.h"
#include "tdf/tdf.h"

#include <deque>
#include <map>
#include <stdexcept>

namespace tak::mapgen {
using namespace detail;

const char* layoutName(uint8_t layout) {
    switch (layout) {
        case Lakes: return "Lakes";
        case Islands: return "Islands";
        case Maze: return "Taros Maze";
        case Ports: return "Veruna Ports";
        case Riverlands: return "Aramon Riverlands";
        case Jungle: return "Zhon Clearings";
        case Highlands: return "Creon Highlands";
        default: return "Mainland";
    }
}

uint8_t themedLayout(uint8_t world) {
    constexpr uint8_t layouts[]={Riverlands,Maze,Ports,Jungle,Highlands};
    return layouts[world % kMapTypes];
}
bool automaticWater(uint8_t layout) {
    return layout==Islands || layout==Ports || layout==Riverlands || layout==Maze || layout==Highlands;
}

namespace {
using Point = std::pair<int,int>;
struct FeatureSize { int x=1,z=1; bool blocking=false; };

std::map<std::string,FeatureSize> featureSizes(const hpi::Vfs& vfs,uint8_t world) {
    static const char* names[]={"aramon","taros","veruna","zhon","creon"};
    auto paths=vfs.list(std::string("features/")+names[world]);
    std::sort(paths.begin(),paths.end());
    std::map<std::string,FeatureSize> out;
    for (const auto& path:paths) {
        if (!hpi::MountSet::key(path).ends_with(".tdf")) continue;
        const auto data=vfs.read(path);
        const auto root=tdf::parseText(std::string(data.begin(),data.end()),path);
        for (const auto& [name,node]:root.children)
            out[name]={std::clamp(int(node.numberOr("footprintx",1)),1,64),
                       std::clamp(int(node.numberOr("footprintz",1)),1,64),
                       node.numberOr("blocking",0)!=0};
    }
    return out;
}

// This is map validation, not a new movement algorithm. Use the engine's retail
// terrain classifier and footprint clearance, then flood four-way legal steps.
std::vector<int> distances(const sim::NavGrid& nav,Point start,int foot) {
    const int w=nav.width(),h=nav.height();
    std::vector<int> d(size_t(w)*h,-1),q;
    if (!nav.fits(start.first,start.second,foot)) return d;
    q.reserve(d.size());q.push_back(start.second*w+start.first);d[q[0]]=0;
    for (size_t head=0;head<q.size();++head) {
        const int c=q[head],x=c%w,z=c/w;
        for (const auto [dx,dz]:std::array<Point,4>{{{0,-1},{1,0},{0,1},{-1,0}}}) {
            const int nx=x+dx,nz=z+dz;
            if (nx<0||nz<0||nx>=w||nz>=h) continue;
            const int n=nz*w+nx;
            if (d[n]<0&&nav.fits(nx,nz,foot)) { d[n]=d[c]+1;q.push_back(n); }
        }
    }
    return d;
}

// Each island has a generous cell allocation, including offshore launch space.
// Mainland starts follow an evenly sampled octagon, rather than rounding each
// player down to one of eight compass directions (which disadvantaged odd counts).
std::vector<Point> planStarts(const Params& p) {
    std::vector<Point> out;
    const int w=p.widthCells,h=p.heightCells;
    for (int k=0;k<p.players;++k) {
        int x,z;
        if (p.layout==Islands) {
            const int cols=p.players<=4?2:3,rows=(p.players+cols-1)/cols;
            x=(2*(k%cols)+1)*w/(2*cols);z=(2*(k/cols)+1)*h/(2*rows);
        } else {
            const int scaled=(k*8000/p.players+int(p.seed%8)*1000)%8000,edge=scaled/1000,t=scaled%1000;
            const auto a=kCompass[edge],b=kCompass[(edge+1)%8];
            x=w/2+(a.first*(1000-t)+b.first*t)*(w*35/100)/1000000;
            z=h/2+(a.second*(1000-t)+b.second*t)*(h*35/100)/1000000;
        }
        // Centre a base between section corners. A 96-cell square of level ground
        // fits within the four corner intervals reserved below.
        x=std::clamp((x/32)*32+16,48,w-48);
        z=std::clamp((z/32)*32+16,48,h-48);
        out.emplace_back(x,z);
    }
    return out;
}
}

Result generateBalanced(const Params& p,const hpi::Vfs& vfs) {
    Result r;r.starts=planStarts(p);
    for(size_t i=0;i<r.starts.size();++i)for(size_t j=0;j<i;++j) {
        const int dx=r.starts[i].first-r.starts[j].first,dz=r.starts[i].second-r.starts[j].second;
        if(dx*dx+dz*dz<72*72)throw std::runtime_error("Random map starts are too close");
    }
    auto& m=r.map;m.stockTerrain=true;
    const int w=p.widthCells,h=p.heightCells,sw=w/32,sh=h/32;
    const auto levels=kLevels[p.mapType];const int land=levels.land;
    m.width=w;m.height=h;m.blocksX=w/2;m.blocksY=h/2;m.seaLevel=levels.sea;
    const size_t cells=size_t(w)*h,blocks=size_t(w/2)*(h/2);
    std::vector<uint8_t> wet(size_t(sw+1)*(sh+1)),reserved(cells),claim(cells),obstacles(cells);
    const auto mark=[&](int x,int z,int radius,uint8_t value) {
        for (int nz=std::max(0,z-radius);nz<=std::min(h-1,z+radius);++nz)
            for (int nx=std::max(0,x-radius);nx<=std::min(w-1,x+radius);++nx)
                reserved[size_t(nz)*w+nx]|=value;
    };
    const auto reserveRoute=[&](Point a,Point b,int radius) {
        const int steps=std::max(std::abs(b.first-a.first),std::abs(b.second-a.second));
        for (int i=0;i<=steps;++i)
            mark(a.first+(b.first-a.first)*i/std::max(1,steps),
                 a.second+(b.second-a.second)*i/std::max(1,steps),radius,1);
    };
    for (const auto s:r.starts) {
        mark(s.first,s.second,43,2); // home economy: terrain is flat; scenery allowed only outside base/routes
        mark(s.first,s.second,14,1);
        if (p.layout!=Islands && p.layout!=Maze && p.layout!=Ports) reserveRoute(s,{w/2,h/2},8);
    }
    if (p.layout==Ports) for (size_t i=0;i<r.starts.size();++i) {
        // Coastal settlements surround a shared basin. Keep the perimeter dry;
        // radial routes would split the basin into disconnected ponds.
        const auto a=r.starts[i];
        reserveRoute(a,{a.first< w/2 ? 48:w-48,a.second},8);
    }
    if (p.layout==Ports) {
        reserveRoute({48,48},{w-48,48},8);reserveRoute({w-48,48},{w-48,h-48},8);
        reserveRoute({w-48,h-48},{48,h-48},8);reserveRoute({48,h-48},{48,48},8);
    }
    const int islandCols=p.players<=4?2:3;
    const int islandRows=(p.players+islandCols-1)/islandCols;
    const int islandRadius=48+32*std::max(0,(std::min(w/islandCols,h/islandRows)-192)/128);
    // Coherent low-frequency water; legacy lakes stay inland, new lakes may
    // reach the map edge. Zero means genuinely dry. Amount is an intensity, not a coverage %.
    for (int z=0;z<=sh;++z) for (int x=0;x<=sw;++x) {
        bool water=p.layout==Islands;
        if (!automaticWater(p.layout)&&p.waterDensity) {
            const int threshold=p.layout==Lakes ? 75+int(p.waterDensity)*90/255 : 25+int(p.waterDensity)*115/255;
            water=int(fractal(p.seed,x*32,z*32,std::max(w,h)))<threshold;
            if (p.formatVer<5&&p.layout==Lakes&&(x<1||z<1||x>sw-1||z>sh-1)) water=false;
        }
        if (p.layout==Ports) water=x>=3 && x<=sw-3 && z>=3 && z<=sh-3;
        if (p.layout==Riverlands) {
            const int bend=int(noise(p.seed,x*32,0,w/3)%3)-1;
            water=std::abs(z-sh/2-bend)<=1 || std::abs(x-sw/2)<=1;
        }
        for (const auto [sx,sz]:r.starts) {
            const int dx=std::abs(x*32-sx),dz=std::abs(z*32-sz);
            const int radius=p.layout==Islands?islandRadius:48;
            // Round larger islands without cutting into their identical home areas.
            if(dx<=radius&&dz<=radius&&(radius<=48||dx+dz<=2*radius-32))water=false;
        }
        wet[size_t(z)*(sw+1)+x]=uint8_t(water);
    }
    // Reserve broad dry corridors in the corner mask before stamping art. Never
    // flatten or carve a route through already-painted coastline/cliff terrain.
    if (p.layout!=Islands) for (int z=0;z<=sh;++z) for (int x=0;x<=sw;++x) {
        for (int dz=-24;dz<=24;dz+=8) for (int dx=-24;dx<=24;dx+=8) {
            const int nx=x*32+dx,nz=z*32+dz;
            if (nx>=0&&nz>=0&&nx<w&&nz<h&&(reserved[size_t(nz)*w+nx]&1))
                wet[size_t(z)*(sw+1)+x]=0;
        }
    }
    const auto cellCase=[&](int x,int z) {
        const size_t i=size_t(z)*(sw+1)+x;
        return wet[i]|(wet[i+1]<<1)|(wet[i+sw+1]<<2)|(wet[i+sw+2]<<3);
    };
    // Monotone cleanup terminates: each change clears a previously wet corner.
    bool changed=true;
    while (changed) {
        changed=false;
        for (int z=0;z<sh;++z) for (int x=0;x<sw;++x) {
            const int c=cellCase(x,z);
            if (c==6||c==9) {wet[size_t(z)*(sw+1)+x+(c==6)]=0;changed=true;}
        }
    }
    m.heights.resize(cells);m.features.assign(cells,0xffff);
    m.tileKeys.resize(blocks);m.tileCols.resize(blocks);m.tileRows.resize(blocks);
    const auto art=kWorldArt[p.mapType];
    for (int z=0;z<h;++z) for (int x=0;x<w;++x)
        m.heights[size_t(z)*w+x]=cellCase(x/32,z/32)==15?0:uint8_t(land);
    for (int z=0;z<h/2;++z) for (int x=0;x<w/2;++x) {
        const bool sea=cellCase(x/16,z/16)==15;const int period=sea?art.seaK:art.groundK;
        const size_t i=size_t(z)*(w/2)+x;
        m.tileKeys[i]=sea?art.sea:art.ground;m.tileCols[i]=uint8_t(x%period);m.tileRows[i]=uint8_t(z%period);
    }
    const auto sizes=featureSizes(vfs,p.mapType);
    const auto sizeOf=[&](const std::string& name)->const FeatureSize* {
        auto it=sizes.find(hpi::MountSet::key(name));return it==sizes.end()?nullptr:&it->second;
    };
    const auto intern=[&](const std::string& name) {
        auto it=std::find(m.featureNames.begin(),m.featureNames.end(),name);
        if (it!=m.featureNames.end()) return uint16_t(it-m.featureNames.begin());
        m.featureNames.push_back(name);return uint16_t(m.featureNames.size()-1);
    };
    const auto putFeature=[&](int x,int z,const std::string& name,bool sacred=false) {
        const auto* f=sizeOf(name);
        if (!f||x<0||z<0||x+f->x>w||z+f->z>h) return false;
        m.features[size_t(z)*w+x]=intern(name);
        for (int dz=0;dz<f->z;++dz) for (int dx=0;dx<f->x;++dx) {
            const size_t i=size_t(z+dz)*w+x+dx;claim[i]=1;
            if (f->blocking&&!sacred) obstacles[i]=1;
        }
        return true;
    };
    // Sorted path lookup and explicit variant order keep output independent of
    // archive enumeration and standard-library hash-table iteration order.
    std::map<std::string,tnt::Map> cache;
    const auto load=[&](const std::string& path)->const tnt::Map* {
        auto it=cache.find(path);if(it!=cache.end())return &it->second;
        if (!vfs.has(path))return nullptr;
        return &cache.emplace(path,tnt::Map::load(vfs.read(path),path)).first->second;
    };
    const auto stamp=[&](const tnt::Map& piece,int x0,int z0) {
        for (int z=0;z<piece.height;++z) for (int x=0;x<piece.width;++x) {
            const size_t src=size_t(z)*piece.width+x,dst=size_t(z0+z)*w+x0+x;
            m.heights[dst]=piece.heights[src];
            const auto f=piece.features[src];
            if(f==0xfffb||f==0xfffc) {
                m.features[dst]=f;
                if(f==0xfffc)obstacles[dst]=claim[dst]=1;
            }
            if (f<piece.featureNames.size()&&!reserved[dst]) putFeature(x0+x,z0+z,piece.featureNames[f]);
        }
        for (int z=0;z<piece.blocksY;++z) for (int x=0;x<piece.blocksX;++x) {
            const size_t src=size_t(z)*piece.blocksX+x,dst=size_t(z0/2+z)*(w/2)+x0/2+x;
            m.tileKeys[dst]=piece.tileKeys[src];m.tileCols[dst]=piece.tileCols[src];m.tileRows[dst]=piece.tileRows[src];
        }
    };
    const auto& kit=kShoreKit[p.mapType];
    for (int z=0;z<sh;++z) for (int x=0;x<sw;++x) {
        const int role=kCaseRole[cellCase(x,z)];if(role<0)continue;
        const tnt::Map* piece=nullptr;
        const int variant=int(latticeVal(p.seed,x,z)%3);
        for(int v=0;v<3&&!piece;++v)
            piece=load(std::string(kit.dir)+kit.name[role]+kit.var[(variant+v)%3]+kit.ext);
        if(!piece||piece->width!=32||piece->height!=32)
            throw std::runtime_error("Random map needs the retail coastline sections for this world");
        stamp(*piece,x*32,z*32);
    }
    // Themed uplands use complete authored cliff/hillside pieces, not painted
    // blockers or height-only walls. A low corner uses the shore kit's wet role.
    if (p.layout==Maze || p.layout==Highlands) {
        std::vector<uint8_t> high(wet.size(),1);
        const auto carve=[&](int x,int z) {
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)
                if(x+dx>=0&&z+dz>=0&&x+dx<=sw&&z+dz<=sh)
                    high[size_t(z+dz)*(sw+1)+x+dx]=0;
        };
        const auto corridor=[&](Point a,Point b) {
            reserveRoute({a.first*32,a.second*32},{b.first*32,a.second*32},8);
            reserveRoute({b.first*32,a.second*32},{b.first*32,b.second*32},8);
            while(a.first!=b.first) {carve(a.first,a.second);a.first+=a.first<b.first?1:-1;}
            while(a.second!=b.second) {carve(a.first,a.second);a.second+=a.second<b.second?1:-1;}
            carve(a.first,a.second);
        };
        if(p.layout==Maze) {
            // Seeded depth-first maze on a coarse lattice. Wide corridors admit
            // armies; home clearings connect to the nearest maze node.
            const int cols=(sw-6)/4+1,rows=(sh-6)/4+1;
            std::vector<uint8_t> seen(size_t(cols)*rows);
            std::vector<Point> stack{{0,0}};seen[0]=1;
            uint64_t rng=p.seed^0x4d415a45ULL;
            while(!stack.empty()) {
                auto [x,z]=stack.back(); std::vector<Point> next;
                for(auto [dx,dz]:std::array<Point,4>{{{1,0},{0,1},{-1,0},{0,-1}}})
                    if(x+dx>=0&&z+dz>=0&&x+dx<cols&&z+dz<rows&&!seen[size_t(z+dz)*cols+x+dx]) next.emplace_back(x+dx,z+dz);
                if(next.empty()) {stack.pop_back();continue;}
                auto n=next[splitmix(rng)%next.size()];seen[size_t(n.second)*cols+n.first]=1;
                corridor({3+4*x,3+4*z},{3+4*n.first,3+4*n.second});stack.push_back(n);
            }
            // Extend edge corridors to the boundary rather than enclosing the
            // entire battlefield in an unbroken cliff wall. Keep old recipes stable.
            if(p.formatVer>=7) {
                for(int x=0;x<cols;++x) {
                    corridor({3+4*x,3},{3+4*x,0});
                    corridor({3+4*x,3+4*(rows-1)},{3+4*x,sh});
                }
                for(int z=0;z<rows;++z) {
                    corridor({3,3+4*z},{0,3+4*z});
                    corridor({3+4*(cols-1),3+4*z},{sw,3+4*z});
                }
            }
            for(auto [x,z]:r.starts) corridor({x/32,z/32},
                {3+4*std::clamp((x/32-1)/4,0,cols-1),3+4*std::clamp((z/32-1)/4,0,rows-1)});
        } else {
            for(int z=0;z<=sh;++z)for(int x=0;x<=sw;++x)
                high[size_t(z)*(sw+1)+x]=p.reliefDensity && fractal(p.seed^0x48494748,x*32,z*32,std::max(w,h))<80+int(p.reliefDensity)*100/255;
        }
        for(int z=0;z<=sh;++z)for(int x=0;x<=sw;++x) {
            for(auto [sx,sz]:r.starts) if(std::abs(x*32-sx)<=48&&std::abs(z*32-sz)<=48) high[size_t(z)*(sw+1)+x]=0;
            if(p.layout==Highlands) for(int dz=-24;dz<=24;dz+=8)for(int dx=-24;dx<=24;dx+=8) {
                const int nx=x*32+dx,nz=z*32+dz;
                if(nx>=0&&nz>=0&&nx<w&&nz<h&&(reserved[size_t(nz)*w+nx]&1)) high[size_t(z)*(sw+1)+x]=0;
            }
        }
        const auto highCase=[&](int x,int z) {const size_t i=size_t(z)*(sw+1)+x;
            return high[i]|(high[i+1]<<1)|(high[i+sw+1]<<2)|(high[i+sw+2]<<3);};
        bool clean=false;while(!clean) {clean=true;
            for(int z=0;z<sh;++z)for(int x=0;x<sw;++x) {const int c=highCase(x,z);
                if(c==6||c==9) {high[size_t(z)*(sw+1)+x+(c==6)]=0;clean=false;}}
        }
        const bool maze=p.layout==Maze;
        const char* creonNames[]={"n","s","e","w","nw","ne","se","sw","n_w","n_e","s_w","s_e"};
        const auto* top=load(maze?"Sections/Taros/High Flats/high_ground.TNT":"sections/creon/flatties/highland512.tnt");
        if(!top||top->width!=32||top->height!=32) throw std::runtime_error("Themed map needs retail high-ground sections");
        for(int z=0;z<sh;++z)for(int x=0;x<sw;++x) {
            const int c=highCase(x,z);
            if(c==0) continue;
            const tnt::Map* piece=top;
            if(c!=15) {
                const int role=kCaseRole[15-c];
                const std::string path=maze?std::string("Sections/Taros/Low to High Cliffs/")+kShoreKit[Taros].name[role]+"01.TNT":
                    std::string("sections/creon/hillsides/")+creonNames[role]+"01.tnt";
                piece=load(path);
            }
            if(!piece||piece->width!=32||piece->height!=32) throw std::runtime_error("Themed map needs retail cliff/hillside sections");
            stamp(*piece,x*32,z*32);
        }
    }
    // Authored relief: copy artwork AND its matching heights. Only self-contained
    // low-ground patches with near-level edges are admitted. No invisible mesas.
    std::vector<const tnt::Map*> hills;
    if(p.reliefDensity && p.layout!=Maze && p.layout!=Highlands) {
        auto paths=vfs.list(kReliefDir[p.mapType]);std::sort(paths.begin(),paths.end());
        for(const auto& path:paths) {
            const auto key=hpi::MountSet::key(path);
            if(!key.ends_with(".tnt"))continue;
            if(p.formatVer>=6) {
                // Audited retail surface families: matching height alone does
                // not make Veruna grass breakers fit sand, or Taros cobbles fit
                // low ground. These kits have no surrounding transition here.
                if(p.mapType==Veruna && key.find("/breaker")!=std::string::npos)continue;
                if(p.mapType==Taros && (key.ends_with("/unique23.tnt") ||
                    key.ends_with("/unique25.tnt") || key.ends_with("/unique27.tnt")))continue;
            }
            const auto* piece=load(path);if(!piece||piece->width>64||piece->height>64)continue;
            bool good=true;int high=land;
            for(int z=0;z<piece->height;++z)for(int x=0;x<piece->width;++x) {
                int height=piece->heights[size_t(z)*piece->width+x];high=std::max(high,height);
                if(height<land-4)good=false;
                if((x<2||z<2||x>=piece->width-2||z>=piece->height-2)&&std::abs(height-land)>4)good=false;
            }
            if(good&&high>=land+16)hills.push_back(piece);
        }
    }
    std::vector<uint8_t> relief(cells);
    uint64_t terrainRng=p.seed^0x173842febaULL;
    for(int z=16;z<h-32;z+=32)for(int x=16;x<w-32;x+=32) {
        if(hills.empty()||splitmix(terrainRng)%512>=p.reliefDensity)continue;
        const auto& piece=*hills[splitmix(terrainRng)%hills.size()];
        bool good=x+piece.width<w&&z+piece.height<h;
        for(int dz=-4;good&&dz<piece.height+4;++dz)for(int dx=-4;dx<piece.width+4;++dx) {
            const int nx=x+dx,nz=z+dz;
            if(nx<0||nz<0||nx>=w||nz>=h){good=false;break;}
            const size_t i=size_t(nz)*w+nx;
            if(reserved[i]||relief[i]||claim[i]||m.heights[i]!=land||cellCase(nx/32,nz/32)!=0){good=false;break;}
        }
        if(!good)continue;
        stamp(piece,x,z);++r.reliefPatches;
        for(int dz=0;dz<piece.height;++dz)for(int dx=0;dx<piece.width;++dx)relief[size_t(z+dz)*w+x+dx]=1;
    }
    // Reserve actual buildable rectangles and approach lanes before scenery. All
    // players receive the SAME tiers at the SAME walking distances on flat land.
    const auto& palette=kWorldFeat[p.mapType];
    const std::array<Point,3> homeOffsets={{{-24,-24},{24,-24},{0,28}}};
    std::vector<Point> deposits;
    const auto flat=[&](int x,int z,int fx,int fz) {
        if(x<1||z<1||x+fx>=w||z+fz>=h)return false;
        for(int dz=0;dz<fz;++dz)for(int dx=0;dx<fx;++dx) {
            const size_t i=size_t(z+dz)*w+x+dx;
            if(m.heights[i]!=land||claim[i])return false;
        }
        return true;
    };
    uint64_t featureRng=p.seed^0xbe72431851ULL;
    const auto decorateDeposit=[&](int x,int z) {
        // Decorative rings stay outside the reserved build footprint. The actual
        // TDF footprints, not nominal 3x3 guesses, decide whether an arc fits.
        const auto& spec=kRing[p.mapType];
        int count=0;
        bool occupiedSlots[8]={};
        for(int slot=0;slot<8&&count<3;++slot) {
            for(int j=0;j<spec.n;++j) {
                const auto& arc=spec.p[j];if(arc.slot!=spec.order[slot])continue;
                const auto* f=sizeOf(arc.name);if(!f)continue;
                int ax=x+arc.dx,az=z+arc.dz;
                if(ax<x+4&&ax+f->x>x-3&&az<z+4&&az+f->z>z-3)continue;
                bool clear=flat(ax,az,f->x,f->z);
                for(int dz=0;clear&&dz<f->z;++dz)for(int dx=0;dx<f->x;++dx)
                    if(reserved[size_t(az+dz)*w+ax+dx]&1){clear=false;break;}
                if(clear) {putFeature(ax,az,arc.name);++count;occupiedSlots[arc.slot]=true;}
                break;
            }
        }
        if (p.formatVer >= 4 && count < 3) {
            // Authored offsets often collide with the lodestone yard or a reserved
            // approach. Move the appropriate arc outward instead of leaving bare
            // mana. Keep its orientation and use the real footprint throughout.
            for (int radius=1; radius<=16 && count<3; ++radius) {
                for (int slot=0; slot<8 && count<3; ++slot) {
                    for (int j=0; j<spec.n && count<3; ++j) {
                        const auto& arc=spec.p[j];
                        if (arc.slot!=spec.order[slot] || occupiedSlots[arc.slot]) continue;
                        const auto* f=sizeOf(arc.name); if (!f) continue;
                        const int ax=x+arc.dx+(arc.dx<0?-radius:arc.dx>0?radius:0);
                        const int az=z+arc.dz+(arc.dz<0?-radius:arc.dz>0?radius:0);
                        if (ax<x+4 && ax+f->x>x-3 && az<z+4 && az+f->z>z-3) continue;
                        bool clear=flat(ax,az,f->x,f->z);
                        for (int dz=0; clear && dz<f->z; ++dz)
                            for (int dx=0; dx<f->x; ++dx)
                                if (reserved[size_t(az+dz)*w+ax+dx]&1) { clear=false; break; }
                        if (clear) { putFeature(ax,az,arc.name); ++count; occupiedSlots[arc.slot]=true; break; }
                    }
                }
            }
            if (count==0) throw std::runtime_error("Random map mana site has no space for ruins");
        }
    };
    const auto deposit=[&](int x,int z,int tier) {
        if(!putFeature(x,z,palette.sacred[tier],true))
            throw std::runtime_error("Random map is missing its world's sacred stone definitions");
        deposits.emplace_back(x,z);
        decorateDeposit(x,z);
        mark(x,z,7,1);
    };
    for(const auto s:r.starts)for(int tier=0;tier<3;++tier) {
        const Point d={s.first+homeOffsets[tier].first,s.second+homeOffsets[tier].second};
        if(!flat(d.first-5,d.second-5,12,12))throw std::runtime_error("Random map home resource area is obstructed");
        reserveRoute(s,d,5);deposit(d.first,d.second,tier);
    }
    sim::NavGrid::Limits groundLimits;groundLimits.maxSlope=16;groundLimits.maxWaterDepth=0;
    sim::NavGrid ground(m.heights,w,h,m.seaLevel,groundLimits);
    ground.setObstacles(&obstacles);
    // Open expansion sites, separated from homes and from one another. They must
    // lie on a reachable component, rather than merely looking dry on the map.
    std::vector<uint8_t> reachable(cells);
    std::vector<std::vector<int>> homeDistances;
    for(size_t k=0;k<r.starts.size();++k) {
        auto d=distances(ground,r.starts[k],6);
        if(p.layout!=Islands&&k==0)for(const auto s:r.starts)
            if(d[size_t(s.second)*w+s.first]<0)throw std::runtime_error("Random map has disconnected starts");
        for(size_t i=0;i<cells;++i)if(d[i]>=0)reachable[i]=1;
        homeDistances.push_back(std::move(d));
    }
    // Add expansions in complete rounds: each player gets the same number and
    // tier, and the route-length band grows equally. A cramped map stops a round
    // for everyone instead of silently favoring whichever start was scanned first.
    const int rounds=std::min(6,int(cells)*int(p.manaDensity)/(255*9000*p.players));
    for(int round=0;round<rounds;++round) {
        std::vector<Point> sites;
        for(size_t player=0;player<r.starts.size();++player) {
            Point best={-1,-1};int bestScore=1000000;
            const auto& d=homeDistances[player];
            const int target=64+round*24;
            for(int attempt=0;attempt<800;++attempt) {
                const int x=8+int(splitmix(featureRng)%uint64_t(w-16));
                const int z=8+int(splitmix(featureRng)%uint64_t(h-16));
                const size_t i=size_t(z)*w+x;const int distance=d[i];
                if(distance<target-12||distance>target+12||reserved[i]||!flat(x-6,z-6,14,14))continue;
                bool good=true;
                for(size_t other=0;other<homeDistances.size();++other)
                    if(other!=player&&homeDistances[other][i]>=0&&homeDistances[other][i]<distance+8)good=false;
                for(const auto* group:{&deposits,&sites})for(const auto pt:*group)
                    if((pt.first-x)*(pt.first-x)+(pt.second-z)*(pt.second-z)<32*32)good=false;
                if(!good)continue;
                const int score=std::abs(distance-target);
                if(score<bestScore){best={x,z};bestScore=score;}
            }
            if(best.first<0)break;
            sites.push_back(best);
        }
        if(sites.size()!=r.starts.size())break;
        // Reserve every route in this round before its ruins are placed.
        for(size_t player=0;player<sites.size();++player) {
            const auto site=sites[player];
            // Follow the existing footprint-valid distance field back home, and
            // reserve this approach before adding any trees or rocks.
            Point at=site;const auto& d=homeDistances[player];
            while(d[size_t(at.second)*w+at.first]>0) {
                mark(at.first,at.second,4,1);
                const int here=d[size_t(at.second)*w+at.first];
                for(const auto [dx,dz]:std::array<Point,4>{{{0,-1},{1,0},{0,1},{-1,0}}}) {
                    const int nx=at.first+dx,nz=at.second+dz;
                    if(nx>=0&&nz>=0&&nx<w&&nz<h&&d[size_t(nz)*w+nx]==here-1){at={nx,nz};break;}
                }
            }
            if (p.formatVer<4) deposit(site.first,site.second,1);
        }
        if (p.formatVer>=4) {
            for (const auto site : sites) deposit(site.first,site.second,1);
            // New ruins are real obstacles. Plan the next round against the
            // updated terrain, rather than routing through an earlier ruin.
            ground.markClearanceDirty();
            for (size_t player=0;player<r.starts.size();++player)
                homeDistances[player]=distances(ground,r.starts[player],6);
        }
    }
    // A separate noise field makes forest stands and rocky regions, with clear
    // paths through them. Dry, flat approaches around bases are never filled in.
    for(int z=1;z<h-4;++z)for(int x=1;x<w-4;++x) {
        const size_t i=size_t(z)*w+x;
        if(reserved[i]&1||claim[i]||!reachable[i])continue;
        const uint64_t roll=splitmix(featureRng);
        const int forest=int(noise(p.seed^0x715921,x,z,24)) + (p.layout==Jungle?60:0);
        const int rocks=int(noise(p.seed^0x359742,x,z,18));
        const char* name=nullptr;
        if(forest>125&&int(roll%10000)<int(p.treeDensity)*(forest-110)*4/160)
            name=palette.trees.p[(roll>>24)%palette.trees.n];
        else if(rocks>145&&int((roll>>12)%10000)<int(p.rockDensity)*(rocks-130)*3/160)
            name=palette.rocks.p[std::min((roll>>24)%palette.rocks.n,(roll>>34)%palette.rocks.n)];
        if(!name)continue;
        const auto* f=sizeOf(name);if(!f||!flat(x-1,z-1,f->x+2,f->z+2))continue;
        bool good=true;
        for(int dz=-1;dz<=f->z;++dz)for(int dx=-1;dx<=f->x;++dx)
            if(reserved[size_t(z+dz)*w+x+dx]&1)good=false;
        if(good)putFeature(x,z,name);
    }
    // Final footprint-aware validation sees scenery exactly as the simulation
    // will. Failure is explicit; an invalid map is never silently launched.
    ground.markClearanceDirty();
    for(size_t k=0;k<r.starts.size();++k) {
        const auto s=r.starts[k];
        if(!flat(s.first-12,s.second-12,24,24))throw std::runtime_error("Random map has an obstructed base");
        const auto d=distances(ground,s,6);
        for(size_t j=k*3;j<k*3+3;++j)
            if(d[size_t(deposits[j].second)*w+deposits[j].first]<0)
                throw std::runtime_error("Random map home mana is unreachable");
        for(size_t j=r.starts.size()*3+k;j<deposits.size();j+=r.starts.size())
            if(d[size_t(deposits[j].second)*w+deposits[j].first]<0)
                throw std::runtime_error("Random map expansion mana is unreachable");
        if(p.layout!=Islands)for(const auto target:r.starts)
            if(d[size_t(target.second)*w+target.first]<0)throw std::runtime_error("Random map scenery blocks an army route");
    }
    if(p.layout==Islands || p.layout==Ports) {
        sim::NavGrid::Limits shipLimits;shipLimits.minWaterDepth=15;
        sim::NavGrid ships(m.heights,w,h,m.seaLevel,shipLimits);ships.setObstacles(&obstacles);
        std::vector<int> waterDistances;
        for(const auto s:r.starts) {
            Point best={-1,-1};int bestDistance=w*w+h*h;
            for(int z=16;z<h-16;++z)for(int x=16;x<w-16;++x) {
                const int d=(x-s.first)*(x-s.first)+(z-s.second)*(z-s.second);
                if(d>=bestDistance||!ships.fits(x,z,15))continue;
                // Sea Fort's yard is 6x18, and output ships need clear water
                // beyond it. The NavGrid clearance cache saturates at 15, so use
                // its exact cell classifier for this larger construction area.
                bool open=true;
                for(int dz=-24;open&&dz<24;++dz)for(int dx=-12;dx<12;++dx)
                    if(!ships.walkable(x+dx,z+dz)){open=false;break;}
                if(!open)continue;
                if(!waterDistances.empty()&&waterDistances[size_t(z)*w+x]<0)continue;
                best={x,z};bestDistance=d;
            }
            if(best.first<0||bestDistance>(p.layout==Ports?std::max(w,h)*std::max(w,h):(islandRadius+96)*(islandRadius+96)))throw std::runtime_error("Random island has no usable connected harbor: start="+std::to_string(s.first)+","+std::to_string(s.second)+" best="+std::to_string(best.first)+","+std::to_string(best.second)+" d2="+std::to_string(bestDistance));
            if(waterDistances.empty())waterDistances=distances(ships,best,8);
            r.harbors.push_back(best);
        }
    }
    const auto water=std::count_if(m.heights.begin(),m.heights.end(),[&](int height){return height<m.seaLevel;});
    r.waterPercent=int(water*100/cells);
    return r;
}
} // namespace tak::mapgen
