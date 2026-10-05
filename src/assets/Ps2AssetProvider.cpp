#include "openfear/assets/Ps2AssetProvider.h"
#include "openfear/AssetLocator.h"
#include "openfear/assets/CdLayout.h"
#include "openfear/assets/PathNormalize.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>

#include <delaythread.h>
#include <kernel.h>
#include <libcdvd.h>

namespace openfear {

namespace {
// Serializes sceCdRead()/sceCdSync() across every real caller - the
// prefetch thread below, Read()'s direct fallback, and ReadFile()'s
// iso_.ReadAt() path (reached from more than one thread). cdvdman keeps
// a single global read-state machine that sceCdRead() checks and fails
// against if a prior read hasn't completed/acknowledged yet - exactly
// the kind of shared state two threads calling it at once could
// actually race on.
int g_cdread_sema = -1;

// Diagnostic only - detail behind the most recent CD sector read failure,
// log-only (too low-level for on-screen display).
char g_last_cd_read_diagnostic[128] = "";

// Diagnostic only - why the most recent CD-root scan attempt failed, for
// on-screen display.
char g_last_cd_scan_diagnostic[160] = "";
}  // namespace

const char* Ps2LastCdReadDiagnostic() {
    return g_last_cd_read_diagnostic;
}

const char* Ps2LastCdScanDiagnostic() {
    return g_last_cd_scan_diagnostic;
}

std::optional<std::filesystem::path> Ps2FindCdfsRoot() {
    // Checked BEFORE ever touching Ps2CdSectorSource/sceCdRead: with no
    // disc in the drive at all (a normal case - USB/loader-only runs),
    // sceCdRead()'s own sceCdSync(0) blocks forever waiting for a read
    // completion interrupt that never arrives. SCECdDETCT is the drive's
    // own bounded hardware detection phase (resolves to a real type or
    // NODISC on its own, safe to spin-wait on) - NODISC means exactly
    // that, bail out before ever risking a real, unbounded read.
    while (sceCdGetDiskType() == SCECdDETCT) {}
    const int disk_type = sceCdGetDiskType();
    if (disk_type == SCECdNODISC) {
        std::snprintf(g_last_cd_scan_diagnostic, sizeof(g_last_cd_scan_diagnostic),
                      "no disc in drive, skipped CD scan");
        return std::nullopt;
    }

    Ps2CdSectorSource cd_source;
    Iso9660Reader iso(cd_source);
    if (iso.Mount()) {
        if (iso.Find("DATA/PLAYER.BIN") || iso.Find("INSTALL/ATMOS.EXE")) {
            g_last_cd_scan_diagnostic[0] = '\0';
            return std::filesystem::path("cdfs:");
        }
        std::snprintf(g_last_cd_scan_diagnostic, sizeof(g_last_cd_scan_diagnostic),
                      "mount OK, %zu files, no PLAYER.BIN", iso.entries().size());
    } else {
        std::snprintf(g_last_cd_scan_diagnostic, sizeof(g_last_cd_scan_diagnostic), "%s",
                      iso.last_mount_error());
    }
    // sceCdRead()'s own diagnostic is log-only - too low-level for the
    // on-screen message (g_last_cd_scan_diagnostic above).
    return std::nullopt;
}

std::optional<std::filesystem::path> Ps2FindMassRoot() {
    // "mass:" (USB stick, mounted by bdmfs_fatfs - a different, unaffected
    // driver) still goes through the normal generic check. Root itself
    // first, then one level of subfolders - a USB stick used for more
    // than one homebrew thing is the common case, players shouldn't have
    // to dump the game loose at the drive's root.
    const std::filesystem::path root("mass:");
    if (AssetLocator::is_valid_root(root)) return root;

    // Only ".iso" names containing "ATMOSFEAR" or "OPENFEAR" are tried - a
    // USB stick holding a large PS2 game library (an OPL-style CD/DVD
    // folder) would otherwise mean mounting every single .iso on it just
    // to rule each one out. A dump named something else just needs
    // renaming to be found.
    auto looks_like_our_iso = [](const std::string& name) {
        const std::string upper = NormalizeVirtualPath(name);
        if (upper.size() < 4 || upper.compare(upper.size() - 4, 4, ".ISO") != 0) return false;
        return upper.find("ATMOSFEAR") != std::string::npos ||
               upper.find("OPENFEAR") != std::string::npos;
    };

    std::optional<std::filesystem::path> found;
    DIR* d = opendir("mass:");
    if (d) {
        for (dirent* ent = readdir(d); !found && ent != nullptr; ent = readdir(d)) {
            const std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            const std::filesystem::path candidate = root / name;

            DIR* sub = opendir(candidate.string().c_str());
            if (sub) {
                // A real subdirectory - check it as a folder install. A
                // loose file at mass: root (another homebrew's .elf, a
                // save, ...) goes through the other branch below instead,
                // checked as a loose .iso by name.
                if (AssetLocator::is_valid_root(candidate)) {
                    found = candidate;
                } else {
                    // Not a complete install itself - a loose .iso one
                    // level down (a USB used for more than one homebrew
                    // thing, same reasoning as the folder case above) is
                    // also a valid asset source, read straight out of it
                    // via IsoAssetProvider instead of extracting it first.
                    for (dirent* sub_ent = readdir(sub); sub_ent != nullptr;
                         sub_ent = readdir(sub)) {
                        const std::string sub_name = sub_ent->d_name;
                        if (!looks_like_our_iso(sub_name)) continue;
                        const std::filesystem::path iso_candidate = candidate / sub_name;
                        if (AssetLocator::is_valid_root(iso_candidate)) {
                            found = iso_candidate;
                            break;
                        }
                    }
                }
                closedir(sub);
            } else if (looks_like_our_iso(name) && AssetLocator::is_valid_root(candidate)) {
                // Not a directory - a loose .iso sitting directly in
                // mass: root is also a valid asset source.
                found = candidate;
            }
        }
        closedir(d);
    }
    return found;
}

bool Ps2CdSectorSource::ReadSectors(uint32_t lba, uint32_t count, void* buf) {
    // Called from both the main thread and the prefetch thread below.
    static std::atomic<uint32_t> call_no{0};
    const uint32_t my_no = ++call_no;

    if (g_cdread_sema >= 0) WaitSema(g_cdread_sema);

    sceCdRMode mode{};
    mode.trycount = 5;
    mode.spindlctrl = SCECdSpinNom;
    mode.datapattern = SCECdSecS2048;
    bool ok = sceCdRead(lba, count, buf, &mode);
    if (ok) {
        // Plain blocking sceCdSync(0) is safe here: the fix for
        // "sceCdRead()/sceCdSync() can hang forever with no disc" lives one
        // level up, in AssetLocator::from_cd_scan() (checks
        // sceCdGetDiskType()==SCECdNODISC and skips the whole CD path
        // before ever constructing a Ps2CdSectorSource) - by the time this
        // function runs at all, a real disc is already confirmed present.
        sceCdSync(0);
        const int err = sceCdGetError();
        ok = (err == 0);
        if (!ok) {
            std::snprintf(g_last_cd_read_diagnostic, sizeof(g_last_cd_read_diagnostic), "CdErr=%d",
                          err);
        }
    } else {
        std::snprintf(g_last_cd_read_diagnostic, sizeof(g_last_cd_read_diagnostic),
                      "sceCdRead()=false");
    }

    if (g_cdread_sema >= 0) SignalSema(g_cdread_sema);
    return ok;
}

namespace {

// One level of a directory's real entries, "PREFIX/NAME" (or bare "NAME"
// if prefix is empty), uppercased - used only for "mass:", never cdfs:
// (which goes through Iso9660Reader/Ps2CdSectorSource instead).
void ListOneLevel(const std::filesystem::path& dir, const std::string& prefix,
                  std::vector<std::string>& out) {
    DIR* d = opendir(dir.string().c_str());
    if (!d) return;
    std::vector<std::string> names;
    for (dirent* ent = readdir(d); ent != nullptr; ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name != "." && name != "..") names.push_back(name);
    }
    closedir(d);

    for (const auto& name : names) {
        const std::string upper_name = NormalizeVirtualPath(name);
        const std::string entry = prefix.empty() ? upper_name : prefix + "/" + upper_name;
        DIR* sub = opendir((dir / name).string().c_str());
        if (sub) {
            closedir(sub);
            if (prefix.empty()) ListOneLevel(dir / name, upper_name, out);
            // Two levels deep is never meaningful for CdLayout's rules - skip.
        } else {
            out.push_back(entry);
        }
    }
}

std::vector<std::string> EnumerateMassLayout(const std::filesystem::path& root) {
    std::vector<std::string> paths;
    ListOneLevel(root, "", paths);
    return paths;
}

// A heap-allocated streaming cursor into one Iso9660Reader entry - the
// AssetHandle used whenever Ps2AssetProvider is serving from the real CD
// (using_iso_ == true). Owned by whichever Open() call created it, freed by
// the matching Close().
struct IsoHandle {
    const Iso9660Reader::Entry* entry;
    uint64_t pos;
};

// ---- Background read-ahead for the one streaming handle SDL3VideoPlayer
// ever has open (AVI, via Open()/Read()/Seek()/Close() - ReadFile()'s
// one-shot callers are unaffected). Measured throughput caps at a flat
// ~110-130KB/s regardless of request size, and reads are synchronous on
// the render thread - one slow sceCdRead() freezes decode/present/audio
// together.
// This thread reads ahead of playback into a buffer the main thread
// drains, overlapping disc I/O instead of blocking on it; doesn't raise
// the ceiling, just turns a stall into smoother/later frames.
//
// Read() always falls back to a direct synchronous read for whatever the
// buffer lacks, so a bug here is at worst a missed opportunity, never a
// correctness regression.
constexpr size_t kPrefetchBufBytes = 256 * 1024;
constexpr uint32_t kPrefetchReadSectors = 4;

struct PrefetchState {
    // All fields below guarded by `sema` (a mutex, not a wait/signal
    // producer-consumer handoff - both sides poll via DelayThread()).
    const Iso9660Reader::Entry* entry = nullptr;  // nullptr = idle
    std::vector<uint8_t> buf;                     // linear, not circular
    uint64_t buf_start = 0;                       // absolute file offset buf[0] holds
    uint32_t buf_len = 0;                         // valid bytes in buf, from buf_start
    uint64_t fill_pos = 0;                        // next absolute offset the thread will fetch

    int sema = -1;
    int thread_id = -1;
};

PrefetchState g_prefetch;
alignas(16) char g_prefetch_stack[16 * 1024];

void PrefetchThreadFunc(void*) {
    static uint8_t scratch[kPrefetchReadSectors * 2048];

    for (;;) {
        WaitSema(g_prefetch.sema);
        const Iso9660Reader::Entry* entry = g_prefetch.entry;
        const uint64_t fill_pos = g_prefetch.fill_pos;
        const uint32_t buf_len = g_prefetch.buf_len;
        SignalSema(g_prefetch.sema);

        if (!entry || buf_len >= kPrefetchBufBytes || fill_pos >= entry->size) {
            DelayThread(1000);
            continue;
        }

        // fill_pos is a real file offset, not necessarily sector-aligned -
        // once Read() has consumed any partial amount (an AVI chunk size,
        // essentially never a multiple of 2048), buf_len/room stop being
        // round numbers too, so by the next fetch fill_pos has drifted
        // off a sector boundary. ReadSectors() can only start on a sector
        // boundary, so the actual read below starts BEFORE fill_pos, at
        // sector*2048 - the first sector_lead bytes of scratch are
        // padding that doesn't belong at fill_pos and must be skipped,
        // not published as if they were it.
        const uint32_t sector_lead = static_cast<uint32_t>(fill_pos % 2048);
        Ps2CdSectorSource source;
        const uint32_t sector = entry->lba + static_cast<uint32_t>(fill_pos / 2048);
        if (!source.ReadSectors(sector, kPrefetchReadSectors, scratch)) {
            DelayThread(2000);
            continue;
        }

        WaitSema(g_prefetch.sema);
        // Only commit if nothing changed the target while we were reading -
        // Open()/Seek()/Close(), or Read()'s own synchronous publish below,
        // could all have moved fill_pos (or switched entry) in the meantime.
        if (g_prefetch.entry == entry && g_prefetch.fill_pos == fill_pos) {
            const size_t room = kPrefetchBufBytes - g_prefetch.buf_len;
            const size_t take = std::min(room, sizeof(scratch) - (size_t)sector_lead);
            std::memcpy(g_prefetch.buf.data() + g_prefetch.buf_len, scratch + sector_lead, take);
            g_prefetch.buf_len += static_cast<uint32_t>(take);
            g_prefetch.fill_pos += take;
        }
        SignalSema(g_prefetch.sema);
    }
}

// Guards the one-time init below against two threads both seeing
// thread_id < 0 and racing to create their own semaphores/thread -
// reachable from both Open() (main thread, AVI) and ReadFile() (can run
// on a decode thread too, see StartReadFileInfraIfNeeded()). The loser
// waits for the winner to finish instead of also initializing.
std::atomic<bool> g_prefetch_init_started{false};

// Lazily starts the single, permanent prefetch thread (and the CD-read
// mutex it shares with the main thread) on first use. Cheap to call every
// Open() - a no-op once started.
void StartPrefetchInfraIfNeeded() {
    if (g_prefetch.thread_id >= 0) return;
    if (g_prefetch_init_started.exchange(true)) {
        while (g_prefetch.thread_id < 0) DelayThread(100);
        return;
    }

    ee_sema_t cdread_opt{};
    cdread_opt.init_count = 1;
    cdread_opt.max_count = 1;
    g_cdread_sema = CreateSema(&cdread_opt);

    ee_sema_t prefetch_opt{};
    prefetch_opt.init_count = 1;
    prefetch_opt.max_count = 1;
    g_prefetch.sema = CreateSema(&prefetch_opt);

    g_prefetch.buf.resize(kPrefetchBufBytes);

    register void* gp asm("gp");
    ee_thread_t th{};
    th.func = reinterpret_cast<void*>(PrefetchThreadFunc);
    th.stack = g_prefetch_stack;
    th.stack_size = sizeof(g_prefetch_stack);
    th.gp_reg = gp;
    th.initial_priority = 100;  // low - never contend with the game loop
    th.attr = 0;

    g_prefetch.thread_id = CreateThread(&th);
    if (g_prefetch.thread_id >= 0) StartThread(g_prefetch.thread_id, nullptr);
}

// Background worker for ReadFile() - keeps the actual blocking
// sceCdSync() off whichever thread called ReadFile() (often SDL3Audio's
// own decode_thread_, which calling iso_.ReadAt() directly on stutters
// audibly at the end of playback). The caller only ever blocks on
// WaitSema() here, never on the real disk wait directly.
struct ReadFileRequest {
    const Iso9660Reader::Entry* entry = nullptr;
    Iso9660Reader* reader = nullptr;
    std::vector<uint8_t>* out = nullptr;
    size_t got = 0;
};

ReadFileRequest g_readfile_request;
int g_readfile_request_sema = -1;  // signaled by ReadFile(): "request ready"
int g_readfile_done_sema = -1;     // signaled by the worker: "result ready"
int g_readfile_call_sema = -1;     // serializes the whole mailbox handoff across callers
int g_readfile_thread_id = -1;
alignas(16) char g_readfile_stack[16 * 1024];

void ReadFileThreadFunc(void*) {
    for (;;) {
        WaitSema(g_readfile_request_sema);
        ReadFileRequest& req = g_readfile_request;
        req.out->resize(req.entry->size);
        req.got = req.reader->ReadAt(*req.entry, 0, req.out->data(), req.out->size());
        SignalSema(g_readfile_done_sema);
    }
}

std::atomic<bool> g_readfile_init_started{false};

// Lazily starts the ReadFile() worker thread (and, via
// StartPrefetchInfraIfNeeded(), the shared g_cdread_sema drive-serialization
// mutex - needed here too, regardless of whether any AVI has opened yet).
void StartReadFileInfraIfNeeded() {
    if (g_readfile_thread_id >= 0) return;
    if (g_readfile_init_started.exchange(true)) {
        while (g_readfile_thread_id < 0) DelayThread(100);
        return;
    }
    StartPrefetchInfraIfNeeded();  // ensures g_cdread_sema exists

    ee_sema_t call_opt{};
    call_opt.init_count = 1;
    call_opt.max_count = 1;
    g_readfile_call_sema = CreateSema(&call_opt);

    ee_sema_t req_opt{};
    req_opt.init_count = 0;
    req_opt.max_count = 1;
    g_readfile_request_sema = CreateSema(&req_opt);

    ee_sema_t done_opt{};
    done_opt.init_count = 0;
    done_opt.max_count = 1;
    g_readfile_done_sema = CreateSema(&done_opt);

    register void* gp asm("gp");
    ee_thread_t th{};
    th.func = reinterpret_cast<void*>(ReadFileThreadFunc);
    th.stack = g_readfile_stack;
    th.stack_size = sizeof(g_readfile_stack);
    th.gp_reg = gp;
    th.initial_priority = 100;  // low - never contend with the game loop
    th.attr = 0;

    g_readfile_thread_id = CreateThread(&th);
    if (g_readfile_thread_id >= 0) StartThread(g_readfile_thread_id, nullptr);
}

}  // namespace

Ps2AssetProvider::Ps2AssetProvider(std::filesystem::path root)
    : NativeAssetProvider(std::move(root), false), iso_(cd_source_) {
    if (root_.string().rfind("cdfs:", 0) == 0 && iso_.Mount()) {
        using_iso_ = true;
        std::vector<std::string> paths;
        paths.reserve(iso_.entries().size());
        for (const auto& e : iso_.entries()) paths.push_back(e.path);
        remap_.Build(paths);
    } else {
        const auto paths = EnumerateMassLayout(root_);
        remap_.Build(paths);
    }
}

std::string Ps2AssetProvider::NormalizeAndRemap(const std::string& path) const {
    return remap_.Resolve(NormalizeVirtualPath(path));
}

bool Ps2AssetProvider::Exists(const std::string& path) {
    if (using_iso_) return iso_.Find(NormalizeAndRemap(path)) != nullptr;

    AssetHandle handle = Open(path);  // includes the ";1" fallback
    if (!handle) return false;
    Close(handle);
    return true;
}

std::vector<uint8_t> Ps2AssetProvider::ReadFile(const std::string& path) {
    if (using_iso_) {
        const Iso9660Reader::Entry* e = iso_.Find(NormalizeAndRemap(path));
        if (!e) return {};

        StartReadFileInfraIfNeeded();
        WaitSema(g_readfile_call_sema);
        std::vector<uint8_t> data;
        g_readfile_request.entry = e;
        g_readfile_request.reader = &iso_;
        g_readfile_request.out = &data;
        SignalSema(g_readfile_request_sema);
        WaitSema(g_readfile_done_sema);
        data.resize(g_readfile_request.got);
        SignalSema(g_readfile_call_sema);
        return data;
    }

    const std::string raw = NormalizeAndRemap(path);
    auto data = NativeAssetProvider::ReadFile(raw);
    if (!data.empty()) return data;
    return NativeAssetProvider::ReadFile(raw + ";1");
}

AssetHandle Ps2AssetProvider::Open(const std::string& path) {
    if (using_iso_) {
        const Iso9660Reader::Entry* e = iso_.Find(NormalizeAndRemap(path));
        if (!e) return 0;

        StartPrefetchInfraIfNeeded();
        WaitSema(g_prefetch.sema);
        g_prefetch.entry = e;
        g_prefetch.fill_pos = 0;
        g_prefetch.buf_start = 0;
        g_prefetch.buf_len = 0;
        SignalSema(g_prefetch.sema);

        return reinterpret_cast<AssetHandle>(new IsoHandle{e, 0});
    }

    const std::string raw = NormalizeAndRemap(path);
    AssetHandle handle = NativeAssetProvider::Open(raw);
    if (handle) return handle;
    return NativeAssetProvider::Open(raw + ";1");
}

size_t Ps2AssetProvider::Read(AssetHandle handle, void* buf, size_t n) {
    if (using_iso_) {
        auto* h = reinterpret_cast<IsoHandle*>(handle);
        if (h->pos >= h->entry->size) return 0;
        // Sector-aligned reads below don't stop at the entry's own boundary on their own -
        // without this, a request reaching past EOF would serve bytes belonging to whatever
        // file follows on the disc.
        n = std::min<size_t>(n, static_cast<size_t>(h->entry->size - h->pos));
        uint8_t* out = static_cast<uint8_t*>(buf);
        size_t total = 0;

        // Serve as much as possible from the prefetch buffer.
        WaitSema(g_prefetch.sema);
        if (g_prefetch.entry == h->entry && h->pos >= g_prefetch.buf_start &&
            h->pos < g_prefetch.buf_start + g_prefetch.buf_len) {
            const uint64_t avail = g_prefetch.buf_start + g_prefetch.buf_len - h->pos;
            const size_t take = std::min<size_t>(n, static_cast<size_t>(avail));
            const size_t off = static_cast<size_t>(h->pos - g_prefetch.buf_start);
            std::memcpy(out, g_prefetch.buf.data() + off, take);

            // Drop the now-consumed leading part so the buffer keeps
            // representing "ahead of playback", not "behind it".
            const size_t consumed_end = off + take;
            const size_t remaining = g_prefetch.buf_len - consumed_end;
            std::memmove(g_prefetch.buf.data(), g_prefetch.buf.data() + consumed_end, remaining);
            g_prefetch.buf_start += consumed_end;
            g_prefetch.buf_len = static_cast<uint32_t>(remaining);

            total = take;
            h->pos += take;
        }
        SignalSema(g_prefetch.sema);

        // Whatever the prefetch buffer didn't have: read it directly via
        // cd_source_, sector-aligned - parse_avi()'s header walk jumps all
        // over the file (idx1 near EOF, back to movi's start, ...), which
        // the sequential-only background thread can never race ahead to
        // serve.
        //
        // Not iso_.ReadAt() - its own separate 8-sector cache doesn't know
        // about the prefetch buffer (or vice versa), so the two would
        // silently disagree about what's already been read. Publishing
        // straight into the prefetch thread's own buffer keeps one cache,
        // not two.
        if (total < n) {
            const uint64_t aligned_start = h->pos & ~static_cast<uint64_t>(2047);
            const uint64_t want_end = h->pos + (n - total);
            const uint32_t sectors_needed =
                static_cast<uint32_t>((want_end - aligned_start + 2047) / 2048);
            const uint32_t sector = h->entry->lba + static_cast<uint32_t>(aligned_start / 2048);

            const size_t scratch_size = sectors_needed * 2048u;
            if (read_scratch_.size() < scratch_size) read_scratch_.resize(scratch_size);
            if (cd_source_.ReadSectors(sector, sectors_needed, read_scratch_.data())) {
                const size_t off_in_scratch = static_cast<size_t>(h->pos - aligned_start);
                const size_t take = std::min(scratch_size - off_in_scratch, n - total);
                std::memcpy(out + total, read_scratch_.data() + off_in_scratch, take);
                h->pos += take;
                total += take;

                // Publish into the shared buffer, all-or-nothing, keeping
                // the invariant buf_start + buf_len == fill_pos intact -
                // only ever replace/extend from the current buffer's own
                // end, never partially, so buf_len can't fall behind
                // fill_pos and desync what the two readers think is
                // actually buffered.
                WaitSema(g_prefetch.sema);
                if (g_prefetch.entry == h->entry &&
                    aligned_start >= g_prefetch.buf_start + g_prefetch.buf_len) {
                    const size_t publish = std::min(scratch_size, kPrefetchBufBytes);
                    std::memcpy(g_prefetch.buf.data(), read_scratch_.data(), publish);
                    g_prefetch.buf_start = aligned_start;
                    g_prefetch.buf_len = static_cast<uint32_t>(publish);
                    g_prefetch.fill_pos = aligned_start + publish;
                }
                SignalSema(g_prefetch.sema);
            }
        }

        return total;
    }
    return NativeAssetProvider::Read(handle, buf, n);
}

bool Ps2AssetProvider::Seek(AssetHandle handle, int64_t offset, AssetSeekOrigin origin) {
    if (using_iso_) {
        auto* h = reinterpret_cast<IsoHandle*>(handle);
        int64_t base = 0;
        switch (origin) {
            case AssetSeekOrigin::Set:
                base = 0;
                break;
            case AssetSeekOrigin::Cur:
                base = static_cast<int64_t>(h->pos);
                break;
            case AssetSeekOrigin::End:
                base = static_cast<int64_t>(h->entry->size);
                break;
        }
        const int64_t target = base + offset;
        if (target < 0) return false;
        h->pos = static_cast<uint64_t>(target);

        // Redirect the prefetch only on a real jump (backward, or far
        // enough forward it won't arrive soon anyway) - not on every
        // Seek(), which fires once per AVI chunk, often just a few KB
        // apart. Redirecting on every small forward mismatch would keep
        // discarding the prefetch's still-in-flight target before it
        // could ever deliver, leaving the main thread to do all the real
        // reading itself. 64KB covers normal interleave gaps while still
        // catching genuine jumps (parse_avi()'s idx1-near-EOF probe,
        // seekFrame()).
        constexpr uint64_t kNearAheadBytes = 64 * 1024;
        WaitSema(g_prefetch.sema);
        if (g_prefetch.entry == h->entry &&
            (h->pos < g_prefetch.buf_start || h->pos > g_prefetch.fill_pos + kNearAheadBytes)) {
            g_prefetch.fill_pos = h->pos & ~static_cast<uint64_t>(2047);
            g_prefetch.buf_start = g_prefetch.fill_pos;
            g_prefetch.buf_len = 0;
        }
        SignalSema(g_prefetch.sema);
        return true;
    }
    return NativeAssetProvider::Seek(handle, offset, origin);
}

int64_t Ps2AssetProvider::Tell(AssetHandle handle) {
    if (using_iso_) return static_cast<int64_t>(reinterpret_cast<IsoHandle*>(handle)->pos);
    return NativeAssetProvider::Tell(handle);
}

void Ps2AssetProvider::Close(AssetHandle handle) {
    if (using_iso_) {
        auto* h = reinterpret_cast<IsoHandle*>(handle);
        WaitSema(g_prefetch.sema);
        if (g_prefetch.entry == h->entry) g_prefetch.entry = nullptr;
        SignalSema(g_prefetch.sema);
        delete h;
        return;
    }
    NativeAssetProvider::Close(handle);
}

}  // namespace openfear
