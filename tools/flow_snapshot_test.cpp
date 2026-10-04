#include "sim/flowsnapshot.h"
#include <cstdio>
#include <set>
#include <stdexcept>
#include <thread>
using namespace tak::sim::flow;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void equal(const Topology& a,const Topology& b) {
    check(a.width==b.width&&a.height==b.height&&a.components==b.components&&a.offsets==b.offsets,
        "prepared topology dimensions/components changed");
    check(a.edges.size()==b.edges.size()&&a.edgeOffsets==b.edgeOffsets,"prepared topology edges changed");
    for(size_t i=0;i<a.tiles.size();++i) {
        const auto& x=*a.tiles[i];const auto& y=*b.tiles[i];
        check(x.cost==y.cost&&x.component==y.component&&x.minimumCost==y.minimumCost&&x.componentCount==y.componentCount,
            "prepared tile differs from serial footprint/label baseline");
    }
    for(size_t i=0;i<a.edges.size();++i) {
        const auto& x=a.edges[i];const auto& y=b.edges[i];
        check(x.from==y.from&&x.to==y.to&&x.inside==y.inside&&x.outside==y.outside,"prepared edge order changed");
    }
}
}
int main() {
    try {
        const auto mainThread=std::this_thread::get_id();
        for(auto [fx,fz]:std::array<std::pair<int,int>,5>{{{1,1},{2,2},{3,5},{8,2},{64,64}}}) {
            constexpr int w=133,h=75;
            std::vector<uint16_t> raw(w*h,64);
            for(int z=9;z<70;++z)raw[size_t(z)*w+63]=0; // gate/obstacle on adjacent tile halo
            raw[64*w+64]=5;raw[3*w+100]=0;
            const auto sampler=[&](int x,int z) {
                check(std::this_thread::get_id()==mainThread,"sampler escaped World thread");
                return raw[size_t(z)*w+x];
            };
            TopologyBuilder reference(w,h);
            for(int z=0;z<h;++z)for(int x=0;x<w;++x) {
                bool fits=true;
                for(int dz=0;dz<fz&&fits;++dz)for(int dx=0;dx<fx;++dx) {
                    const int nx=x-fx/2+dx,nz=z-fz/2+dz;
                    if(nx<0||nz<0||nx>=w||nz>=h||!raw[size_t(nz)*w+nx]){fits=false;break;}
                }
                reference.setCost({x,z},fits?raw[size_t(z)*w+x]:0);
            }
            while(!reference.done()&&!reference.failed())reference.step(37);
            check(reference.done(),"reference topology failed");
            std::array<std::shared_ptr<const Topology>,2> results;
            std::array<std::vector<size_t>,2> work;
            std::vector<std::shared_ptr<const Tile>> cached(reference.finish()->tiles.size());
            for(int mode=0;mode<2;++mode) {
                SnapshotBuilder::Hooks hooks;
                hooks.uniform=[&](SnapshotBuilder::Rectangle r)->std::optional<uint16_t> {
                    check(std::this_thread::get_id()==mainThread,"uniform callback escaped World thread");
                    check(r.x>=0&&r.z>=0&&r.x+r.w<=w&&r.z+r.h<=h,"uniform halo not clipped");
                    for(int z=r.z;z<r.z+r.h;++z)for(int x=r.x;x<r.x+r.w;++x)
                        if(raw[size_t(z)*w+x]!=64)return std::nullopt;
                    return 64;
                };
                size_t published=0;
                hooks.publish=[&](size_t index,std::shared_ptr<const Tile> tile) {
                    check(std::this_thread::get_id()==mainThread,"publication escaped World thread");
                    check(index==published++,"tile publication order depends on worker completion");
                    cached[index]=std::move(tile);
                };
                SnapshotBuilder snapshot(w,h,fx,fz,sampler,{}, {},{},hooks,mode?4:0);
                size_t tick=0;
                while(!snapshot.done()&&!snapshot.failed()) {
                    const size_t budget=1+(tick++%137);
                    const auto used=snapshot.step(budget);
                    check(used<=budget,"prepared snapshot exceeded work quota");work[size_t(mode)].push_back(used);
                }
                check(snapshot.done()&&published==cached.size(),"prepared publication incomplete");
                results[size_t(mode)]=snapshot.finish();equal(*reference.finish(),*results[size_t(mode)]);
            }
            check(work[0]==work[1],"worker count changed logical snapshot progress");
            SnapshotBuilder::Hooks import;
            import.prepared=[&](size_t index){return cached[index];};
            SnapshotBuilder restored(w,h,fx,fz,[](int,int)->uint16_t{throw std::runtime_error("cached tile resampled");},{},{},{},import,4);
            while(!restored.done()&&!restored.failed())restored.step(37);
            check(restored.done()&&restored.samples()==0,"prepared cache did not survive fresh builder");
            equal(*results[0],*restored.finish());
            for(size_t i=0;i<cached.size();++i)check(restored.finish()->tiles[i]==cached[i],"cached immutable tile copied");
        }
        // Large deterministic quanta actually dispatch two worker jobs; small
        // budgets above deliberately stay inline to avoid futures overhead.
        for(size_t quantum:{size_t(8192),size_t(16384),size_t(65536)}) {
            auto sample=[](int x,int z){return uint16_t(x%31==0&&z%29?0:64);};
            SnapshotBuilder serial(256,256,3,5,sample),parallel(256,256,3,5,sample,{},{},{},{},4);
            while(!serial.done()&&!serial.failed()) {
                check(serial.step(quantum)==parallel.step(quantum),"parallel batch changed work quota");
                check(serial.checksum()==parallel.checksum(),"parallel batch changed intermediate snapshot state");
            }
            check(serial.done()&&parallel.done(),"parallel preparation failed");
            equal(*serial.finish(),*parallel.finish());
        }
        {
            SnapshotBuilder a(128,128,1,1,[](int,int){return uint16_t(1);});
            SnapshotBuilder b(128,128,1,1,[](int,int){return uint16_t(2);});
            a.step(16);b.step(16);
            check(a.checksum()!=b.checksum(),"intermediate sampling fingerprint omitted raw costs");
        }
        // Many completely unknown tiles need only one immutable uniform body;
        // boundary footprints still block off-map space. No sampling is needed.
        for(uint16_t cost:{uint16_t(0),uint16_t(64)}) {
            SnapshotBuilder::Hooks hooks;
            hooks.uniform=[cost](SnapshotBuilder::Rectangle){return std::optional<uint16_t>(cost);};
            SnapshotBuilder snapshot(512,512,3,5,[](int,int)->uint16_t{throw std::runtime_error("uniform tile sampled");},{},{},{},hooks,4);
            size_t work=0;
            while(!snapshot.done()&&!snapshot.failed())work+=snapshot.step(2048);
            check(snapshot.done()&&snapshot.samples()==0,"uniform snapshot did not finish without sampling");
            const auto t=snapshot.finish();
            SnapshotBuilder unchanged(512,512,3,5,[](int,int)->uint16_t{throw std::runtime_error("unchanged uniform sampled");},t,{}, {},hooks,4);
            while(!unchanged.done()&&!unchanged.failed())unchanged.step(2048);
            check(unchanged.finish()==t,"unchanged uniform costs replaced snapshot identity");
            std::set<const Tile*> interior;
            for(int z=1;z<7;++z)for(int x=1;x<7;++x)interior.insert(t->tiles[size_t(z)*8+x].get());
            check(interior.size()<=2,"uniform interior tiles were rebuilt repeatedly");
            check(t->cost({0,0})==0&&t->cost({256,256})==cost,"uniform footprint boundary changed");
            std::printf("uniform snapshot cost=%u work=%zu interior allocations=%zu\n",cost,work,interior.size());
        }
        std::puts("PASS sealed snapshot preparation, uniform halos, immutable cache, deterministic workers");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"flow snapshot: %s\n",e.what());return 1;}
}
