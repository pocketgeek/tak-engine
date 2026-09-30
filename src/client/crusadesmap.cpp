#include "client/crusadesmap.h"
#include "hpi/hpi.h"
#include <zlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace tak::crusadesmap {
namespace {
constexpr size_t kMaxAsset = 16 * 1024 * 1024;
constexpr size_t kMaxDefinition = 8 * 1024 * 1024;
constexpr uint32_t kMaxDimension = 4096;
constexpr uint64_t kMaxPixels = 4 * 1024 * 1024;
[[noreturn]] void fail(const char* why) { throw std::runtime_error(why); }
void require(bool condition, const char* why) { if (!condition) fail(why); }
void appendUtf8(std::string& out, uint32_t point) {
    if (point < 0x80) out += char(point);
    else if (point < 0x800) { out += char(0xc0 | (point >> 6)); out += char(0x80 | (point & 63)); }
    else { out += char(0xe0 | (point >> 12)); out += char(0x80 | ((point >> 6) & 63)); out += char(0x80 | (point & 63)); }
}
std::string cp1252(std::string_view input) {
    constexpr uint16_t special[]{0x20ac,0,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
        0x2c6,0x2030,0x160,0x2039,0x152,0,0x17d,0,0,0x2018,0x2019,0x201c,0x201d,0x2022,
        0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0,0x17e,0x178};
    std::string result;
    for (const unsigned char byte : input) {
        require(byte != 0 && byte != 0x7f && (byte >= 0x20 || byte == '\n' || byte == '\r' || byte == '\t'), "invalid control byte in Darien definition");
        uint32_t point = byte;
        if (byte >= 0x80 && byte < 0xa0) { point = special[byte - 0x80]; require(point != 0, "undefined CP1252 byte in Darien definition"); }
        appendUtf8(result, point);
    }
    return result;
}
struct Field { char kind; std::string value; };
using Fields = std::map<std::string, Field>;
struct DefinitionReader {
    const Bytes& input;
    size_t offset = 0;
    bool spaceByte(uint8_t c) const { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
    void space() { while (offset < input.size() && spaceByte(input[offset])) ++offset; }
    bool at(std::string_view token) const {
        return token.size() <= input.size() - offset && std::equal(token.begin(), token.end(), input.begin()+offset);
    }
    void consume(std::string_view token) { require(at(token), "malformed Darien definition tag"); offset += token.size(); }
    Fields record() {
        space(); consume("{[DarienMap"); Fields fields;
        while (true) {
            space(); if (at("]}")) { offset += 2; return fields; }
            consume("<"); require(offset < input.size(), "truncated Darien field");
            const char kind = char(input[offset++]); require(kind == 'S' || kind == 'I', "unknown Darien field type"); consume(":");
            const size_t start = offset;
            while (offset < input.size() && input[offset] != '=') {
                const auto c = input[offset++]; require((c >= 'a' && c <= 'z') || c == '_', "invalid Darien field name");
                require(offset-start <= 64, "oversized Darien field name");
            }
            require(offset > start, "empty Darien field name");
            const std::string name(input.begin()+start,input.begin()+offset); consume("=");
            const size_t valueStart = offset;
            while (offset < input.size() && input[offset] != '>') { require(input[offset] != '<', "nested Darien field tag"); ++offset; }
            require(offset-valueStart <= 65536, "oversized Darien field");
            const std::string_view raw(reinterpret_cast<const char*>(input.data()+valueStart),offset-valueStart); consume(">");
            require(fields.emplace(name,Field{kind,cp1252(raw)}).second, "duplicate Darien field");
            require(fields.size() <= 12, "too many Darien fields");
        }
    }
};
const std::string& text(const Fields& fields, const char* name) {
    const auto found = fields.find(name); require(found != fields.end() && found->second.kind == 'S', "missing/wrong-type Darien text field");
    return found->second.value;
}
int32_t integer(const Fields& fields, const char* name) {
    const auto found = fields.find(name); require(found != fields.end() && found->second.kind == 'I', "missing/wrong-type Darien integer field");
    const auto& value = found->second.value; int32_t result = 0;
    require(!value.empty() && value.front() != '+', "invalid Darien integer");
    const auto parsed = std::from_chars(value.data(),value.data()+value.size(),result);
    require(parsed.ec == std::errc{} && parsed.ptr == value.data()+value.size(), "invalid Darien integer");
    return result;
}
uint32_t be32(const Bytes& input, size_t at) {
    require(at <= input.size() && input.size()-at >= 4, "truncated PNG integer");
    return (uint32_t(input[at])<<24)|(uint32_t(input[at+1])<<16)|(uint32_t(input[at+2])<<8)|input[at+3];
}
void imageValid(const Image& image) {
    require(image.width && image.height && image.width <= kMaxDimension && image.height <= kMaxDimension &&
        uint64_t(image.width)*image.height <= kMaxPixels && image.rgba.size() == uint64_t(image.width)*image.height*4,
        "invalid strategic image dimensions/storage");
}
bool magenta(const Image& image, size_t pixel) {
    const auto* p = &image.rgba[pixel*4];
    // Retail's working image key 0xfc1f is RGB555 magenta with bit15 set.
    // Red/blue quantize to31, green to0; alpha is not ownership information.
    return (p[0] >> 3) == 31 && (p[1] >> 3) == 0 && (p[2] >> 3) == 31;
}
int paeth(int a, int b, int c) {
    const int p=a+b-c, pa=std::abs(p-a), pb=std::abs(p-b), pc=std::abs(p-c);
    return pa<=pb && pa<=pc ? a : pb<=pc ? b : c;
}
} // namespace

Definition parseDefinition(const Bytes& bytes) {
    require(!bytes.empty() && bytes.size() <= kMaxDefinition, "Darien definition size outside limit");
    DefinitionReader reader{bytes}; const auto world=reader.record();
    require(world.size()==6 && text(world,"command")=="world_attribs", "expected Darien world header");
    Definition definition; definition.name=text(world,"name");
    const auto width=integer(world,"width"), height=integer(world,"height"), count=integer(world,"parcels"), borders=integer(world,"borders");
    require(width>0 && height>0 && count>0 && count<=1024 && borders>=0 && !definition.name.empty(), "invalid Darien world attributes");
    definition.declaredWidth=uint32_t(width); definition.declaredHeight=uint32_t(height); definition.declaredBorders=uint32_t(borders);
    std::set<std::string> names;
    for (int n=0;n<count;++n) {
        const auto fields=reader.record(); require(fields.size()==11 && text(fields,"command")=="parcel", "expected Darien parcel");
        Parcel p; const auto id=integer(fields,"chatareaid"); require(id>0,"invalid Darien parcel ID"); p.id=uint32_t(id);
        p.name=text(fields,"name"); p.description=text(fields,"description"); p.nativeFaction=text(fields,"nativerace"); p.terrain=text(fields,"terrain");
        p.fireX=integer(fields,"xfireanchor"); p.fireY=integer(fields,"yfireanchor"); p.textX=integer(fields,"xtextanchor"); p.textY=integer(fields,"ytextanchor"); p.nativeIcon=integer(fields,"nativeicon");
        require(!p.name.empty() && p.name.size()<=4096 && !p.nativeFaction.empty() && !p.terrain.empty(), "invalid Darien parcel strings");
        require(names.insert(p.name).second && definition.parcels.emplace(p.id,std::move(p)).second, "duplicate Darien parcel ID/name");
    }
    reader.space(); require(reader.offset==bytes.size(),"unexpected data after Darien parcels");
    return definition;
}

Image decodePng(const Bytes& bytes) {
    static constexpr uint8_t signature[]{137,80,78,71,13,10,26,10};
    require(bytes.size()>=8 && bytes.size()<=kMaxAsset && std::equal(std::begin(signature),std::end(signature),bytes.begin()),"invalid PNG signature/size");
    Image image; Bytes compressed,palette,transparency;
    bool header=false, data=false, dataEnded=false, ended=false, seenPalette=false, seenTransparency=false;
    uint8_t depth=0, color=0; size_t offset=8;
    while(offset<bytes.size()) {
        require(bytes.size()-offset>=12,"truncated PNG chunk"); const auto length=be32(bytes,offset);
        require(length<=bytes.size()-offset-12,"invalid PNG chunk length");
        const auto* type=bytes.data()+offset+4; const auto* payload=type+4;
        for(int i=0;i<4;++i)require((type[i]>='A'&&type[i]<='Z')||(type[i]>='a'&&type[i]<='z'),"invalid PNG chunk type");
        require((type[2]&32)==0,"invalid PNG reserved chunk bit");
        const auto expected=be32(bytes,offset+8+length);
        require(uint32_t(crc32(0,type,uInt(length+4)))==expected,"PNG chunk CRC mismatch");
        const std::string kind(reinterpret_cast<const char*>(type),4);
        if(!header)require(kind=="IHDR","PNG header must be first");
        if(data && kind!="IDAT")dataEnded=true;
        if(kind=="IHDR") {
            require(!header && length==13,"invalid PNG IHDR"); header=true;
            image.width=be32(bytes,offset+8); image.height=be32(bytes,offset+12); depth=payload[8]; color=payload[9];
            require(image.width && image.height && image.width<=kMaxDimension && image.height<=kMaxDimension && uint64_t(image.width)*image.height<=kMaxPixels,"PNG dimensions exceed limit");
            require(payload[10]==0 && payload[11]==0 && payload[12]==0,"unsupported PNG compression/filter/interlace");
            require((color==3 && (depth==1 || depth==2 || depth==4 || depth==8)) || ((color==2 || color==6 || color==0 || color==4) && depth==8),"unsupported PNG color/depth");
        } else if(kind=="PLTE") {
            require(!seenPalette && !seenTransparency && !data && length && length<=768 && length%3==0 && color!=0 && color!=4,"invalid PNG palette");
            seenPalette=true; palette.assign(payload,payload+length);
            if(color==3)require(length/3<=size_t(1u<<depth),"PNG palette exceeds bit depth");
        } else if(kind=="tRNS") {
            require(!seenTransparency && !data,"invalid PNG transparency order"); seenTransparency=true;
            require((color==3 && seenPalette && length && length<=palette.size()/3) || (color==2 && length==6) || (color==0 && length==2),"invalid PNG transparency");
            if(color==0)require(payload[0]==0,"PNG transparency sample exceeds bit depth");
            if(color==2)require(payload[0]==0 && payload[2]==0 && payload[4]==0,"PNG transparency sample exceeds bit depth");
            transparency.assign(payload,payload+length);
        } else if(kind=="IDAT") {
            require(!dataEnded && (color!=3 || seenPalette),"invalid PNG image data order"); data=true;
            require(length<=kMaxAsset-compressed.size(),"oversized PNG compressed data"); compressed.insert(compressed.end(),payload,payload+length);
        } else if(kind=="IEND") {
            require(length==0 && data,"invalid PNG end"); ended=true; offset+=12; break;
        } else require((type[0]&32)!=0,"unsupported critical PNG chunk");
        offset+=size_t(length)+12;
    }
    require(ended && offset==bytes.size() && !compressed.empty(),"missing PNG image data/end or trailing bytes");
    const unsigned channels=color==6?4:color==2?3:color==4?2:1;
    const size_t rowBytes=(size_t(image.width)*channels*depth+7)/8;
    const size_t bytesPerPixel=std::max<size_t>(1,(channels*depth+7)/8);
    Bytes inflated((rowBytes+1)*image.height);
    z_stream stream{}; require(inflateInit(&stream)==Z_OK,"cannot initialize PNG inflater");
    stream.next_in=compressed.data(); stream.avail_in=uInt(compressed.size()); stream.next_out=inflated.data(); stream.avail_out=uInt(inflated.size());
    const int status=inflate(&stream,Z_FINISH); const bool valid=status==Z_STREAM_END && stream.total_out==inflated.size() && stream.avail_in==0;
    inflateEnd(&stream); require(valid,"invalid or oversized PNG compressed stream");
    Bytes samples(rowBytes*image.height);
    for(size_t y=0;y<image.height;++y) {
        const auto filter=inflated[y*(rowBytes+1)]; require(filter<=4,"invalid PNG filter");
        for(size_t x=0;x<rowBytes;++x) {
            const int a=x>=bytesPerPixel?samples[y*rowBytes+x-bytesPerPixel]:0;
            const int b=y?samples[(y-1)*rowBytes+x]:0;
            const int c=y && x>=bytesPerPixel?samples[(y-1)*rowBytes+x-bytesPerPixel]:0;
            const int predictor=filter==0?0:filter==1?a:filter==2?b:filter==3?(a+b)/2:paeth(a,b,c);
            samples[y*rowBytes+x]=uint8_t(inflated[y*(rowBytes+1)+x+1]+predictor);
        }
    }
    image.rgba.resize(size_t(image.width)*image.height*4);
    for(size_t y=0;y<image.height;++y)for(size_t x=0;x<image.width;++x) {
        auto* pixel=&image.rgba[(y*image.width+x)*4]; pixel[3]=255;
        const auto* row=&samples[y*rowBytes];
        if(color==3) {
            const size_t bit=x*depth; const uint8_t index=uint8_t((row[bit/8]>>(8-depth-(bit%8)))&((1u<<depth)-1));
            require(size_t(index)*3+2<palette.size(),"PNG palette index out of range");
            std::copy_n(palette.data()+size_t(index)*3,3,pixel); if(index<transparency.size())pixel[3]=transparency[index];
        } else if(color==2 || color==6) {
            std::copy_n(row+x*channels,3,pixel);
            if(color==6)pixel[3]=row[x*channels+3];
            else if(!transparency.empty() && transparency[0]==0 && transparency[2]==0 && transparency[4]==0 &&
                pixel[0]==transparency[1] && pixel[1]==transparency[3] && pixel[2]==transparency[5])pixel[3]=0;
        } else {
            pixel[0]=pixel[1]=pixel[2]=row[x*channels]; if(color==4)pixel[3]=row[x*channels+1];
            else if(!transparency.empty() && transparency[0]==0 && pixel[0]==transparency[1])pixel[3]=0;
        }
    }
    return image;
}

Presentation makePresentation(Definition definition,Image borders,Image honor,Image terror,Image contested) {
    for(const auto* image:{&borders,&honor,&terror,&contested}) {
        imageValid(*image); require(image->width==borders.width && image->height==borders.height,"strategic PNG dimensions differ");
    }
    require(!definition.parcels.empty() && definition.parcels.size()<=1024,"invalid strategic parcel count");
    Presentation result; result.definition_=std::move(definition); result.borders_=std::move(borders);
    result.honor_=std::move(honor); result.terror_=std::move(terror); result.contested_=std::move(contested);
    const size_t count=size_t(result.width())*result.height(); result.regions_.assign(count,0);
    std::vector<size_t> queue;
    for(const auto& [id,parcel]:result.definition_.parcels) {
        require(id && id==parcel.id,"invalid strategic parcel ID");
        if(parcel.fireX<0 || parcel.fireY<0 || uint32_t(parcel.fireX)>=result.width() || uint32_t(parcel.fireY)>=result.height()) {++result.unmappedParcels_;continue;}
        const size_t seed=size_t(parcel.fireY)*result.width()+size_t(parcel.fireX);
        if(!magenta(result.borders_,seed) || result.regions_[seed]) {++result.unmappedParcels_;continue;}
        queue.clear(); queue.push_back(seed); result.regions_[seed]=id;
        for(size_t i=0;i<queue.size();++i) {
            const size_t pixel=queue[i], x=pixel%result.width(), y=pixel/result.width();
            const auto visit=[&](size_t candidate) {
                if(!result.regions_[candidate] && magenta(result.borders_,candidate)) {result.regions_[candidate]=id;queue.push_back(candidate);}
            };
            if(x)visit(pixel-1);
            if(x+1<result.width())visit(pixel+1);
            if(y)visit(pixel-result.width());
            if(y+1<result.height())visit(pixel+result.width());
        }
    }
    return result;
}
const Parcel* Presentation::parcel(uint32_t id) const {const auto found=definition_.parcels.find(id);return found==definition_.parcels.end()?nullptr:&found->second;}
std::optional<uint32_t> Presentation::regionAt(int x,int y) const {
    if(x<0 || y<0 || uint32_t(x)>=width() || uint32_t(y)>=height())return {};
    const auto id=regions_[size_t(y)*width()+size_t(x)];return id?std::optional<uint32_t>(id):std::nullopt;
}
bool Presentation::compatible(const net::crusades::Snapshot& snapshot,std::string* reason) const {
    const auto no=[&](const char* why){if(reason)*reason=why;return false;};
    if(snapshot.territories.size()!=definition_.parcels.size())return no("Local strategic artwork does not match the authoritative territory set.");
    std::set<uint32_t> ids;
    for(const auto& territory:snapshot.territories) {
        const auto* local=parcel(territory.id);
        if(!local || local->name!=territory.displayName || !ids.insert(territory.id).second)return no("Local strategic parcel IDs/names differ from the authoritative campaign.");
    }
    if(reason)reason->clear();
    return true;
}
Image Presentation::compose(const net::crusades::Snapshot& snapshot) const {
    require(compatible(snapshot),"incompatible authoritative campaign geometry");
    std::map<uint32_t,const Image*> owners;
    for(const auto& territory:snapshot.territories) {
        const Image* source=nullptr;
        if(territory.owner) {
            switch(*territory.owner) {
            case net::crusades::Owner::Honor:source=&honor_;break;
            case net::crusades::Owner::Terror:source=&terror_;break;
            case net::crusades::Owner::Contested:source=&contested_;break;
            default:fail("invalid authoritative territory owner");
            }
        }
        owners.emplace(territory.id,source);
    }
    Image out=borders_;
    for(size_t pixel=0;pixel<regions_.size();++pixel) {
        const auto id=regions_[pixel];
        if(id) {
            const auto* source=owners.at(id);
            if(source)std::copy_n(&source->rgba[pixel*4],4,&out.rgba[pixel*4]);
            else {out.rgba[pixel*4]=out.rgba[pixel*4+1]=out.rgba[pixel*4+2]=48;out.rgba[pixel*4+3]=255;}
        } else if(magenta(borders_,pixel)) {
            out.rgba[pixel*4]=out.rgba[pixel*4+1]=out.rgba[pixel*4+2]=48;out.rgba[pixel*4+3]=255;
        }
    }
    return out;
}
LoadResult loadPresentation(const hpi::Vfs& vfs) {
    try {
        constexpr const char* root="Boneyards/Metagame/";
        const auto read=[&](const char* file) {
            const std::string path=std::string(root)+file;
            require(vfs.has(path,true),"Local Darien strategic artwork is not installed.");
            auto bytes=vfs.read(path,true);require(bytes.size()<=kMaxAsset,"Local strategic asset exceeds size limit.");return bytes;
        };
        auto definition=parseDefinition(read("Darien.def")); auto borders=decodePng(read("Borders.png"));
        auto honor=decodePng(read("HonorMap.png")); auto terror=decodePng(read("TerrorMap.png")); auto contested=decodePng(read("ContestedMap.png"));
        return {makePresentation(std::move(definition),std::move(borders),std::move(honor),std::move(terror),std::move(contested)),{}};
    } catch(const std::exception& error) {return {{},error.what()};}
}
} // namespace tak::crusadesmap
