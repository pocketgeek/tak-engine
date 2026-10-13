#include "client/mapview.h"
#include "client/gpuvram.h"
#include "hpi/hpi.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) try {
    if(argc!=2 && argc!=3)return 2;
    SDL_SetMainReady();
    SDL_SetHint("SDL_SHUTDOWN_DBUS_ON_QUIT","1"); // release SDL's process-global DBus allocations for leak checks
    check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
    auto* window=SDL_CreateWindow("terrain chunks",0,0,640,480,SDL_WINDOW_HIDDEN);
    auto* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE);
    if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    check(renderer,SDL_GetError());
    const auto baseline=gpuvram::bytes();
    {
        auto vfs=tak::hpi::mountRetailRoot(argv[1]);
        MapView map(renderer,vfs,tak::hpi::findMap(vfs,argc==3 ? argv[2] : "Ulasem Arena"));map.finishChunks();
        auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,1280,960);
        check(target,SDL_GetError());SDL_SetRenderTarget(renderer,target);
        auto draw=[&] {
            SDL_SetRenderDrawColor(renderer,70,80,90,255);SDL_RenderClear(renderer);
            map.draw(610,450);
            check(SDL_GetRenderTarget(renderer)==target,"render target unchanged");
        };
        auto warm=[&] {
            const auto start=SDL_GetTicks();
            do {
                draw();const auto stats=map.chunkStats();
                if(stats.visible && stats.visible==stats.ready)return;
                check(SDL_GetTicks()-start<60000,"terrain chunks failed to finish");SDL_Delay(1);
            }while(true);
        };
        auto read=[&] {std::vector<unsigned char> pixels(1280*960*4);
            check(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),1280*4)==0,SDL_GetError());return pixels;};
        for(float scale:{1.f,1.5f,2.f}) {
            SDL_RenderSetScale(renderer,scale,scale);
            SDL_Rect viewport{4,6,620,460},clip{2,3,601,441};
            SDL_RenderSetViewport(renderer,&viewport);SDL_RenderSetClipRect(renderer,&clip);
            map.setZoom(.05f);map.setOffset(300,300);warm();
            const auto first=read();const auto wideUploads=map.chunkStats().uploads;
            for(int frame=0;frame<6;++frame) {map.setOffset(300+frame,300+frame);draw();}
            check(map.chunkStats().uploads==wideUploads,"panning reuses map-space images");
            map.setOffset(300,300);draw();check(read()==first,"returning camera restores image");
            map.setZoom(1);warm();const auto bothUploads=map.chunkStats().uploads;
            map.setZoom(.05f);draw();check(map.chunkStats().uploads==bothUploads,"returning to a cached resolution does not rebuild");
            map.setBilinear(true);draw();check(map.chunkStats().uploads==bothUploads,"filter changes reuse chunk pixels");
            map.setBilinear(false);draw();check(read()==first,"filter reset restores nearest image");
            SDL_Rect actual{};SDL_RenderGetViewport(renderer,&actual);
            check(actual.x==viewport.x && actual.y==viewport.y && actual.w==viewport.w && actual.h==viewport.h,"viewport unchanged");
            SDL_RenderGetClipRect(renderer,&actual);
            check(SDL_RenderIsClipEnabled(renderer) && actual.x==clip.x && actual.w==clip.w && actual.y==clip.y && actual.h==clip.h,"clip unchanged");
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);check(sx==scale && sy==scale,"AA scale unchanged");
            map.invalidateRenderTargets();check(map.chunkStats().bytes==0,"reset frees images");warm();check(read()==first,"reset recreates same pixels");
        }
        // A local brush edit must retain unrelated queued/in-flight snapshots,
        // even before their textures have reached the render thread.
        SDL_RenderSetScale(renderer,1,1);map.setZoom(1);map.setOffset(800,800);
        map.invalidateRenderTargets();draw();
        const auto pending=map.chunkStats().pending;
        check(pending>=4,"fixture queued visible chunks");
        map.tilesEdited(100,100,1,1);
        check(map.chunkStats().pending==pending,"offscreen stamp must retain pending visible chunks");
        map.tilesEdited(25,25,1,1);
        check(map.chunkStats().pending==pending-1,"interior stamp cancels only its pending chunk");
        map.tilesEdited(25,25,1,1);
        check(map.chunkStats().pending==pending-1,"repeated edits coalesce before the next draw");
        warm();check(map.chunkStats().ready==map.chunkStats().visible,"retained and replacement snapshots finish together");
        // Full-detail texels, including both sides of a chunk boundary, match
        // the independent CPU terrain compositor. This catches crop/gutter seams.
        SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,nullptr);
        SDL_Rect terrainClip{0,0,610,450};SDL_RenderSetClipRect(renderer,&terrainClip);
        map.setZoom(1);map.setOffset(800,800);warm();const auto full=read();
        for(int y:{31,32,223,224,225,440})for(int x:{31,32,223,224,225,600}) {
            const int wx=800+x,wy=800+y;
            std::vector<uint8_t> block(32*32*4);
            map.compositor().renderBlock(map.map(),wx/32,wy/32,block,32,0,0);
            const size_t from=(size_t(wy%32)*32+wx%32)*4,to=(size_t(y)*1280+x)*4;
            for(int c=0;c<4;++c)check(full[to+c]==block[from+c],"full-resolution terrain texel or chunk seam mismatch");
        }
        auto& edited=map.editMap();
        map.invalidateRenderTargets();warm();
        const auto resident=map.chunkStats().images;
        check(resident>=4,"fixture spans four terrain chunks");
        edited.tileCols[size_t(25)*edited.blocksX+25]^=1;
        map.tilesEdited(25,25,1,1);
        check(map.chunkStats().images==resident-1,"interior stamp retains unaffected terrain images");
        warm();const auto partial=read();
        map.invalidateRenderTargets();warm();
        check(read()==partial,"partial terrain rebuild matches full rebuild");
        edited.tileCols[size_t(32)*edited.blocksX+32]^=1;
        map.tilesEdited(32,32,1,1);
        check(map.chunkStats().images==0,"corner edit invalidates neighbouring filtering gutters");
        draw(); // allow a worker snapshot to start, then change the same tile
        edited.tileCols[size_t(32)*edited.blocksX+32]^=3;
        map.tilesEdited(32,32,1,1);warm();const auto raced=read();
        map.invalidateRenderTargets();warm();
        check(read()==raced,"old in-flight tile snapshots cannot overwrite new edits");
        const auto before=read();
        for(auto& col:edited.tileCols)col^=1;
        map.tilesEdited();check(map.chunkStats().images==0,"terrain edits invalidate images");warm();
        check(read()!=before,"edited terrain appears in rebuilt chunks");
        check(map.chunkStats().bytes<=(size_t(256)<<20),"chunk memory budget");
        // Reload while chunk composition may still be reading the old source.
        // The section decoder and chunk worker must both relinquish its images.
        for(int i=0;i<4;++i) {
            map.invalidateRenderTargets();map.setZoom(.1f);draw();
            map.reload(vfs,tak::hpi::findMap(vfs,i%2 ? "Ulasem Arena" : "Cairbray Coast Landing"));
            check(map.chunkStats().bytes==0,"reload frees old chunk images");
        }
        warm();
        // Fallback sections upload on demand (this view never calls finishChunks, which
        // pins them): a frame whose chunks are all still baking is drawn completely from
        // sections the same frame, and once chunks cover the view the duplicate sections
        // are released. Magenta clear = a hole (no underlay is set here).
        {
            MapView fresh(renderer,vfs,tak::hpi::findMap(vfs,argc==3 ? argv[2] : "Ulasem Arena"));
            SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetClipRect(renderer,&terrainClip);
            const auto start=SDL_GetTicks();
            while(!fresh.sectionsDecoded()) {fresh.ensureChunks(610,450);check(SDL_GetTicks()-start<60000,"sections failed to decode");SDL_Delay(1);}
            fresh.ensureChunks(610,450);
            check(fresh.sectionBytes()==0,"no section is uploaded before the fallback needs it");
            auto holes=[&] {
                const auto px=read();size_t n=0;
                for(int y=0;y<450;++y)for(int x=0;x<610;++x) {
                    const auto* p=&px[(size_t(y)*1280+x)*4];n+=p[0]==255 && p[1]==0 && p[2]==255;
                }
                return n;
            };
            auto frame=[&](float x,float y) {
                fresh.setZoom(1);fresh.setOffset(x,y);
                SDL_SetRenderDrawColor(renderer,255,0,255,255);SDL_RenderClear(renderer);fresh.draw(610,450);
            };
            frame(2000,2000);
            check(fresh.chunkStats().ready==0,"fixture: the first frame has no chunk images");
            check(holes()==0,"chunk-less first frame is drawn completely from sections");
            check(fresh.sectionBytes()>0,"the fallback uploaded its sections");
            const auto fallback=read();
            const auto settle=SDL_GetTicks();
            do {frame(2000,2000);check(SDL_GetTicks()-settle<60000,"chunks failed to finish");SDL_Delay(1);}
            while(fresh.chunkStats().ready!=fresh.chunkStats().visible);
            check(holes()==0,"chunked frame has no holes");
            // SDL's software rasteriser samples geometry slightly differently from
            // copies, so the texel-exact comparison is a GL-only check.
            SDL_RendererInfo info{};SDL_GetRendererInfo(renderer,&info);
            if(info.name && !std::strcmp(info.name,"opengl"))
                check(read()==fallback,"section fallback matches the chunk image texel for texel");
            const auto idle=SDL_GetTicks();
            while(fresh.sectionBytes() && SDL_GetTicks()-idle<10000) {frame(2000,2000);SDL_Delay(5);}
            check(fresh.sectionBytes()==0,"sections are released once chunks cover the view");
            // A jump to an unbaked spot: the fallback re-uploads in the same frame.
            frame(9000,9000);
            check(holes()==0,"a jump draws completely from re-uploaded sections");
            // finishChunks (screenshots, the campaign briefing) lifts the limits for ONE
            // draw only: afterwards the sections are budgeted and released as usual.
            fresh.finishChunks();frame(6000,6000);
            check(holes()==0,"the finishChunks frame is complete");
            {
                const auto t=SDL_GetTicks();
                do {frame(6000,6000);check(SDL_GetTicks()-t<60000,"chunks failed to finish");SDL_Delay(1);}
                while(fresh.chunkStats().ready!=fresh.chunkStats().visible);
                const auto idle2=SDL_GetTicks();
                while(fresh.sectionBytes() && SDL_GetTicks()-idle2<10000) {frame(6000,6000);SDL_Delay(5);}
                check(fresh.sectionBytes()==0,"finishChunks does not pin sections for the session");
            }
            // Lowering the limit below what is held sheds off-screen chunks next frame.
            for(float x:{2000.f,4000.f,6000.f,8000.f}) {
                const auto t=SDL_GetTicks();
                do {frame(x,2000);check(SDL_GetTicks()-t<60000,"chunks failed to finish");SDL_Delay(1);}
                while(fresh.chunkStats().ready!=fresh.chunkStats().visible);
            }
            const auto held=fresh.chunkStats().bytes;
            const size_t pretend=size_t(600)<<20;   // stand-in for everything else resident
            gpuvram::addBuffer(pretend);gpuvram::setLimit(pretend+(size_t(32)<<20));
            frame(8000,2000);
            check(fresh.chunkStats().bytes<held,"a lowered limit sheds off-screen chunks");
            check(fresh.chunkStats().ready==fresh.chunkStats().visible,"...but keeps every visible one");
            check(holes()==0,"...and leaves no hole");
            gpuvram::setLimit(gpuvram::kAutoLimit);gpuvram::removeBuffer(pretend);
        }
        SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(target);
    }
    check(gpuvram::bytes()==baseline,"terrain teardown frees textures");
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS: terrain chunk pan/zoom reuse, filtering, AA, edits, reset and cleanup");return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
