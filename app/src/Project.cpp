// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Project.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <sstream>

namespace mend::editor {

namespace {
constexpr char kMagic[8] = { 'M', 'E', 'N', 'D', 'P', 'R', 'O', 'J' };
constexpr uint32_t kVersion = 1;

std::string keyName(const char* name) // "Breath Control" -> "Breath_Control"
{
    std::string k(name);
    std::replace(k.begin(), k.end(), ' ', '_');
    return k;
}

// Values on one line can't contain newlines: escape them.
std::string escape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '\\')
            o += "\\\\";
        else if (c == '\n')
            o += "\\n";
        else if (c == '\r')
            o += "\\r";
        else
            o += c;
    }
    return o;
}

std::string unescape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            o += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::string numbers(const std::vector<float>& v)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os.precision(9);
    for (size_t i = 0; i < v.size(); ++i)
        os << (i ? " " : "") << v[i];
    return os.str();
}

std::vector<float> parseNumbers(const std::string& s)
{
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    std::vector<float> v;
    float f;
    while (is >> f)
        v.push_back(f);
    return v;
}

double parseDouble(const std::string& s, double fallback)
{
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double d;
    return (is >> d) ? d : fallback;
}

// Copy what was saved over the defaults (tolerates added or removed parameters).
void overlay(std::vector<float>& values, const std::vector<float>& saved)
{
    for (size_t i = 0; i < values.size() && i < saved.size(); ++i)
        values[i] = saved[i];
}

struct File {
    FILE* f = nullptr;
    ~File()
    {
        if (f != nullptr)
            std::fclose(f);
    }
};

bool hostIsLittleEndian()
{
    const uint16_t x = 1;
    unsigned char b;
    std::memcpy(&b, &x, 1);
    return b == 1;
}
} // namespace

bool isProjectFile(const std::string& path)
{
    std::string lower = path;
    for (auto& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string ext = kProjectExtension;
    return lower.size() > ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0;
}

bool saveProject(const std::string& path, const AudioBuffer& audio, const ProjectState& st, std::string& error,
                 std::atomic<float>* progress)
{
    if (!hostIsLittleEndian()) {
        error = "Saving projects isn't supported on this kind of computer.";
        return false;
    }
    const size_t nch = audio.channels.size(), frames = audio.numFrames();

    std::ostringstream h;
    h.imbue(std::locale::classic());
    h.precision(17);
    h << "mend-project 1\n";
    h << "source " << escape(st.sourcePath) << "\n";
    h << "rate " << audio.sampleRate << "\n";
    h << "channels " << nch << "\n";
    h << "frames " << frames << "\n";
    h << "view.blend " << st.viewBlend << "\n";
    h << "view.scale " << st.freqScale << "\n";
    h << "view.floor " << st.floorDb << "\n";
    h << "view.start " << st.viewStart << "\n";
    h << "view.fpp " << st.framesPerPx << "\n";
    h << "cursor " << st.cursor << "\n";
    h << "selection " << st.selStart << " " << st.selEnd << "\n";
    h << "box " << (st.boxActive ? 1 : 0) << " " << st.boxLoHz << " " << st.boxHiHz << "\n";
    h << "preview " << (st.preview ? 1 : 0) << "\n";
    for (int t = 0; t < static_cast<int>(ToolId::Count) && t < static_cast<int>(st.chain.size()); ++t) {
        const std::string k = "tool." + keyName(toolName(static_cast<ToolId>(t)));
        const ToolSettings& s = st.chain[static_cast<size_t>(t)];
        h << k << ".enabled " << (s.enabled ? 1 : 0) << "\n";
        h << k << ".values " << numbers(s.values) << "\n";
        if (!s.noiseProfile.empty())
            h << k << ".profile " << escape(s.noiseProfile) << "\n";
    }
    for (int m = 0; m < static_cast<int>(ModuleId::Count) && m < static_cast<int>(st.modules.size()); ++m)
        h << "module." << keyName(moduleInfo(static_cast<ModuleId>(m)).name) << ".values "
          << numbers(st.modules[static_cast<size_t>(m)]) << "\n";
    const std::string header = h.str();

    // Write beside the target and move it into place, so a failed save
    // never damages an existing project.
    const std::string tmp = path + ".saving";
    {
        File out { openFileUtf8(tmp, "wb") };
        if (out.f == nullptr) {
            error = "Couldn't write to that folder. Check it exists and isn't read-only.";
            return false;
        }
        const uint32_t version = kVersion;
        const uint64_t hlen = header.size();
        bool ok = std::fwrite(kMagic, 1, 8, out.f) == 8 && std::fwrite(&version, 4, 1, out.f) == 1
            && std::fwrite(&hlen, 8, 1, out.f) == 1 && std::fwrite(header.data(), 1, header.size(), out.f) == header.size();
        const size_t chunk = 1 << 18;
        for (size_t c = 0; ok && c < nch; ++c) {
            const float* data = audio.channels[c].data();
            for (size_t at = 0; ok && at < frames; at += chunk) {
                const size_t n = std::min(chunk, frames - at);
                ok = std::fwrite(data + at, sizeof(float), n, out.f) == n;
                if (progress != nullptr)
                    progress->store(static_cast<float>((c * static_cast<double>(frames) + at + n) / (nch * static_cast<double>(frames))));
            }
        }
        ok = ok && std::fflush(out.f) == 0;
        if (std::fclose(out.f) != 0)
            ok = false;
        out.f = nullptr;
        if (!ok) {
            removeFileUtf8(tmp);
            error = "Couldn't save the project (is the disk full?).";
            return false;
        }
    }
    if (!replaceFile(tmp, path)) {
        removeFileUtf8(tmp);
        error = "Couldn't replace the existing project file. Is it open in another program?";
        return false;
    }
    return true;
}

bool loadProject(const std::string& path, AudioBuffer& audio, ProjectState& st, std::string& error,
                 std::atomic<float>* progress, std::atomic<bool>* cancel)
{
    File in { openFileUtf8(path, "rb") };
    if (in.f == nullptr) {
        error = "Couldn't open the project. Check it still exists.";
        return false;
    }
    char magic[8];
    uint32_t version = 0;
    uint64_t hlen = 0;
    if (std::fread(magic, 1, 8, in.f) != 8 || std::memcmp(magic, kMagic, 8) != 0 || std::fread(&version, 4, 1, in.f) != 1
        || std::fread(&hlen, 8, 1, in.f) != 1 || hlen > (64u << 20)) {
        error = "This isn't a Mend project, or it's damaged.";
        return false;
    }
    if (version > kVersion) {
        error = "This project was saved by a newer version of Mend. Please update Mend to open it.";
        return false;
    }
    if (!hostIsLittleEndian()) {
        error = "Opening projects isn't supported on this kind of computer.";
        return false;
    }
    std::string header(static_cast<size_t>(hlen), '\0');
    if (std::fread(&header[0], 1, header.size(), in.f) != header.size()) {
        error = "The project is damaged (its settings are cut short).";
        return false;
    }

    std::map<std::string, std::string> kv;
    std::istringstream lines(header);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t sp = line.find(' ');
        if (sp != std::string::npos)
            kv[line.substr(0, sp)] = line.substr(sp + 1);
    }
    auto get = [&](const std::string& k) { auto it = kv.find(k); return it == kv.end() ? std::string() : it->second; };

    const double rate = parseDouble(get("rate"), 0.0);
    const double chans = parseDouble(get("channels"), 0.0), frames = parseDouble(get("frames"), -1.0);
    if (rate <= 0.0 || chans < 1.0 || chans > 64.0 || frames < 0.0) {
        error = "The project is damaged (its audio description is missing).";
        return false;
    }
    const size_t nch = static_cast<size_t>(chans), n = static_cast<size_t>(frames);

    ProjectState s;
    s.sourcePath = unescape(get("source"));
    s.viewBlend = static_cast<float>(std::clamp(parseDouble(get("view.blend"), 0.5), 0.0, 1.0));
    s.freqScale = static_cast<int>(parseDouble(get("view.scale"), -1.0));
    s.floorDb = static_cast<float>(parseDouble(get("view.floor"), -110.0));
    s.viewStart = parseDouble(get("view.start"), 0.0);
    s.framesPerPx = parseDouble(get("view.fpp"), 0.0);
    s.cursor = static_cast<size_t>(std::max(0.0, parseDouble(get("cursor"), 0.0)));
    {
        std::istringstream sel(get("selection"));
        sel.imbue(std::locale::classic());
        double a0 = 0, a1 = 0;
        if (sel >> a0 >> a1) {
            s.selStart = static_cast<size_t>(std::max(0.0, a0));
            s.selEnd = static_cast<size_t>(std::max(0.0, a1));
        }
        std::istringstream is(get("box"));
        is.imbue(std::locale::classic());
        int active = 0;
        if (is >> active >> s.boxLoHz >> s.boxHiHz)
            s.boxActive = active != 0;
    }
    s.preview = get("preview") != "0";
    s.chain = defaultChain();
    for (int t = 0; t < static_cast<int>(ToolId::Count); ++t) {
        const std::string k = "tool." + keyName(toolName(static_cast<ToolId>(t)));
        ToolSettings& ts = s.chain[static_cast<size_t>(t)];
        ts.enabled = get(k + ".enabled") == "1";
        overlay(ts.values, parseNumbers(get(k + ".values")));
        ts.noiseProfile = unescape(get(k + ".profile"));
    }
    for (int m = 0; m < static_cast<int>(ModuleId::Count); ++m) {
        auto v = defaultModuleValues(static_cast<ModuleId>(m));
        overlay(v, parseNumbers(get("module." + keyName(moduleInfo(static_cast<ModuleId>(m)).name) + ".values")));
        s.modules.push_back(std::move(v));
    }

    AudioBuffer a;
    a.sampleRate = rate;
    try {
        a.channels.assign(nch, std::vector<float>(n));
    } catch (const std::bad_alloc&) {
        error = "Not enough memory to open this project.";
        return false;
    }
    const size_t chunk = 1 << 18;
    for (size_t c = 0; c < nch; ++c) {
        for (size_t at = 0; at < n; at += chunk) {
            if (cancel != nullptr && cancel->load()) {
                error = "Cancelled.";
                return false;
            }
            const size_t k = std::min(chunk, n - at);
            if (std::fread(a.channels[c].data() + at, sizeof(float), k, in.f) != k) {
                error = "The project is damaged (its audio is cut short).";
                return false;
            }
            if (progress != nullptr)
                progress->store(static_cast<float>((c * static_cast<double>(n) + at + k) / (nch * static_cast<double>(n))));
        }
    }
    audio = std::move(a);
    st = std::move(s);
    return true;
}

} // namespace mend::editor
