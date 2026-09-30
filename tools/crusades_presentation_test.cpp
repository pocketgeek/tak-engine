#include "client/crusadesmap.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace c=tak::crusadesmap;
namespace {
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class Function>void rejects(Function action,const char* why){++checks;try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
c::Bytes bytes(const std::string& text){return {text.begin(),text.end()};}
std::string parcel(int id,const std::string& name,int x,int y){return "{[DarienMap<S:command=parcel><S:name="+name+"><I:chatareaid="+std::to_string(id)+"><S:description=Two\nlines "+std::string(1,char(0xe9))+"><I:xfireanchor="+std::to_string(x)+"><I:yfireanchor="+std::to_string(y)+"><I:xtextanchor=1><I:ytextanchor=2><S:nativerace=Neutral><I:nativeicon=0><S:terrain=Forest>]}";}
std::string definition(){return "{[DarienMap<S:command=world_attribs><S:name=Synthetic><I:height=8><I:width=5><I:parcels=3><I:borders=7>]}"+parcel(1,"Left",1,1)+parcel(2,"Right",4,1)+parcel(3,"Missing",99,99);}
void be(c::Bytes& out,uint32_t value){for(int n=3;n>=0;--n)out.push_back(uint8_t(value>>(8*n)));}
void chunk(c::Bytes& out,const char* type,const c::Bytes& body){be(out,uint32_t(body.size()));const auto begin=out.size();out.insert(out.end(),type,type+4);out.insert(out.end(),body.begin(),body.end());be(out,uint32_t(crc32(0,out.data()+begin,uInt(body.size()+4))));}
c::Bytes png(unsigned w,unsigned h,uint8_t depth,uint8_t color,const c::Bytes& scan,const c::Bytes& palette={},const c::Bytes& alpha={},uint8_t interlace=0){
    c::Bytes out{137,80,78,71,13,10,26,10},header;be(header,w);be(header,h);header.insert(header.end(),{depth,color,0,0,interlace});chunk(out,"IHDR",header);
    if(!palette.empty())chunk(out,"PLTE",palette);
    if(!alpha.empty())chunk(out,"tRNS",alpha);
    uLongf size=compressBound(scan.size());c::Bytes compressed(size);check(compress2(compressed.data(),&size,scan.data(),scan.size(),Z_BEST_SPEED)==Z_OK,"fixture compression");compressed.resize(size);
    chunk(out,"IDAT",compressed);chunk(out,"IEND",{});return out;
}
void parser(){
    const auto d=c::parseDefinition(bytes(definition()));
    check(d.parcels.size()==3 && d.declaredWidth==5 && d.declaredHeight==8,"header labels preserved without axis correction");
    check(d.parcels.at(1).description=="Two\nlines \xc3\xa9","multiline CP1252 text converted to UTF8");
    check(d.parcels.at(2).fireX==4 && d.parcels.at(2).textY==2,"anchors remain distinct fields");
    check(d.parcels.at(1).nativeFaction=="Neutral","native faction remains descriptive");
    auto invalid=definition();invalid.replace(invalid.find("chatareaid=2"),12,"chatareaid=1");
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"duplicate IDs accepted");
    invalid=definition();invalid.replace(invalid.find("name=Right"),10,"name=Left");
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"duplicate names accepted");
    invalid=definition();invalid.replace(invalid.find("parcels=3"),9,"parcels=2");
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"extra parcel accepted");
    invalid=definition();invalid.replace(invalid.find("I:width=5"),9,"S:width=5");
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"wrong field type accepted");
    invalid=definition();invalid.replace(invalid.find("chatareaid=1"),12,"chatareaid=0");
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"zero parcel ID accepted");
    invalid=definition();invalid.replace(invalid.find("borders=7"),9,"borders=-1");
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"negative border count accepted");
    invalid=definition();invalid.insert(invalid.find("]}"),"<I:width=5>");
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"duplicate field accepted");
    invalid=definition();invalid.insert(invalid.find("Two"),1,char(0x81));
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"undefined CP1252 accepted");
    invalid=definition();invalid.insert(invalid.find("Two"),1,'\0');
    rejects([&]{(void)c::parseDefinition(bytes(invalid));},"NUL accepted");
    for(size_t size:{size_t(0),size_t(1),definition().size()-1})rejects([&]{(void)c::parseDefinition(bytes(definition().substr(0,size)));},"truncated definition accepted");
    rejects([]{(void)c::parseDefinition(c::Bytes(8*1024*1024+1,' '));},"oversized definition accepted");
}
int predictor(int a,int b,int c){const int p=a+b-c;const std::array<int,3> distances{std::abs(p-a),std::abs(p-b),std::abs(p-c)};const auto n=std::min_element(distances.begin(),distances.end())-distances.begin();return n==0?a:n==1?b:c;}
void decoder(){
    c::Bytes expected(3*5*4);for(size_t i=0;i<expected.size();++i)expected[i]=uint8_t(i*17+5);
    c::Bytes scan;
    for(size_t y=0;y<5;++y){scan.push_back(uint8_t(y));for(size_t x=0;x<12;++x){const int a=x>=4?expected[y*12+x-4]:0,b=y?expected[(y-1)*12+x]:0,c=y&&x>=4?expected[(y-1)*12+x-4]:0;const int pred=y==0?0:y==1?a:y==2?b:y==3?(a+b)/2:predictor(a,b,c);scan.push_back(uint8_t(expected[y*12+x]-pred));}}
    const auto rgba=png(3,5,8,6,scan);const auto decoded=c::decodePng(rgba);check(decoded.rgba==expected && decoded.width==3 && decoded.height==5,"all five PNG filters reconstruct RGBA bytes");
    for(uint8_t depth:{1,2,4,8}){
        const unsigned count=depth==1?2:4;c::Bytes palette;for(unsigned i=0;i<count;++i)palette.insert(palette.end(),{uint8_t(i*50),uint8_t(200-i),uint8_t(i+10)});
        c::Bytes raw,expectedPalette;for(unsigned y=0;y<2;++y){raw.push_back(0);c::Bytes row((7*depth+7)/8,0);for(unsigned x=0;x<7;++x){const auto index=(x+y)%count;const unsigned bit=x*depth;row[bit/8]|=uint8_t(index<<(8-depth-bit%8));expectedPalette.insert(expectedPalette.end(),{uint8_t(index*50),uint8_t(200-index),uint8_t(index+10),uint8_t(index==1?127:255)});}raw.insert(raw.end(),row.begin(),row.end());}
        check(c::decodePng(png(7,2,depth,3,raw,palette,{255,127})).rgba==expectedPalette,"indexed bit depth and transparency decoded");
    }
    check(c::decodePng(png(1,1,8,2,{0,1,2,3},{},{0,1,0,2,0,3})).rgba==c::Bytes({1,2,3,0}),"RGB transparency key");
    check(c::decodePng(png(1,1,8,0,{0,70},{},{0,70})).rgba==c::Bytes({70,70,70,0}),"grayscale transparency key");
    check(c::decodePng(png(1,1,8,4,{0,70,90})).rgba==c::Bytes({70,70,70,90}),"grayscale alpha");
    auto broken=rgba;broken[29]^=1;rejects([&]{(void)c::decodePng(broken);},"bad CRC accepted");
    broken=rgba;broken.push_back(0);rejects([&]{(void)c::decodePng(broken);},"trailing PNG bytes accepted");
    for(size_t size:{size_t(0),size_t(7),size_t(20),rgba.size()-1})rejects([&]{(void)c::decodePng(c::Bytes(rgba.begin(),rgba.begin()+size));},"truncated PNG accepted");
    rejects([&]{(void)c::decodePng(png(0,1,8,6,{}));},"zero width accepted");
    rejects([&]{(void)c::decodePng(png(4097,1,8,6,{}));},"oversized width accepted");
    rejects([&]{(void)c::decodePng(png(4096,4096,8,6,{}));},"oversized pixel allocation accepted");
    rejects([&]{(void)c::decodePng(png(1,1,8,6,{0,1,2,3,4},{},{},1));},"unsupported interlace accepted");
    rejects([&]{(void)c::decodePng(png(1,1,16,6,{}));},"unsupported depth accepted");
    rejects([&]{(void)c::decodePng(png(1,1,8,6,{5,1,2,3,4}));},"invalid filter accepted");
    rejects([&]{(void)c::decodePng(png(1,1,8,6,{0,1,2,3,4,5}));},"excess decompressed image accepted");
    rejects([&]{(void)c::decodePng(png(1,1,8,3,{0,1},{1,2,3}));},"out of range palette index accepted");
    rejects([&]{(void)c::decodePng(png(1,1,8,3,{0,0}));},"missing palette accepted");
    rejects([&]{(void)c::decodePng(png(1,1,8,2,{0,1,2,3},{},{1,1,0,2,0,3}));},"out of range transparency sample accepted");
}
c::Image solid(uint8_t r,uint8_t g,uint8_t b){c::Image image;image.width=8;image.height=5;for(int i=0;i<40;++i)image.rgba.insert(image.rgba.end(),{r,g,b,255});return image;}
void geometry(){
    auto borders=solid(0,0,0);const auto key=[&](int x,int y){const size_t at=(y*8+x)*4;borders.rgba[at]=255;borders.rgba[at+2]=255;};
    for(int y=1;y<=3;++y){for(int x=1;x<=2;++x)key(x,y);for(int x=4;x<=6;++x)key(x,y);}key(0,4);
    auto p=c::makePresentation(c::parseDefinition(bytes(definition())),borders,solid(200,0,0),solid(0,0,200),solid(0,200,0));
    check(p.width()==8 && p.height()==5,"geometry uses image axes not world header labels");
    check(p.unmappedParcelCount()==1 && p.parcel(3),"unmapped seed keeps parcel for list selection");
    check(p.regionAt(1,1)==1 && p.regionAt(2,3)==1 && p.regionAt(4,1)==2 && p.regionAt(6,3)==2,"connected seed fills map to stable IDs");
    check(!p.regionAt(0,4) && !p.regionAt(3,2) && !p.regionAt(-1,0) && !p.regionAt(8,0),"disconnected/border/out of bounds pixels unselectable");
    tak::net::crusades::Snapshot snapshot;
    for(const auto& [id,parcel]:p.definition().parcels){tak::net::crusades::Territory t;t.id=id;t.displayName=parcel.name;t.owner=id==1?tak::net::crusades::Owner::Honor:tak::net::crusades::Owner::Terror;snapshot.territories.push_back(t);}
    check(p.compatible(snapshot),"authoritative ID/name set matches geometry");
    auto image=p.compose(snapshot);check(image.rgba[(1*8+1)*4]==200 && image.rgba[(1*8+4)*4+2]==200,"authoritative ownership selects side artwork");
    check(image.rgba[(4*8)*4]==48,"unmapped component neutral not falsely assigned");
    snapshot.territories[0].owner=tak::net::crusades::Owner::Contested;snapshot.territories[1].owner.reset();image=p.compose(snapshot);
    check(image.rgba[(1*8+1)*4+1]==200 && image.rgba[(1*8+4)*4]==48,"contested artwork differs from unknown neutral");
    auto mismatch=snapshot;mismatch.territories[0].displayName="Wrong";check(!p.compatible(mismatch),"same ID with wrong name falls back");
    rejects([&]{(void)p.compose(mismatch);},"incompatible composition accepted");
    mismatch=snapshot;mismatch.territories[0].id=999;check(!p.compatible(mismatch),"unknown authoritative ID falls back");
    mismatch=snapshot;mismatch.territories.pop_back();check(!p.compatible(mismatch),"partial territory set cannot impersonate full map");
    mismatch=snapshot;mismatch.territories[1]=mismatch.territories[0];check(!p.compatible(mismatch),"duplicate authoritative ID rejected");
    auto wrong=solid(1,2,3);wrong.height=4;wrong.rgba.resize(8*4*4);
    rejects([&]{(void)c::makePresentation(c::parseDefinition(bytes(definition())),borders,wrong,solid(0,0,0),solid(0,0,0));},"mismatched art dimensions accepted");
}
}
int main(){try{parser();decoder();geometry();std::cout<<"PASS: "<<checks<<" Crusades presentation checks\n";}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
