#include "client/gameview.h"

// Out-of-line GameView methods for the power codes (sim/powercodes.h): what a +CODE
// chat line does on this client, and MakePoster's whole-map picture. The sim codes
// leave here as Cmd::PowerCode and change the game only when the server sequences
// them (it drops them while the room has POWER CODES off); Radar and MakePoster are
// local display.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

    void GameView::submitChat(const std::string& line) {
        // Retail's chat handler 0x4b4fb0 runs a '+' line through the command table and
        // then sends it as chat anyway, to every human (not the chosen recipients) --
        // so everyone sees a code typed, whether or not it did anything.
        const auto code = tak::sim::parsePowerCode(line);
        if (code) powerCode(*code);
        mp_->chat(line, code ? uint8_t(0xff) : chatRecipients_);
    }

    void GameView::powerScriptStep() {
        static const char* script = tak::devEnv("TAK_POWER_SCRIPT");
        static size_t at = 0;
        if (!script || !mp_) return;
        const std::string all(script);
        while (at < all.size()) {
            const size_t end = std::min(all.find(';', at), all.size());
            const std::string item = all.substr(at, end - at);
            const size_t colon = item.find(':');
            const uint32_t tick = colon == std::string::npos ? 0 : uint32_t(std::atoi(item.c_str()));
            if (netTick_ < tick) return;
            const std::string line = colon == std::string::npos ? item : item.substr(colon + 1);
            if (const auto code = tak::sim::parsePowerCode(line); code && code->code == tak::sim::PowerCode::ManaMe) {
                selection_.clear();
                for (const UnitR* u : front().live)
                    if (u->alive() && u->player == localPlayer_) selection_.push_back(u->id);
            }
            std::fprintf(stderr, "power script: tick %u types '%s'\n", netTick_, line.c_str());
            submitChat(line);
            at = end + 1;
        }
    }

    bool GameView::powerCodesOn() const {
        if (!mp_ || replayMode_ || spectating_ || !missionStem_.empty() || inLobbyPhase()) return false;
        const auto& room = mp_->room();
        return room.opts.powerCodes != 0 && room.mission.empty() && room.mySlot >= 0;
    }

    void GameView::powerCode(const tak::sim::PowerCodeInput& in) {
        using tak::sim::PowerCode;
        // Retail 0x428f10 skips a group-2 handler unless the match allows cheating:
        // with POWER CODES off the line is ordinary chat and does nothing else.
        if (!powerCodesOn()) return;
        switch (in.code) {
            case PowerCode::Radar: radarCode_ = !radarCode_; return;
            case PowerCode::MakePoster: startPoster(); return;
            case PowerCode::Los:
                if (in.arg == tak::sim::PowerArg::Invalid) return;   // retail ignores any other word
                break;
            default: break;
        }
        if (!tak::sim::simPowerCode(in.code)) return;
        tak::net::Command c;
        c.kind = tak::net::Cmd::PowerCode;
        c.targetId = int32_t(in.code);
        c.queue = uint8_t(in.arg);
        std::lock_guard<std::mutex> lock(simMutex_);
        if (in.code == PowerCode::ManaMe) {
            // 426390 acts on the typer's selection: one command per selected unit of
            // this player's (the sim checks ownership and the pool again).
            for (int id : selection_) {
                const UnitR* u = frameUnitP(id);
                if (!u || !u->alive() || u->player != localPlayer_) continue;
                c.unitId = id;
                issue(c);
            }
            return;
        }
        issue(c);
    }

    // Retail writes <game dir>\screenshots\BIGSHOT%04i.bmp. Ours goes beside it as PNG,
    // in the data folder; a read-only install falls back to the profile folder.
    std::string GameView::posterFolder() const {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        if (const char* dir = tak::devEnv("TAK_POSTER_DIR")) candidates.push_back(fs::u8path(dir));   // tests
        if (!installRoot_.empty()) candidates.push_back(fs::u8path(installRoot_) / "screenshots");
        if (const std::string profile = tak::replayDir(); !profile.empty())
            candidates.push_back(fs::u8path(profile) / "screenshots");
        for (const auto& dir : candidates) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            if (!fs::is_directory(dir, ec)) continue;
            const fs::path probe = dir / ".tak-write-test";
            if (FILE* f = std::fopen(probe.string().c_str(), "wb")) {
                std::fclose(f);
                fs::remove(probe, ec);
                const auto text = dir.u8string();
                return std::string(text.begin(), text.end());
            }
        }
        return {};
    }

    void GameView::startPoster() {
        if (poster_) return;   // one at a time
        const int width = int(mapView_.map().blocksX) * 32, height = int(mapView_.map().blocksY) * 32;
        if (width <= 0 || height <= 0) return;
        const std::string folder = posterFolder();
        if (folder.empty()) { systemChat("POSTER NOT SAVED: NO WRITABLE SCREENSHOTS FOLDER"); return; }
        // The number after the highest existing BIGSHOT, as retail's 0x526650 picks it.
        namespace fs = std::filesystem;
        int highest = 0;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(fs::u8path(folder), ec)) {
            const auto name = entry.path().filename().string();
            int n = 0;
            if (name.size() == 15 && (name.rfind("BIGSHOT", 0) == 0) && std::sscanf(name.c_str() + 7, "%4d", &n) == 1)
                highest = std::max(highest, n);
        }
        char file[32];
        std::snprintf(file, sizeof file, "BIGSHOT%04d.png", std::min(highest + 1, 9999));
        auto job = std::make_unique<PosterJob>();
        job->width = width; job->height = height;
        try {
            job->rgb = std::make_shared<std::vector<uint8_t>>(size_t(width) * size_t(height) * 3, 0);
        } catch (const std::bad_alloc&) {
            systemChat("POSTER NOT SAVED: THE MAP IS TOO LARGE FOR MEMORY");
            return;
        }
        const auto path = fs::u8path(folder) / file;
        const auto text = path.u8string();
        job->path.assign(text.begin(), text.end());
        job->startMs = SDL_GetTicks64();
        poster_ = std::move(job);
        systemChat("MAKING POSTER " + std::to_string(width) + "X" + std::to_string(height) + "...");
    }

    void GameView::posterStep(int winW, int winH) {
        {   // results of finished writers, as chat lines
            std::lock_guard<std::mutex> lock(posterNotes_->m);
            for (auto& line : posterNotes_->lines) systemChat(line);
            posterNotes_->lines.clear();
        }
        if (!poster_ || inLobbyPhase() || loadScreen_) return;
        PosterJob& job = *poster_;
        const int mvw = mapViewW(winW);
        if (!job.tileW) {
            job.tileW = std::min(mvw, job.width);
            job.tileH = std::min(winH, job.height);
            job.cols = (job.width + job.tileW - 1) / job.tileW;
            job.rows = (job.height + job.tileH - 1) / job.tileH;
        }
        if (mvw < job.tileW || winH < job.tileH) { job.tileW = 0; return; }   // the window shrank: retile
        // One tile: the world at native scale (retail draws the poster 1:1), with the
        // selection, health bars and HUD left out; everything else as this player sees it.
        const uint64_t tileStart = SDL_GetPerformanceCounter();
        const int tx = job.next % job.cols, ty = job.next / job.cols;
        const int x0 = tx * job.tileW, y0 = ty * job.tileH;
        const int x1 = std::min(job.width, x0 + job.tileW), y1 = std::min(job.height, y0 + job.tileH);
        const float camX = mapView_.offX(), camY = mapView_.offY(), camZoom = mapView_.zoom();
        auto selection = std::move(selection_);
        selection_.clear();
        const int bars = healthBars_;
        healthBars_ = 0;
        mapView_.setZoom(1.0f);
        mapView_.setOffset(float(x0), float(y0));
        mapView_.ensureChunks(mvw, winH);   // may clamp the edge tiles back onto the map
        mapView_.finishChunks();
        const int ox = int(std::lround(mapView_.offX())), oy = int(std::lround(mapView_.offY()));
        posterFrame_ = true;
        draw(winW, winH);
        posterFrame_ = false;
        std::vector<uint8_t> px(size_t(mvw) * winH * 4);
        const SDL_Rect area{0, 0, mvw, winH};
        const bool read = SDL_RenderReadPixels(ren_, &area, SDL_PIXELFORMAT_RGBA32, px.data(), mvw * 4) == 0;
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
        SDL_RenderClear(ren_);   // the frame's own draw follows; nothing of the tile shows
        mapView_.setZoom(camZoom);
        mapView_.setOffset(camX, camY);
        selection_ = std::move(selection);
        healthBars_ = bars;
        if (read) {
            auto& rgb = *job.rgb;
            for (int y = y0; y < y1; ++y) {
                const int sy = y - oy;
                if (sy < 0 || sy >= winH) continue;
                for (int x = x0; x < x1; ++x) {
                    const int sx = x - ox;
                    if (sx < 0 || sx >= mvw) continue;
                    const uint8_t* s = px.data() + (size_t(sy) * mvw + sx) * 4;
                    uint8_t* d = rgb.data() + (size_t(y) * job.width + x) * 3;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                }
            }
        }
        job.maxTileMs = std::max(job.maxTileMs, double(SDL_GetPerformanceCounter() - tileStart) * 1000.0 /
                                                    double(SDL_GetPerformanceFrequency()));
        if (++job.next < job.cols * job.rows) return;
        // Every tile is in: compress and write on a worker, report through posterNotes_.
        const uint64_t renderMs = SDL_GetTicks64() - job.startMs;
        std::fprintf(stderr, "poster: %dx%d in %d tiles of %dx%d, rendered in %llu ms, worst tile %.1f ms\n",
                     job.width, job.height, job.cols * job.rows, job.tileW, job.tileH, (unsigned long long)renderMs,
                     job.maxTileMs);
        std::thread([rgb = job.rgb, path = job.path, w = job.width, h = job.height, notes = posterNotes_] {
            std::string line;
            try {
                const auto t0 = std::chrono::steady_clock::now();
                tak::png::writeRgb(std::filesystem::u8path(path), w, h, *rgb);
                std::fprintf(stderr, "poster: wrote %s in %lld ms\n", path.c_str(), (long long)
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
                line = "POSTER SAVED: " + path;
            } catch (const std::exception& e) {
                line = std::string("POSTER NOT SAVED: ") + e.what();
            }
            std::lock_guard<std::mutex> lock(notes->m);
            notes->lines.push_back(line);
        }).detach();
        poster_.reset();
    }
