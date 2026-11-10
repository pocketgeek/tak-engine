#include "sim/simcpus.h"
#include "util/devenv.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <thread>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

namespace tak::sim {

namespace {
// Set once a thread of this process is pinned: until then no thread's CPUs are touched.
std::atomic<bool> gPinned{false};
std::atomic<bool> gPinFailureLogged{false};

void pinFailed(int cpu) {
    if (!gPinFailureLogged.exchange(true))
        std::fprintf(stderr,"sim threads: could not pin a thread to cpu %d; it is left unpinned\n",cpu);
}

bool parseInt(std::string_view text,int& out) {
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),out);
    return error==std::errc{} && end==text.data()+text.size() && out>=0;
}

// A sysfs file's first line, without its newline; empty when it cannot be read.
std::string firstLine(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    if (in) std::getline(in,line);
    while (!line.empty() && (line.back()=='\n' || line.back()=='\r' || line.back()==' ')) line.pop_back();
    return line;
}

// Each core's logical CPUs (lowest first) and kind, in the order of their first CPU:
// the list L and the counts.
struct Core { std::vector<int> cpus;bool perf=true; };
CpuTopology ordered(std::vector<Core> cores) {
    CpuTopology t;
    t.perf=0;
    for (const Core& c:cores) ++(c.perf ? t.perf : t.eff);
    if (!t.perf) {   // one kind only: plain cores
        for (Core& c:cores) c.perf=true;
        t.perf=t.eff;t.eff=0;
    }
    for (const bool perf:{true,false})
        for (const Core& c:cores) if (c.perf==perf) t.order.push_back(c.cpus.front());
    for (const bool perf:{true,false})
        for (const Core& c:cores) if (c.perf==perf) t.order.insert(t.order.end(),c.cpus.begin()+1,c.cpus.end());
    t.smt=unsigned(t.order.size())-t.perf-t.eff;
    t.known=true;
    return t;
}

// Every CPU a plain core of its own: what is left when nothing better can be read
// (CpuTopology::known stays false).
CpuTopology plainCpus(std::vector<int> ids) {
    CpuTopology t;
    if (ids.empty()) ids.push_back(0);
    t.perf=unsigned(ids.size());
    t.order=std::move(ids);
    return t;
}
CpuTopology plainCpus(unsigned count) {
    std::vector<int> ids(std::max(count,1u));
    for (size_t i=0;i<ids.size();++i) ids[i]=int(i);
    return plainCpus(std::move(ids));
}

#if defined(__linux__)
// The process's CPUs are its main thread's: sched_getaffinity(0) is the calling
// thread's, which is one CPU on a pinned thread.
bool processCpus(cpu_set_t& set) {
    CPU_ZERO(&set);
    return sched_getaffinity(getpid(),sizeof(set),&set)==0;
}
#elif defined(_WIN32)
DWORD_PTR processCpus() {
    DWORD_PTR process=0,system=0;
    return GetProcessAffinityMask(GetCurrentProcess(),&process,&system) ? process : 0;
}
#endif

CpuTopology nativeCpus() {
#if defined(__linux__)
    cpu_set_t set;
    std::vector<int> allowed;
    if (processCpus(set))
        for (int c=0;c<CPU_SETSIZE;++c) if (CPU_ISSET(c,&set)) allowed.push_back(c);
    if (allowed.empty()) return plainCpus(std::thread::hardware_concurrency());
    return readLinuxCpus("/sys",std::move(allowed));
#elif defined(_WIN32)
    // One record per core: its logical CPUs' mask and its EfficiencyClass (the highest
    // class is a performance core; one class is no split). Group 0, where the thread
    // masks of pinThisThread apply.
    DWORD_PTR allowed=processCpus();
    if (!allowed) allowed=~DWORD_PTR(0);
    DWORD size=0;
    GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&size);
    std::vector<unsigned char> buffer(size);
    if (!size || !GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),&size))
        return plainCpus(std::thread::hardware_concurrency());
    struct Found { int first;unsigned kind;Core core; };
    std::vector<Found> found;
    unsigned top=0;
    for (DWORD at=0;at<size;) {
        const auto* info=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()+at);
        if (!info->Size) break;
        if (info->Relationship==RelationProcessorCore && info->Processor.GroupCount>=1 &&
            info->Processor.GroupMask[0].Group==0) {
            const DWORD_PTR mask=DWORD_PTR(info->Processor.GroupMask[0].Mask)&allowed;
            Found f{-1,unsigned(info->Processor.EfficiencyClass),{}};
            for (int bit=0;bit<int(sizeof(DWORD_PTR)*8);++bit)
                if (mask>>bit&1) f.core.cpus.push_back(bit);
            if (!f.core.cpus.empty()) {
                f.first=f.core.cpus.front();
                top=std::max(top,f.kind);
                found.push_back(std::move(f));
            }
        }
        at+=info->Size;
    }
    if (found.empty()) return plainCpus(std::thread::hardware_concurrency());
    std::sort(found.begin(),found.end(),[](const Found& a,const Found& b) {return a.first<b.first;});
    std::vector<Core> cores;
    for (Found& f:found) {
        f.core.perf=f.kind==top;
        cores.push_back(std::move(f.core));
    }
    return ordered(std::move(cores));
#elif defined(__APPLE__)
    // Counts only: macOS has no CPU ids to give a thread, and pins none.
    const auto value=[](const char* name) {
        int v=0;
        size_t size=sizeof(v);
        return sysctlbyname(name,&v,&size,nullptr,0)==0 && v>0 ? unsigned(v) : 0u;
    };
    unsigned perf=0,eff=0,logical=0;
    if (value("hw.nperflevels")>=2) {
        perf=value("hw.perflevel0.physicalcpu");eff=value("hw.perflevel1.physicalcpu");
        logical=value("hw.perflevel0.logicalcpu")+value("hw.perflevel1.logicalcpu");
    } else {
        perf=value("hw.physicalcpu");logical=value("hw.logicalcpu");
    }
    CpuTopology t=plainCpus(perf && logical>=perf+eff ? logical : std::thread::hardware_concurrency());
    if (perf && logical>=perf+eff) { t.perf=perf;t.eff=eff;t.smt=logical-perf-eff;t.known=true; }
    t.canPin=false;
    return t;
#else
    CpuTopology t=plainCpus(std::thread::hardware_concurrency());
    t.canPin=false;
    return t;
#endif
}
} // namespace

bool parseCpuList(std::string_view text,std::vector<int>& out) {
    constexpr int kMostId=65535;
    std::vector<int> ids;
    for (size_t at=0;at<=text.size();) {
        const size_t comma=std::min(text.find(',',at),text.size());
        const std::string_view part=text.substr(at,comma-at);
        const size_t dash=part.find('-');
        int first=0,last=0;
        if (!parseInt(part.substr(0,dash),first)) return false;
        last=first;
        if (dash!=std::string_view::npos && !parseInt(part.substr(dash+1),last)) return false;
        if (last<first || last>kMostId) return false;
        for (int id=first;id<=last;++id) ids.push_back(id);
        at=comma+1;
    }
    out=std::move(ids);
    return true;
}

std::string cpuListText(const std::vector<int>& cpus) {
    std::string text;
    for (const int cpu:cpus) text+=(text.empty() ? "" : ",")+std::to_string(cpu);
    return text;
}

std::string pinnedText(const std::string& cpus,unsigned unpinned) {
    if (cpus.empty()) return {};
    std::string text=" pinned to cpus "+cpus;
    if (unpinned) text+="; "+std::to_string(unpinned)+(unpinned==1 ? " thread" : " threads")+" not pinned";
    return text;
}

CpuTopology readLinuxCpus(const std::filesystem::path& root,std::vector<int> allowed) {
    namespace fs=std::filesystem;
    const fs::path cpuDir=root/"devices"/"system"/"cpu";
    if (allowed.empty()) {
        std::error_code ec;
        for (fs::directory_iterator it(cpuDir,ec),end;!ec && it!=end;it.increment(ec)) {
            const std::string name=it->path().filename().string();
            int id=0;
            if (name.rfind("cpu",0)==0 && parseInt(std::string_view(name).substr(3),id)) allowed.push_back(id);
        }
    }
    std::sort(allowed.begin(),allowed.end());
    allowed.erase(std::unique(allowed.begin(),allowed.end()),allowed.end());
    const auto cpuPath=[&](int cpu) { return cpuDir/("cpu"+std::to_string(cpu)); };
    // The cores, by what their CPUs say they share.
    std::vector<Core> cores;
    std::vector<std::string> keys;
    for (const int cpu:allowed) {
        const fs::path topology=cpuPath(cpu)/"topology";
        std::string key=firstLine(topology/"thread_siblings_list");
        if (key.empty()) {
            const std::string package=firstLine(topology/"physical_package_id"),core=firstLine(topology/"core_id");
            if (package.empty() || core.empty()) return plainCpus(std::move(allowed));
            key=package+":"+core;
        }
        const size_t at=size_t(std::find(keys.begin(),keys.end(),key)-keys.begin());
        if (at==keys.size()) { keys.push_back(std::move(key));cores.emplace_back(); }
        cores[at].cpus.push_back(cpu);
    }
    if (cores.empty()) return plainCpus(std::move(allowed));
    // Their kind: Intel's hybrid lists, else the capacities, else all alike.
    std::vector<int> perf,eff;
    if (parseCpuList(firstLine(root/"devices"/"cpu_core"/"cpus"),perf) &&
        parseCpuList(firstLine(root/"devices"/"cpu_atom"/"cpus"),eff)) {
        for (Core& c:cores) c.perf=std::find(eff.begin(),eff.end(),c.cpus.front())==eff.end();
    } else {
        std::vector<int> capacity(cores.size());
        int top=0;
        bool have=true;
        for (size_t i=0;i<cores.size() && have;++i) {
            have=parseInt(firstLine(cpuPath(cores[i].cpus.front())/"cpu_capacity"),capacity[i]);
            top=std::max(top,capacity[i]);
        }
        for (size_t i=0;i<cores.size() && have;++i) cores[i].perf=capacity[i]==top;
    }
    return ordered(std::move(cores));
}

CpuTopology detectCpus() {
    const char* fixture=tak::devEnv("TAK_CPU_SYSFS");
    CpuTopology t=fixture && *fixture ? readLinuxCpus(fixture,{}) : nativeCpus();
    if (tak::devFlag("TAK_CPU_NOPIN")) t.canPin=false;
    return t;
}

bool usableCpu(int cpu) {
#if defined(__linux__)
    cpu_set_t all;
    return cpu>=0 && cpu<CPU_SETSIZE && (!processCpus(all) || CPU_ISSET(cpu,&all));
#elif defined(_WIN32)
    const DWORD_PTR all=processCpus();
    return cpu>=0 && cpu<int(sizeof(DWORD_PTR)*8) && (!all || (all>>cpu&1));
#else
    (void)cpu;
    return false;
#endif
}

bool pinThisThread(int cpu) {
    if (!usableCpu(cpu)) return false;
#if defined(__linux__)
    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(cpu,&one);
    if (pthread_setaffinity_np(pthread_self(),sizeof(one),&one)!=0) { pinFailed(cpu);return false; }
#elif defined(_WIN32)
    if (!SetThreadAffinityMask(GetCurrentThread(),DWORD_PTR(1)<<cpu)) { pinFailed(cpu);return false; }
#else
    (void)pinFailed;
    return false;
#endif
    gPinned.store(true,std::memory_order_relaxed);
    return true;
}

void unpinThisThread() {
    if (!gPinned.load(std::memory_order_relaxed)) return;
#if defined(__linux__)
    cpu_set_t all;
    if (processCpus(all)) pthread_setaffinity_np(pthread_self(),sizeof(all),&all);
#elif defined(_WIN32)
    if (const DWORD_PTR all=processCpus()) SetThreadAffinityMask(GetCurrentThread(),all);
#endif
}

} // namespace tak::sim
