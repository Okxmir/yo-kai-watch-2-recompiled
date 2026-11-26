// fs:USER: archives and files. RomFS reads come straight from the player's dump; save data
// lives in a folder in the user's data directory.
#include "service.h"

#include "dump.h"
#include "log.h"
#include "memory.h"
#include "platform.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Test-only tracing, called only by a -MapDiagnostics build. Normal builds
// do not insert the hooks into translated game code. The destination and saved return
// address are still available before the guest sprintf restores its stack.
extern "C" void yw2_trace_formatted_path(const Ctx *ctx) {
    static const bool enabled=std::getenv("YW2_TRACE_MAP_PATHS")!=nullptr;
    if(!enabled)return;
    const uint32_t sp=ctx->r[13];
    if(!mem::is_committed(sp)||!mem::is_committed(sp+24))return;
    auto str=[](uint32_t a){std::string s;for(unsigned i=0;i<192&&mem::is_committed(a+i);++i){char c=char(*mem::ptr(a+i));if(!c)break;if(c<32||c>126)return std::string{};s+=c;}return s;};
    const auto value=str(mem::read32(sp+16));
    if(value.find("t101g")==std::string::npos)return;
    auto fmt=str(mem::read32(sp+20));
    LOG("map-path: %s format=%s caller=%08x",value.c_str(),fmt.c_str(),mem::read32(sp+12));
    std::string stack;
    for(uint32_t i=12;i<1024;i+=4){const uint32_t at=sp+i;if(!mem::is_committed(at))break;const uint32_t pc=mem::read32(at),a=pc&~1u;if(a<0x100004||a>=0x6f4000)continue;const uint32_t insn=mem::read32(a-4);if((insn&0x0f000000u)!=0x0b000000u&&(insn&0x0ffffff0u)!=0x012fff30u)continue;char buf[32];std::snprintf(buf,sizeof buf," +%03x:%08x",i,pc);stack+=buf;}
    LOG("map-path stack:%s",stack.c_str());
}

extern "C" void yw2_trace_stream_grid(const Ctx *ctx, uint32_t pc) {
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;
    if(!enabled)return;
    const uint32_t sp=ctx->r[13], map=ctx->r[4];
    if(!mem::is_committed(sp+180)||!mem::is_committed(map+5788))return;
    auto u32=[](uint32_t a){return mem::read32(a);};
    auto f32=[&](uint32_t a){float f;const auto v=u32(a);std::memcpy(&f,&v,4);return f;};
    const uint32_t grid=map+5088, prev=map+5776;
    if(pc==0x003a0990){
        struct Seen {uint32_t cell=~0u,quad=~0u,n=0;};static std::map<uint32_t,Seen> seen;
        auto &s=seen[map];const uint32_t cell=u32(sp),quad=u32(sp+8)&255;
        if(s.cell==cell&&s.quad==quad&&++s.n%60!=0)return;
        s.cell=cell;s.quad=quad;
        const uint32_t p=u32(sp+172);const bool p_ok=mem::is_committed(p)&&mem::is_committed(p+8);
        LOG("stream-grid map=%08x cell=%u xy=%04x,%04x q=%u prev=%u/%u mode=%u dim=%u,%u origin=%.1f,%.1f size=%.1f,%.1f world=%.1f,%.1f,%.1f relative=%.1f,%.1f player=%.1f,%.1f,%.1f",map,cell,u32(sp+4)&65535,u32(sp+4)>>16,quad,u32(prev),u32(prev+8)&255,unsigned(*mem::ptr(map+445)),u32(grid)&65535,u32(grid)>>16,f32(grid+4),f32(grid+8),f32(grid+12),f32(grid+16),f32(sp+80),f32(sp+84),f32(sp+88),f32(sp+92),f32(sp+100),p_ok?f32(p):0.f,p_ok?f32(p+4):0.f,p_ok?f32(p+8):0.f);
        std::string slots;for(unsigned i=0;i<8;++i){auto a=map+3456+i*204;char buf[140];std::snprintf(buf,sizeof buf," [%u:%u/%u/%u p=%08x,%08x pos=%.0f,%.0f,%.0f]",u32(a+8),unsigned(*mem::ptr(a+12)),unsigned(*mem::ptr(a+14)),unsigned(*mem::ptr(a+15)),u32(a),u32(a+4),f32(a+168),f32(a+172),f32(a+176));slots+=buf;}LOG("stream-slots map=%08x%s",map,slots.c_str());
    }else if(pc==0x003a0cd0){
        LOG("stream-tiles map=%08x cell=%u quad=%u desired=%u,%u,%u,%u previous=%u,%u,%u,%u",map,u32(sp+56),u32(sp+64)&255,u32(sp+24),u32(sp+28),u32(sp+32),u32(sp+36),u32(map+3440),u32(map+3444),u32(map+3448),u32(map+3452));
    }
}

extern "C" void yw2_trace_stream_registration(const Ctx *ctx,uint32_t pc){
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;if(!enabled)return;
    const auto a=ctx->r[4],sp=ctx->r[13];if(!mem::is_committed(a+176)||!mem::is_committed(sp+44))return;
    const uint32_t id=mem::read32(a+8);auto f32=[](uint32_t p){const uint32_t v=mem::read32(p);float f;std::memcpy(&f,&v,4);return f;};
    if(pc==0x0039c164)LOG("stream-register id=%u result=%08x output=%08x slot-pos=%.1f,%.1f,%.1f coll-pos=%.1f,%.1f",id,ctx->r[0],ctx->r[6],f32(a+168),f32(a+172),f32(a+176),f32(sp+40),f32(sp+44));
    else if(pc==0x0039c1a8){static std::map<uint32_t,uint32_t> last;const uint32_t v=ctx->r[0];auto it=last.find(a);if(it==last.end()||it->second!=v){last[a]=v;LOG("stream-register-status id=%u status=%u",id,v);}}
    else if(pc==0x0039c1c4)LOG("stream-register-complete id=%u index=%d data=%08x r6=%08x",id,int32_t(mem::read32(ctx->r[7])),mem::read32(ctx->r[6]+12),ctx->r[6]);
}

extern "C" void yw2_trace_stream_resource(const Ctx *ctx,uint32_t pc){
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;if(!enabled)return;
    const auto slot=ctx->r[5],ref=ctx->r[7];if(!mem::is_committed(slot+212)||!mem::is_committed(ref+4))return;
    const auto obj=mem::read32(ref),name=mem::read32(slot+208);std::string s;if(mem::is_committed(name))for(unsigned i=0;i<96&&mem::is_committed(name+i);++i){char c=char(*mem::ptr(name+i));if(!c)break;if(c<32||c>126){s.clear();break;}s+=c;}
    if(s.rfind("data/map/",0)!=0)return;
    unsigned state=255;uint32_t data=0,vt=0;if(obj&&mem::is_committed(obj+60)){state=*mem::ptr(obj+4);data=mem::read32(obj+60);vt=mem::read32(obj);}
    static std::map<uint64_t,std::pair<uint32_t,unsigned>> last;
    const auto key=(uint64_t(pc)<<32)|slot;auto value=std::make_pair(obj,state);if(last.contains(key)&&last[key]==value)return;last[key]=value;
    LOG("stream-resource %s pc=%08x slot=%08x state=%u obj=%08x state2=%u data=%08x vt=%08x ref=%08x",s.c_str(),pc,slot,unsigned(*mem::ptr(slot)),obj,state,data,vt,ref);
}

static std::map<uint32_t,std::string> stream_job_names;
static bool stream_prm_target=false;
static unsigned stream_prm_index=0;
extern "C" void yw2_trace_archive_job(const Ctx *ctx,uint32_t pc){
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;if(!enabled)return;
    const auto obj=ctx->r[4];if(!mem::is_committed(obj+64)||mem::read32(obj)!=0x00801020)return;
    const auto name=mem::read32(obj+12);std::string s;if(mem::is_committed(name))for(unsigned i=0;i<128&&mem::is_committed(name+i);++i){char c=char(*mem::ptr(name+i));if(!c)break;if(c<32||c>126){s.clear();break;}s+=c;}
    if(s.rfind("data/map/",0)!=0)return;
    const auto data=mem::read32(obj+60);if(!data||!mem::is_committed(data+80))return;
    stream_job_names[data]=s;
    if(pc==0x005e7c00)return;
    static std::map<uint64_t,std::pair<uint32_t,uint32_t>> last;const auto key=(uint64_t(pc)<<32)|obj;auto v=std::make_pair(ctx->r[0],mem::read32(data+36));if(last.contains(key)&&last[key]==v)return;last[key]=v;
    LOG("archive-job name=%s pc=%08x result=%08x obj=%08x ticket=%08x data=%08x vt=%08x f36=%08x f40=%08x f44=%08x f48=%08x f52=%08x f56=%08x f60=%08x f64=%08x f68=%08x",s.c_str(),pc,ctx->r[0],obj,mem::read32(obj+64),data,mem::read32(data),mem::read32(data+36),mem::read32(data+40),mem::read32(data+44),mem::read32(data+48),mem::read32(data+52),mem::read32(data+56),mem::read32(data+60),mem::read32(data+64),mem::read32(data+68));
}

extern "C" void yw2_trace_archive_stage(const Ctx *ctx,uint32_t pc){
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;if(!enabled)return;
    const auto obj=ctx->r[pc>=0x00250000?5:4];auto it=stream_job_names.find(obj);if(it==stream_job_names.end())return;
    const auto sp=ctx->r[13];if(!mem::is_committed(sp+104))return;
    LOG("archive-stage name=%s pc=%08x result=%08x data=%08x r6=%08x r9=%08x r10=%08x r11=%08x sp12=%08x",it->second.c_str(),pc,ctx->r[0],obj,ctx->r[6],ctx->r[9],ctx->r[10],ctx->r[11],mem::read32(sp+12));
}

extern "C" void yw2_trace_archive_finalize(const Ctx *ctx,uint32_t pc){
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;if(!enabled)return;
    const auto sp=ctx->r[13];if(!mem::is_committed(sp+200))return;
    const auto obj=mem::read32(sp+140);auto it=stream_job_names.find(obj);if(it==stream_job_names.end())return;
    LOG("archive-finalize name=%s pc=%08x result=%08x prior=%08x r1=%08x r2=%08x r3=%08x",it->second.c_str(),pc,ctx->r[0],mem::read32(sp+12),ctx->r[1],ctx->r[2],ctx->r[3]);
}

extern "C" void yw2_trace_archive_objects(const Ctx *ctx,uint32_t pc){
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;if(!enabled)return;
    const auto obj=ctx->r[11]-296;auto it=stream_job_names.find(obj);if(it==stream_job_names.end())return;
    // Restrict to the failing sample for the expensive per-object tracing.
    if(it->second.find("t101g56")==std::string::npos)return;
    const auto sp=ctx->r[13];if(!mem::is_committed(sp+384))return;
    if(pc==0x00238bc8){stream_prm_target=true;stream_prm_index=mem::read32(sp+24);}
    if(pc==0x00238c00)stream_prm_target=false;
    LOG("archive-objects name=%s pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r9=%08x prior=%08x i24=%u i44=%u i48=%u i208=%u",it->second.c_str(),pc,ctx->r[0],ctx->r[1],ctx->r[2],ctx->r[3],ctx->r[4],ctx->r[5],ctx->r[6],ctx->r[7],ctx->r[9],mem::read32(sp+192),mem::read32(sp+24),mem::read32(sp+44),mem::read32(sp+48),mem::read32(sp+208));
}

extern "C" void yw2_trace_prm(const Ctx *ctx,uint32_t pc){
    static const bool enabled=std::getenv("YW2_TRACE_STREAM_GRID")!=nullptr;if(!enabled||!stream_prm_target)return;
    if(pc==0x00211ca8||pc==0x00211cbc){const auto p=ctx->r[8];if(mem::is_committed(p)){const uint32_t heap=mem::read32(p)&~3u;if(mem::is_committed(heap+108)){uint32_t n=mem::read32(heap+108),total=0,big=0,count=0;for(;n&&count<10000&&mem::is_committed(n+36);++count){auto size=mem::read32(n+24);total+=size;big=std::max(big,size);n=mem::read32(n+36);}LOG("prm-heap index=%u pc=%08x result=%08x heap=%08x free=%u max=%u count=%u active=%08x h76=%08x h80=%08x head104=%08x head108=%08x",stream_prm_index,pc,ctx->r[0],heap,total,big,count,mem::read32(heap+72),mem::read32(heap+76),mem::read32(heap+80),mem::read32(heap+104),mem::read32(heap+108));}}}
    if(stream_prm_index==42)LOG("prm-stage pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r8=%08x r9=%08x r10=%08x r11=%08x",pc,ctx->r[0],ctx->r[1],ctx->r[2],ctx->r[3],ctx->r[4],ctx->r[5],ctx->r[6],ctx->r[8],ctx->r[9],ctx->r[10],ctx->r[11]);
}

namespace hle {
namespace {

// result codes (module FS)
constexpr uint32_t ERR_FILE_NOT_FOUND = 0xC8804470;
constexpr uint32_t ERR_PATH_NOT_FOUND = 0xC8804478;
constexpr uint32_t ERR_NOT_FORMATTED = 0xC8A04554;
constexpr uint32_t ERR_ALREADY_EXISTS = 0xC82044BE;
constexpr uint32_t ERR_DIR_ALREADY_EXISTS = 0xC82044B9;
constexpr uint32_t ERR_ARCHIVE_NOT_FOUND = 0xC8804465;
constexpr uint32_t ERR_UNSUPPORTED = 0xE0C04702;
constexpr uint32_t ERR_EXTDATA_NOT_FOUND = 0xC8A04478; // games create their extdata on this

enum ArchiveId : uint32_t {
    SELF_NCCH = 0x00000003,
    SAVE_DATA = 0x00000004,
    EXT_SAVE_DATA = 0x00000006,
    SHARED_EXT_SAVE_DATA = 0x00000007,
    SYSTEM_SAVE_DATA = 0x00000008,
    SDMC = 0x00000009,
    NCCH = 0x2345678A,
};

enum PathType : uint32_t { PATH_INVALID = 0, PATH_EMPTY = 1, PATH_BINARY = 2, PATH_ASCII = 3, PATH_UTF16 = 4 };

struct Path {
    uint32_t type;
    std::vector<uint8_t> data;

    std::string text() const {
        std::string s;
        if (type == PATH_ASCII) {
            for (uint8_t c : data)
                if (c) s.push_back(char(c));
        } else if (type == PATH_UTF16) {
            for (size_t i = 0; i + 1 < data.size(); i += 2) {
                uint16_t c = uint16_t(data[i] | (data[i + 1] << 8));
                if (!c) break;
                // save paths are ASCII in practice; keep anything else visible but harmless
                s.push_back(c < 0x80 ? char(c) : '_');
            }
        }
        return s;
    }
};

Path read_path(uint32_t type, uint32_t size, uint32_t addr) {
    Path p{type, {}};
    p.data.assign(mem::ptr(addr), mem::ptr(addr) + size);
    return p;
}

// Maps a guest path under `root`, refusing anything that would escape it.
std::optional<fs::path> host_path(const fs::path &root, const Path &p) {
    fs::path out = root;
    std::string text = p.text();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('/', start);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(start, end - start);
        if (part == ".." || part.find('\\') != std::string::npos || part.find(':') != std::string::npos) return std::nullopt;
        if (!part.empty() && part != ".") out /= part;
        start = end + 1;
    }
    return out;
}

// ---- files ----

// Optional, local streaming diagnostic. It labels ROM reads using the actual
// archive directory and prints plausible guest return addresses for map chunks.
// No game data is copied or modified, and it is inactive during normal play.
void trace_map_load(uint64_t offset,uint32_t len,uint64_t got) {
    static const bool enabled=std::getenv("YW2_TRACE_MAP_LOADS")!=nullptr;
    if(!enabled)return;
    struct Entry {uint64_t start,end;std::string name;};
    static const auto entries=[] {
        std::vector<Entry> out;
        auto file=dump().romfs_file("yw2_a.fa");
        if(!file)return out;
        auto arc=level5::read_arc0([&](uint64_t at,void *dst,uint64_t n){return dump().read_romfs(file->offset+at,dst,n)==n;},file->size);
        if(!arc)return out;
        for(const auto &[name,f]:arc->files)if(name.rfind("data/map/",0)==0&&name.ends_with(".xc"))out.push_back({file->offset+f.offset,file->offset+f.offset+f.size,name});
        std::sort(out.begin(),out.end(),[](const Entry &a,const Entry &b){return a.start<b.start;});
        return out;
    }();
    auto it=std::upper_bound(entries.begin(),entries.end(),offset,[](uint64_t n,const Entry &e){return n<e.start;});
    if(it==entries.begin())return;
    --it;if(offset>=it->end)return;
    auto *thread=kernel::current_thread();
    if(!thread)return;
    const Ctx &ctx=thread->ctx;
    LOG("map-load: %s +%llu bytes=%u read=%llu thread=%u sp=%08x lr=%08x",it->name.c_str(),(unsigned long long)(offset-it->start),len,(unsigned long long)got,thread->id,ctx.r[13],ctx.r[14]);
    // The guest CPU saves return addresses in its own mapped stack. A code
    // address is only considered a candidate when preceded by a branch-link.
    std::string stack;
    for(uint32_t i=0;i<4096;i+=4){
        const uint32_t at=ctx.r[13]+i;if(!mem::is_committed(at))break;
        const uint32_t pc=mem::read32(at),a=pc&~1u;
        if(a<0x100004||a>=0x6f4000)continue;
        const uint32_t insn=mem::read32(a-4);
        bool call=(pc&1u)?((insn&0xf800u)==0xf000u&&((insn>>16)&0xd000u)==0xd000u):((insn&0x0f000000u)==0x0b000000u||(insn&0x0ffffff0u)==0x012fff30u);
        if(!call)continue;
        char value[32];std::snprintf(value,sizeof value," +%03x:%08x",i,pc);stack+=value;
        if(stack.size()>1200)break;
    }
    LOG("map-load stack:%s",stack.c_str());
}

struct File : Service {
    // RomFS-backed (read-only: the game's, or its update's) or a host file
    bool romfs = false, update = false, writable = false;
    std::fstream host;
    fs::path host_path;
    std::function<bool()> before_write;

    File() : Service("fs file") {}

    uint64_t size() {
        if (romfs) return update ? dump().update_romfs_size() : dump().romfs_size();
        return fs::file_size(host_path);
    }

    void handle(Request &req) override {
        switch (req.id()) {
        case 0x0802: { // Read(offset, size) -> bytes read, into a mapped buffer
            uint64_t offset = uint64_t(req.arg(0)) | (uint64_t(req.arg(1)) << 32);
            uint32_t len = req.arg(2), dest = req.arg(4);
            uint64_t got;
            if (romfs) {
                got = update ? dump().read_update_romfs(offset, mem::ptr(dest), len)
                             : dump().read_romfs(offset, mem::ptr(dest), len);
                if(!update)trace_map_load(offset,len,got);
            } else {
                host.clear();
                host.seekg(std::streamoff(offset));
                host.read(reinterpret_cast<char *>(mem::ptr(dest)), len);
                got = uint64_t(host.gcount());
            }
            LOG_IPC("fs Read %s %#llx+%#x -> %#llx", romfs ? (update ? "update-romfs" : "romfs") : "file", (unsigned long long)offset, len,
                    (unsigned long long)got);
            uint32_t desc = req.arg(3);
            auto r = req.reply(2, 2);
            r.push(uint32_t(got));
            r.push(desc);
            r.push(dest);
            break;
        }
        case 0x0803: { // Write(offset, size, flush flags) from a mapped buffer -> bytes written
            uint64_t offset = uint64_t(req.arg(0)) | (uint64_t(req.arg(1)) << 32);
            uint32_t len = req.arg(2), desc = req.arg(4), src = req.arg(5);
            if (romfs || !writable || (before_write && !before_write())) {
                req.reply(1, 0, ERR_UNSUPPORTED);
                break;
            }
            host.clear();
            host.seekp(std::streamoff(offset));
            host.write(reinterpret_cast<const char *>(mem::ptr(src)), len);
            if (req.arg(3) & 1) host.flush();
            if(!host){req.reply(1,0,ERR_UNSUPPORTED);break;}
            auto r = req.reply(2, 2);
            r.push(len);
            r.push(desc);
            r.push(src);
            break;
        }
        case 0x0804: { // GetSize
            auto r = req.reply(3, 0);
            r.push64(size());
            break;
        }
        case 0x0805: { // SetSize(size)
            uint64_t n = uint64_t(req.arg(0)) | (uint64_t(req.arg(1)) << 32);
            if (romfs || !writable || (before_write && !before_write())) {req.reply(1,0,ERR_UNSUPPORTED);break;}
            {
                host.flush();
                std::error_code ec;fs::resize_file(host_path,n,ec);
                if(ec){req.reply(1,0,ERR_UNSUPPORTED);break;}
            }
            req.reply(1, 0);
            break;
        }
        case 0x0808: // Close
            host.close();
            req.reply(1, 0);
            break;
        case 0x0809: // Flush
            host.flush();
            req.reply(1, 0);
            break;
        case 0x080A: // SetPriority
            req.reply(1, 0);
            break;
        case 0x080B: { // GetPriority
            auto r = req.reply(2, 0);
            r.push(0);
            break;
        }
        case 0x080C: { // OpenLinkFile -> another session to this same file
            auto r = req.reply(1, 2);
            auto self = std::static_pointer_cast<File>(shared_from_this());
            r.push_handles({kernel::add_handle(std::make_shared<kernel::Session>(self))}, true);
            break;
        }
        default:
            unimplemented(*this, req);
        }
    }
};

// ---- directories ----

struct Directory : Service {
    struct Entry {
        std::string name;
        bool dir;
        uint64_t size;
    };
    std::vector<Entry> entries;
    size_t next = 0;

    Directory() : Service("fs directory") {}

    void handle(Request &req) override {
        switch (req.id()) {
        case 0x0801: { // Read(count) -> entries read, into a mapped buffer of 0x228-byte entries
            uint32_t count = req.arg(0), desc = req.arg(1), dest = req.arg(2), n = 0;
            for (; n < count && next < entries.size(); n++, next++) {
                uint8_t *e = mem::ptr(dest + n * 0x228);
                std::memset(e, 0, 0x228);
                const Entry &en = entries[next];
                std::string name = en.name;
                for (size_t i = 0; i < name.size() && i < 0x105; i++) {
                    e[i * 2] = uint8_t(name[i]); // UTF-16LE
                }
                // 8.3 short name and extension, upper case
                std::string stem = fs::path(name).stem().string(), ext = fs::path(name).extension().string();
                if (!ext.empty()) ext = ext.substr(1);
                for (size_t i = 0; i < 8 && i < stem.size(); i++) e[0x20C + i] = uint8_t(std::toupper(stem[i]));
                for (size_t i = 0; i < 3 && i < ext.size(); i++) e[0x215 + i] = uint8_t(std::toupper(ext[i]));
                e[0x219] = 1;
                bool dir = en.dir;
                e[0x21B] = dir;
                uint64_t size = dir ? 0 : en.size;
                std::memcpy(e + 0x220, &size, 8);
            }
            auto r = req.reply(2, 2);
            r.push(n);
            r.push(desc);
            r.push(dest);
            break;
        }
        case 0x0802: // Close
            req.reply(1, 0);
            break;
        default:
            unimplemented(*this, req);
        }
    }
};

// ---- archives ----

struct VersionFamily;

struct Archive {
    uint32_t id;
    fs::path root; // empty for RomFS-backed archives
    // another game's link data, whose missing versions are filled in from the ones there
    const VersionFamily *versions = nullptr;
};

// shared extdata holding the system-wide Play Coin count
constexpr uint32_t PLAY_COINS_EXTDATA = 0xF000000B;

void create_play_coins(const fs::path &root) {
    fs::path file = root / "gamecoin.dat";
    if (fs::exists(file)) return;
    fs::create_directories(root);
    // magic, total coins, coins today, total steps, steps today, last date (year, month, day)
    struct {
        uint32_t magic = 0x4F00;
        uint16_t total_coins = 42, coins_today = 0;
        uint32_t total_steps = 0, steps_today = 0;
        uint16_t year = 2014;
        uint8_t month = 12, day = 29;
    } coins;
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(&coins), sizeof coins);
    LOG("fs: created %s", file.string().c_str());
}

// Each game's save data (and its secure values) is kept apart, by program id, under games/<id> in
// the data folder. What lives on the SD card stays at the top and is shared, as on the console:
// extdata is found by its own id, and the three versions of Yo-kai Watch 2 use the same one.
// Before there was more than one game, Psychic Specters' save was at the top.
constexpr uint64_t PSYCHIC_SPECTERS = 0x00040000001B2700;

// Region bridge for links: games read other games' link data from those games' extdata, by id, and
// a Japanese release looks for the Japanese releases' ids. When an id isn't there, its other
// region's game is used instead, if that one's export is. Experimental: the two regions' exports may
// not match.
struct ExtdataBridge {
    uint32_t wanted, instead;
    const char *what;
};
constexpr ExtdataBridge EXTDATA_BRIDGES[] = {
    {0x16C7, 0x1CEB, "Busters (Japan) -> Blasters (USA)"},
    {0x1910, 0x1D67, "Yo-kai Watch 3 (Japan) -> Yo-kai Watch 3 (USA)"},
};

// Link data: each version of a game exports its own folder in the game's extdata ("yw-b_a" Red Cat
// Corps, "yw-b_b" White Dog Squad). A game linking with it is shown every version: a version you
// haven't played reads as one you have, so one copy of a game gives every version's link. Only
// for reading another game's link data; a game writing its own exports always writes its own.
struct VersionFamily {
    uint32_t extdata;
    const char *prefix, *letters; // folders prefix + letter
    uint32_t own[4];              // the extdata ids of the games that export here (unique ids)
};
constexpr VersionFamily VERSION_FAMILIES[] = {
    {0x19A9, "yw2_", "abc", {0x19A9, 0x19AA, 0x1B27}},  // Bony Spirits, Fleshy Souls, Psychic Specters
    {0x1CEB, "yw-b_", "ab", {0x1CEB, 0x1CEF}},          // Red Cat Corps, White Dog Squad
    {0x1D67, "yw3_", "abc", {0x1D67}},                  // Yo-kai Watch 3
};

// `p` (under the archive's root), or the same file in another version's folder if `p` isn't there
fs::path fill_version(const Archive &a, const fs::path &p) {
    std::error_code ec;
    if (!a.versions || fs::exists(p, ec)) return p;
    fs::path rel = fs::relative(p, a.root, ec);
    auto it = rel.begin();
    if (ec || it == rel.end()) return p;
    std::string first = it->string(), prefix = a.versions->prefix;
    if (first.size() != prefix.size() + 1 || first.compare(0, prefix.size(), prefix) != 0) return p;
    fs::path rest;
    for (++it; it != rel.end(); ++it) rest /= *it;
    for (const char *l = a.versions->letters; *l; l++) {
        fs::path other = a.root / (prefix + *l) / rest;
        if (fs::exists(other, ec)) return other;
    }
    return p;
}

// moves the save from before per-game folders into Psychic Specters' (the only game there was)
void move_old_saves() {
    fs::path top = platform::state_dir(), to = platform::game_dir(PSYCHIC_SPECTERS);
    std::error_code ec;
    if (!fs::exists(top / "save", ec)) return;
    if (fs::exists(to / "save", ec)) {
        LOG("fs: both %s and %s exist; leaving the old one alone", (top / "save").string().c_str(),
            (to / "save").string().c_str());
        return;
    }
    fs::create_directories(to, ec);
    auto move = [&](const fs::path &from, const fs::path &dest) {
        fs::rename(from, dest, ec);
        if (ec) LOG("fs: could not move %s: %s", from.string().c_str(), ec.message().c_str());
    };
    std::vector<std::pair<fs::path, fs::path>> moves{{top / "save", to / "save"}}; // listed first, then moved
    for (auto &e : fs::directory_iterator(top, ec))
        if (e.path().filename().string().starts_with("save_secure_value_"))
            moves.push_back({e.path(), to / e.path().filename()});
    for (auto &[from, dest] : moves) move(from, dest);
    LOG("fs: moved Psychic Specters' save data to %s", to.string().c_str());
}

// this game's own data folder
const fs::path &game_root() {
    static const fs::path root = [] {
        move_old_saves();
        fs::path r = platform::game_dir(dump().program_id());
        fs::create_directories(r);
        return r;
    }();
    return root;
}

struct FsUser : Service {
    std::map<uint64_t, Archive> archives;
    uint64_t next_archive = 1;
    std::vector<std::weak_ptr<File>> writable_saves;
    bool save_changed = false;

    FsUser() : Service("fs:USER") {}

    fs::path save_root() const { return game_root() / "save"; }
    // SD-card extra data, by extdata id
    fs::path extdata_root(uint32_t low, uint32_t high) const {
        char name[24];
        std::snprintf(name, sizeof name, "%08x%08x", high, low);
        return platform::state_dir() / "extdata" / name;
    }
    fs::path secure_value_path(uint32_t slot) const {
        return game_root() / ("save_secure_value_" + std::to_string(slot) + ".bin");
    }

    // Files are small and commit is serialized by the guest kernel lock. Keep
    // separate, timestamped snapshots; a failed copy never replaces an earlier
    // snapshot or the player's current save. This also protects FormatSaveData.
    bool checkpoint_save(const char *reason) {
        std::error_code ec;
        if (!fs::exists(save_root(), ec) || fs::is_empty(save_root(), ec)) return !ec;
        auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        fs::path backups = game_root() / "save-backups";
        fs::path dest = backups / (std::string("auto-") + reason + "-" + std::to_string(stamp));
        fs::path pending = dest;
        pending += ".pending";
        fs::create_directories(pending, ec);
        if (!ec) fs::copy(save_root(), pending / "save", fs::copy_options::recursive, ec);
        if (!ec) for (const auto &entry : fs::directory_iterator(game_root(), ec)) {
            if (ec) break;
            if (entry.path().filename().string().starts_with("save_secure_value_") && entry.is_regular_file(ec))
                fs::copy_file(entry.path(), pending / entry.path().filename(), ec);
            if (ec) break;
        }
        if (!ec) fs::rename(pending, dest, ec);
        if (ec) { LOG("fs: could not checkpoint save: %s", ec.message().c_str()); return false; }
        LOG("fs: saved checkpoint %s", dest.string().c_str());
        return true;
    }

    void flush_writable_saves() {
        std::erase_if(writable_saves, [](const std::weak_ptr<File> &weak) {
            auto file = weak.lock();
            if (!file) return true;
            if (file->host.is_open()) file->host.flush();
            return false;
        });
    }

    // Some games never issue ControlArchive(commit), but write an existing
    // Diary in place and finish by updating its secure value. Preserve the
    // last untouched save before the first mutation instead of depending on
    // one particular guest commit sequence.
    bool begin_save_mutation() {
        if(save_changed)return true;
        flush_writable_saves();
        if(!checkpoint_save("before-write"))return false;
        save_changed=true;
        return true;
    }
    void finish_save_mutation(const char *reason) {
        if(!save_changed)return;
        flush_writable_saves();
        if(checkpoint_save(reason))save_changed=false;
    }

    bool atomic_secure_value(uint32_t slot, uint64_t value) {
        fs::path dest = secure_value_path(slot), pending = dest;
        pending += ".pending";
        {
            std::ofstream out(pending, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(&value), sizeof value);
            out.flush();
            if (!out) return false;
        }
#if defined(_WIN32)
        return MoveFileExW(pending.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code ec;
        fs::rename(pending, dest, ec);
        return !ec;
#endif
    }

    // opens archive `id`; result code on failure
    uint32_t open_archive(uint32_t id, const Path &path, Archive *out) {
        switch (id) {
        case SELF_NCCH:
        case NCCH:
            *out = {id, {}};
            return kernel::RESULT_OK;
        case SAVE_DATA:
            if (!fs::exists(save_root())) return ERR_NOT_FORMATTED;
            *out = {id, save_root()};
            return kernel::RESULT_OK;
        case EXT_SAVE_DATA: { // binary path: media type, extdata id low, high
            uint32_t w[3] = {};
            std::memcpy(w, path.data.data(), std::min<size_t>(path.data.size(), sizeof w));
            fs::path root = extdata_root(w[1], w[2]);
            if (!fs::exists(root) && w[2] == 0)
                for (const ExtdataBridge &b : EXTDATA_BRIDGES)
                    if (w[1] == b.wanted && fs::exists(extdata_root(b.instead, 0))) {
                        LOG("fs: extdata %08x isn't here; linking with %08x instead (%s)", b.wanted, b.instead, b.what);
                        root = extdata_root(b.instead, 0);
                    }
            if (!fs::exists(root)) return ERR_EXTDATA_NOT_FOUND;
            *out = {id, root};
            uint32_t self = uint32_t(dump().program_id() >> 8) & 0xFFFFF; // the running game's unique id
            for (const VersionFamily &v : VERSION_FAMILIES)
                if (w[2] == 0 && root == extdata_root(v.extdata, 0) &&
                    std::find(std::begin(v.own), std::end(v.own), self) == std::end(v.own))
                    out->versions = &v;
            return kernel::RESULT_OK;
        }
        case SDMC: {
            fs::path root = platform::state_dir() / "sdmc";
            fs::create_directories(root);
            *out = {id, root};
            return kernel::RESULT_OK;
        }
        case SHARED_EXT_SAVE_DATA: {
            // binary path: media type, extdata id (u64)
            uint32_t ext_id = path.data.size() >= 8 ? uint32_t(path.data[4] | (path.data[5] << 8) |
                                                                (path.data[6] << 16) | (path.data[7] << 24))
                                                     : 0;
            char name[16];
            std::snprintf(name, sizeof name, "%08x", ext_id);
            fs::path root = platform::state_dir() / "extdata" / "shared" / name;
            if (ext_id == PLAY_COINS_EXTDATA) create_play_coins(root);
            if (!fs::exists(root)) {
                LOG("fs: shared extdata %08x not present", ext_id);
                return ERR_ARCHIVE_NOT_FOUND;
            }
            *out = {id, root};
            return kernel::RESULT_OK;
        }
        default: {
            std::string bytes;
            for (uint8_t b : path.data) {
                char buf[4];
                std::snprintf(buf, sizeof buf, "%02x", b);
                bytes += buf;
            }
            LOG("fs: archive %#x (path type %u: %s) not supported; reporting it as not found", id, path.type,
                bytes.c_str());
            return ERR_ARCHIVE_NOT_FOUND;
        }
        }
    }

    Archive *archive(uint32_t lo, uint32_t hi) {
        auto it = archives.find(uint64_t(lo) | (uint64_t(hi) << 32));
        return it == archives.end() ? nullptr : &it->second;
    }

    // opens a file in an archive; result code, and the file on success
    uint32_t open_file(const Archive &a, const Path &path, uint32_t flags, std::shared_ptr<File> *out) {
        auto f = std::make_shared<File>();
        if (a.root.empty()) {
            // RomFS: binary path whose first word is the content type (0 = RomFS, 5 = the update's
            // RomFS, which a game with no update installed doesn't find)
            uint32_t type = 0;
            if (path.data.size() >= 4) std::memcpy(&type, path.data.data(), 4);
            if (type == 5 && !dump().has_update()) return ERR_FILE_NOT_FOUND;
            if (type != 0 && type != 5) {
                LOG("fs: NCCH content type %u not supported", type);
                return ERR_FILE_NOT_FOUND;
            }
            f->romfs = true, f->update = type == 5;
            if (f->update) LOG("fs: the game opened its update's RomFS");
            *out = f;
            return kernel::RESULT_OK;
        }
        auto p = host_path(a.root, path);
        if (!p) return ERR_PATH_NOT_FOUND;
        bool write = flags & 2, create = flags & 4;
        if (!write && !create) p = fill_version(a, *p);
        if (!fs::exists(*p)) {
            if (!create) return ERR_FILE_NOT_FOUND;
            if(a.id==SAVE_DATA&&!begin_save_mutation())return ERR_UNSUPPORTED;
            std::ofstream(*p, std::ios::binary);
        }
        f->host_path = *p;
        f->writable = write;
        f->host.open(*p, std::ios::binary | std::ios::in | (write ? std::ios::out : std::ios::openmode{}));
        if (!f->host) return ERR_FILE_NOT_FOUND;
        if (a.id == SAVE_DATA && write) {
            writable_saves.push_back(f);
            f->before_write=[this]{return begin_save_mutation();};
        }
        *out = f;
        return kernel::RESULT_OK;
    }

    void reply_file(Request &req, uint32_t result, const std::shared_ptr<File> &f, const char *what,
                    const Path &path) {
        LOG_IPC("fs %s \"%s\" -> %08x", what, path.type == PATH_BINARY ? "<binary>" : path.text().c_str(), result);
        if (result != kernel::RESULT_OK) {
            req.reply(1, 0, result);
            return;
        }
        auto r = req.reply(1, 2);
        r.push_handles({kernel::add_handle(std::make_shared<kernel::Session>(f))}, true);
    }

    void handle(Request &req) override {
        switch (req.id()) {
        case 0x0801: // Initialize
        case 0x0861: // InitializeWithSdkVersion
        case 0x0862: // SetPriority
            req.reply(1, 0);
            break;
        case 0x0863: { // GetPriority
            auto r = req.reply(2, 0);
            r.push(0);
            break;
        }
        case 0x080C: { // OpenArchive(id, path type, size, path) -> archive handle
            Archive a;
            Path path = read_path(req.arg(1), req.arg(2), req.arg(4));
            uint32_t result = open_archive(req.arg(0), path, &a);
            if (req.arg(0) == EXT_SAVE_DATA && path.data.size() >= 12) { // which game's extdata
                uint32_t low, high;
                std::memcpy(&low, path.data.data() + 4, 4);
                std::memcpy(&high, path.data.data() + 8, 4);
                LOG_IPC("fs OpenArchive %#x (extdata %08x%08x) -> %08x", req.arg(0), high, low, result);
            } else {
                LOG_IPC("fs OpenArchive %#x -> %08x", req.arg(0), result);
            }
            if (result != kernel::RESULT_OK) {
                req.reply(1, 0, result);
                break;
            }
            uint64_t h = next_archive++;
            archives[h] = a;
            auto r = req.reply(3, 0);
            r.push64(h);
            break;
        }
        case 0x080D: { // ControlArchive: commit writable save files and retain a recovery checkpoint
            auto *a = archive(req.arg(0), req.arg(1));
            if (a && a->id == SAVE_DATA && req.arg(2) == 0 && save_changed) {
                finish_save_mutation("commit");
            }
            req.reply(1, 0);
            break;
        }
        case 0x080E: // CloseArchive(handle)
            archives.erase(uint64_t(req.arg(0)) | (uint64_t(req.arg(1)) << 32));
            req.reply(1, 0);
            break;
        case 0x0802: { // OpenFile(transaction, archive, path type, size, flags, attributes, path)
            Archive *a = archive(req.arg(1), req.arg(2));
            Path path = read_path(req.arg(3), req.arg(4), req.arg(8));
            std::shared_ptr<File> f;
            uint32_t result = a ? open_file(*a, path, req.arg(5), &f) : kernel::RESULT_INVALID_HANDLE;
            reply_file(req, result, f, "OpenFile", path);
            break;
        }
        case 0x0803: { // OpenFileDirectly(transaction, archive id, archive path, file path, flags, attrs)
            Archive a;
            Path apath = read_path(req.arg(2), req.arg(3), req.arg(9));
            Path fpath = read_path(req.arg(4), req.arg(5), req.arg(11));
            uint32_t result = open_archive(req.arg(1), apath, &a);
            std::shared_ptr<File> f;
            if (result == kernel::RESULT_OK) result = open_file(a, fpath, req.arg(6), &f);
            reply_file(req, result, f, "OpenFileDirectly", fpath);
            break;
        }
        case 0x0804:   // DeleteFile(transaction, archive, path type, size, path)
        case 0x0806:   // DeleteDirectory
        case 0x0807: { // DeleteDirectoryRecursively
            Archive *a = archive(req.arg(1), req.arg(2));
            Path path = read_path(req.arg(3), req.arg(4), req.arg(6));
            auto p = a && !a->root.empty() ? host_path(a->root, path) : std::nullopt;
            uint32_t result;
            if (!p || !fs::exists(*p)) {
                result = req.id() == 0x0804 ? ERR_FILE_NOT_FOUND : ERR_PATH_NOT_FOUND;
            } else if(a->id==SAVE_DATA&&!begin_save_mutation()) {
                result=ERR_UNSUPPORTED;
            } else {
                std::error_code ec;
                if (req.id() == 0x0807) fs::remove_all(*p, ec);
                else fs::remove(*p, ec);
                result = ec ? ERR_PATH_NOT_FOUND : kernel::RESULT_OK;
            }
            LOG_IPC("fs Delete \"%s\" -> %08x", path.text().c_str(), result);
            req.reply(1, 0, result);
            break;
        }
        case 0x0805:   // RenameFile(transaction, src archive, src type, src size, dst archive, dst type, dst size)
        case 0x080A: { // RenameDirectory
            Archive *sa = archive(req.arg(1), req.arg(2)), *da = archive(req.arg(5), req.arg(6));
            Path src = read_path(req.arg(3), req.arg(4), req.arg(10));
            Path dst = read_path(req.arg(7), req.arg(8), req.arg(12));
            auto sp = sa && !sa->root.empty() ? host_path(sa->root, src) : std::nullopt;
            auto dp = da && !da->root.empty() ? host_path(da->root, dst) : std::nullopt;
            uint32_t result = ERR_PATH_NOT_FOUND;
            if (sp && dp && fs::exists(*sp)) {
                if (fs::exists(*dp)) {
                    result = ERR_ALREADY_EXISTS;
                } else if((sa->id==SAVE_DATA||da->id==SAVE_DATA)&&!begin_save_mutation()) {
                    result=ERR_UNSUPPORTED;
                } else {
                    std::error_code ec;
                    fs::rename(*sp, *dp, ec);
                    result = ec ? ERR_PATH_NOT_FOUND : kernel::RESULT_OK;
                }
            }
            req.reply(1, 0, result);
            break;
        }
        case 0x0808: { // CreateFile(transaction, archive, path type, size, attributes, file size, path)
            Archive *a = archive(req.arg(1), req.arg(2));
            Path path = read_path(req.arg(3), req.arg(4), req.arg(9));
            uint64_t size = uint64_t(req.arg(6)) | (uint64_t(req.arg(7)) << 32);
            auto p = a && !a->root.empty() ? host_path(a->root, path) : std::nullopt;
            uint32_t result;
            if (!p || !fs::exists(p->parent_path())) {
                result = ERR_PATH_NOT_FOUND;
            } else if (fs::exists(*p)) {
                result = ERR_ALREADY_EXISTS;
            } else if(a->id==SAVE_DATA&&!begin_save_mutation()) {
                result=ERR_UNSUPPORTED;
            } else {
                {std::ofstream file(*p,std::ios::binary);if(!file){req.reply(1,0,ERR_UNSUPPORTED);break;}}
                std::error_code ec;fs::resize_file(*p,size,ec);
                result = ec ? ERR_UNSUPPORTED : kernel::RESULT_OK;
            }
            LOG_IPC("fs CreateFile \"%s\" (%llu bytes) -> %08x", path.text().c_str(), (unsigned long long)size,
                    result);
            req.reply(1, 0, result);
            break;
        }
        case 0x0809: { // CreateDirectory(transaction, archive, path type, size, attributes, path)
            Archive *a = archive(req.arg(1), req.arg(2));
            Path path = read_path(req.arg(3), req.arg(4), req.arg(7));
            auto p = a && !a->root.empty() ? host_path(a->root, path) : std::nullopt;
            uint32_t result = !p                 ? ERR_PATH_NOT_FOUND
                              : fs::exists(*p)   ? ERR_DIR_ALREADY_EXISTS
                              : a->id==SAVE_DATA&&!begin_save_mutation() ? ERR_UNSUPPORTED
                              : fs::create_directory(*p) ? kernel::RESULT_OK
                                                         : ERR_PATH_NOT_FOUND;
            req.reply(1, 0, result);
            break;
        }
        case 0x080B: { // OpenDirectory(archive, path type, size, path) -> directory
            Archive *a = archive(req.arg(0), req.arg(1));
            Path path = read_path(req.arg(2), req.arg(3), req.arg(5));
            auto p = a && !a->root.empty() ? host_path(a->root, path) : std::nullopt;
            if (p) p = fill_version(*a, *p);
            if (!p || !fs::is_directory(*p)) {
                req.reply(1, 0, ERR_PATH_NOT_FOUND);
                break;
            }
            auto d = std::make_shared<Directory>();
            std::error_code ec;
            for (auto &e : fs::directory_iterator(*p, ec))
                d->entries.push_back({e.path().filename().string(), e.is_directory(ec), e.is_directory(ec) ? 0 : e.file_size(ec)});
            // another game's link data: every version's folder, if any one is there
            if (a->versions && fs::equivalent(*p, a->root, ec)) {
                std::string prefix = a->versions->prefix;
                bool any = std::any_of(d->entries.begin(), d->entries.end(), [&](auto &e) {
                    return e.dir && e.name.size() == prefix.size() + 1 && e.name.compare(0, prefix.size(), prefix) == 0;
                });
                for (const char *l = a->versions->letters; any && *l; l++) {
                    std::string name = prefix + *l;
                    if (std::none_of(d->entries.begin(), d->entries.end(), [&](auto &e) { return e.name == name; })) {
                        d->entries.push_back({name, true, 0});
                        LOG("fs: link data: %s shown from another version", name.c_str());
                    }
                }
            }
            auto r = req.reply(1, 2);
            r.push_handles({kernel::add_handle(std::make_shared<kernel::Session>(d))}, true);
            break;
        }
        case 0x0812: { // GetFreeBytes(archive)
            auto r = req.reply(3, 0);
            r.push64(512ull << 20);
            break;
        }
        case 0x0817:   // IsSdmcDetected: the user data folder stands in for the SD card
        case 0x0818: { // IsSdmcWritable
            auto r = req.reply(2, 0);
            r.push(1);
            break;
        }
        case 0x0851: { // CreateExtSaveData(media, id low, id high, unknown, dirs, files, size lo/hi, icon size, icon)
            fs::path root = extdata_root(req.arg(1), req.arg(2));
            std::error_code ec;
            fs::create_directories(root, ec);
            LOG("fs: created extra save data %s", root.string().c_str());
            req.reply(1, 0);
            break;
        }
        case 0x0852: { // DeleteExtSaveData(media, id low, id high, unknown)
            std::error_code ec;
            fs::remove_all(extdata_root(req.arg(1), req.arg(2)), ec);
            req.reply(1, 0);
            break;
        }
        case 0x0821: { // CardSlotIsInserted
            auto r = req.reply(2, 0);
            r.push(1);
            break;
        }
        case 0x0845: { // GetFormatInfo(archive id, path) -> total size, directories, files, duplicate data
            if (req.arg(0) == SAVE_DATA && !fs::exists(save_root())) {
                req.reply(1, 0, ERR_NOT_FORMATTED);
                break;
            }
            auto r = req.reply(5, 0);
            r.push(512 << 10);
            r.push(32);
            r.push(32);
            r.push(0);
            break;
        }
        // Secure values guard against restoring old saves; the game stores one per slot and
        // checks it against the save. Kept next to the save directory, not inside it.
        case 0x0875: { // SetThisSaveDataSecureValue(?, ?, slot, value lo, value hi, ?)
            uint64_t value = req.arg(3) | (uint64_t(req.arg(4)) << 32);
            bool saved = begin_save_mutation() && atomic_secure_value(req.arg(2), value);
            if (!saved) LOG("fs: could not commit secure save value");
            else finish_save_mutation("secure-value");
            req.reply(1, 0, saved ? kernel::RESULT_OK : ERR_UNSUPPORTED);
            break;
        }
        case 0x086F: { // GetThisSaveDataSecureValue(slot) -> exists, is gamecard, value
            uint64_t value = 0;
            bool exists = bool(std::ifstream(secure_value_path(req.arg(0)), std::ios::binary)
                                   .read(reinterpret_cast<char *>(&value), sizeof value));
            auto r = req.reply(5, 0);
            r.push(exists);
            r.push(1);
            r.push64(exists ? value : 0);
            break;
        }
        case 0x084C: // FormatSaveData(archive id, path, block size, dirs, files, buckets, duplicate data)
            if (req.arg(0) == SAVE_DATA) {
                std::error_code ec;
                flush_writable_saves();
                if (!checkpoint_save("before-format")) {
                    req.reply(1, 0, ERR_UNSUPPORTED);
                    break;
                }
                fs::remove_all(save_root(), ec);
                if (ec) { req.reply(1, 0, ERR_UNSUPPORTED); break; }
                for (auto &e : fs::directory_iterator(game_root(), ec))
                    if (e.path().filename().string().starts_with("save_secure_value_")) fs::remove(e.path(), ec);
                fs::create_directories(save_root());
                LOG("fs: formatted save data at %s", save_root().string().c_str());
            }
            req.reply(1, 0);
            break;
        default:
            unimplemented(*this, req);
        }
    }
};

} // namespace

void register_fs() { register_service({"fs:USER"}, std::make_shared<FsUser>()); }

} // namespace hle
