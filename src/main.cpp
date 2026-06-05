// SPDX-License-Identifier: MIT OR Unlicense
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kWindowW = 1180;
constexpr int kWindowH = 760;
constexpr SDL_WindowFlags kWindowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED;
constexpr double kPi = 3.14159265358979323846;

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a = 255;
};

constexpr Color kSky{105, 190, 235};
constexpr Color kDeepSky{54, 132, 220};
constexpr Color kDesert{210, 166, 78};
constexpr Color kRock{100, 93, 86};
constexpr Color kBrown{104, 71, 37};
constexpr Color kBlack{0, 0, 0};
constexpr Color kWhite{245, 245, 238};
constexpr Color kPanelLite{37, 52, 78, 255};
constexpr Color kBlue{25, 92, 205};
constexpr Color kWater{47, 190, 240};
constexpr Color kRed{235, 66, 55};
constexpr Color kYellow{255, 224, 83};
constexpr Color kGold{244, 177, 45};
constexpr Color kGreen{72, 188, 116};
constexpr Color kPurple{144, 107, 220};
constexpr Color kInk{22, 20, 18};

double nowSeconds()
{
    static const uint64_t freq = SDL_GetPerformanceFrequency();
    return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(freq);
}

fs::path findFont()
{
    const std::array<fs::path, 8> candidates = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/opt/homebrew/share/fonts/dejavu/DejaVuSans.ttf",
        "/opt/homebrew/share/fonts/liberation-fonts/LiberationSans-Regular.ttf",
    };
    for (const auto &path : candidates) {
        if (fs::exists(path)) return path;
    }
    throw std::runtime_error("no usable TTF font found");
}

double clamp(double value, double lo, double hi)
{
    return std::max(lo, std::min(value, hi));
}

double wrap(double value, double lo, double hi)
{
    const double span = hi - lo;
    while (value < lo) value += span;
    while (value >= hi) value -= span;
    return value;
}

std::optional<fs::path> findFromAncestors(const fs::path &relative)
{
    fs::path path = fs::current_path();
    for (int i = 0; i < 12; ++i) {
        const fs::path candidate = path / relative;
        if (fs::exists(candidate)) return candidate;
        if (!path.has_parent_path() || path == path.parent_path()) break;
        path = path.parent_path();
    }
    return std::nullopt;
}

std::string trim(std::string text)
{
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::string lowerCopy(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string stripDolDoc(std::string text)
{
    std::string out;
    bool inCmd = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i + 1 < text.size() && text[i] == '$' && text[i + 1] == '$') {
            inCmd = !inCmd;
            ++i;
            continue;
        }
        if (!inCmd) out.push_back(text[i]);
    }
    return trim(out);
}

std::vector<std::string> readTextLines(const fs::path &path)
{
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string joinLines(const std::vector<std::string> &lines)
{
    std::string out;
    for (const std::string &line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

class TextAssets {
public:
    void load()
    {
        if (const auto path = findFromAncestors("TempleOS/Misc/Bible.TXT")) {
            bibleLines_ = readTextLines(*path);
            biblePath_ = *path;
        } else if (const auto path = findFromAncestors("TinkerOS/Misc/Bible.TXT")) {
            bibleLines_ = readTextLines(*path);
            biblePath_ = *path;
        }

        if (const auto path = findFromAncestors("TinkerOS/Adam/God/Vocab.DD")) {
            loadVocab(*path);
        } else if (const auto path = findFromAncestors("TempleOS/Adam/God/Vocab.DD")) {
            loadVocab(*path);
        }

        if (const auto path = findFromAncestors("TinkerOS/Adam/God/HSNotes.DD")) {
            loadHelp(*path);
        } else if (const auto path = findFromAncestors("TempleOS/Adam/God/HSNotes.DD")) {
            loadHelp(*path);
        }
    }

    bool hasBible() const
    {
        return !bibleLines_.empty();
    }

    std::vector<std::string> bibleVerse(const std::string &book, const std::string &marker,
                                        int lineCount) const
    {
        if (bibleLines_.empty()) {
            return {book + " " + marker, "Bible text asset not found."};
        }

        const int start = bookStart(book);
        if (start < 0) return {book + " " + marker};

        const std::string needle = marker + " ";
        int found = -1;
        for (int i = start; i < static_cast<int>(bibleLines_.size()); ++i) {
            if (i > start && isBookHeading(bibleLines_[static_cast<size_t>(i)])) break;
            const std::string &line = bibleLines_[static_cast<size_t>(i)];
            if (line.rfind(marker, 0) == 0 || line.find(" " + needle) != std::string::npos ||
                line.find("  " + needle) != std::string::npos) {
                found = i;
                break;
            }
        }
        if (found < 0) return {book + " " + marker};

        std::vector<std::string> out;
        out.push_back(book + " " + marker);
        for (int i = found; i < static_cast<int>(bibleLines_.size()) &&
                            static_cast<int>(out.size()) <= lineCount; ++i) {
            std::string line = trim(bibleLines_[static_cast<size_t>(i)]);
            if (line.empty()) {
                if (!out.empty() && !out.back().empty()) out.push_back("");
                continue;
            }
            out.push_back(line);
        }
        return out;
    }

    std::vector<std::string> randomGodText(std::mt19937 &rng) const
    {
        if (!bibleLines_.empty()) {
            std::uniform_int_distribution<int> coin(0, 1);
            if (coin(rng) == 0) {
                std::vector<int> candidates;
                for (int i = 0; i < static_cast<int>(bibleLines_.size()); ++i) {
                    const std::string line = trim(bibleLines_[static_cast<size_t>(i)]);
                    if (!line.empty() && std::isdigit(static_cast<unsigned char>(line.front())) &&
                        line.find(':') != std::string::npos) {
                        candidates.push_back(i);
                    }
                }
                if (!candidates.empty()) {
                    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
                    const int start = candidates[static_cast<size_t>(pick(rng))];
                    std::vector<std::string> out = {"God Says...", "Bible Passage"};
                    for (int i = start; i < static_cast<int>(bibleLines_.size()) &&
                                        static_cast<int>(out.size()) < 17; ++i) {
                        const std::string line = trim(bibleLines_[static_cast<size_t>(i)]);
                        if (!line.empty()) out.push_back(line);
                    }
                    return out;
                }
            }
        }

        std::vector<std::string> words;
        if (!vocab_.empty()) {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(vocab_.size()) - 1);
            for (int i = 0; i < 16; ++i) words.push_back(vocab_[static_cast<size_t>(pick(rng))]);
        } else {
            words = {"trust", "walk", "wilderness", "water", "cloud", "bread", "mercy", "law",
                     "mountain", "listen", "return", "promise", "camp", "journey", "people", "go"};
        }

        std::vector<std::string> out = {"God Says..."};
        std::string line;
        for (const std::string &word : words) {
            if (line.size() + word.size() + 1 > 46) {
                out.push_back(line);
                line.clear();
            }
            if (!line.empty()) line += ' ';
            line += word;
        }
        if (!line.empty()) out.push_back(line);
        return out;
    }

    std::vector<std::string> helpLines() const
    {
        if (!helpLines_.empty()) return helpLines_;
        return {"The Purpose of Life",
                "Add your own story-line...",
                "Like old school toys."};
    }

private:
    std::vector<std::string> bibleLines_;
    std::vector<std::string> vocab_;
    std::vector<std::string> helpLines_;
    fs::path biblePath_;

    static bool isBookHeading(const std::string &line)
    {
        const std::string lower = lowerCopy(line);
        return lower.find("the ") == 0 && lower.find(" book ") != std::string::npos;
    }

    int bookStart(const std::string &book) const
    {
        const std::string lowerBook = lowerCopy(book);
        for (int i = 0; i < static_cast<int>(bibleLines_.size()); ++i) {
            const std::string lower = lowerCopy(bibleLines_[static_cast<size_t>(i)]);
            if (lower.find(lowerBook) != std::string::npos && isBookHeading(lower)) return i;
        }
        return -1;
    }

    void loadVocab(const fs::path &path)
    {
        for (std::string line : readTextLines(path)) {
            line = trim(line);
            if (line.empty() || line.find('$') != std::string::npos) continue;
            const bool mostlyWord = std::all_of(line.begin(), line.end(), [](unsigned char ch) {
                return std::isalpha(ch) || ch == '\'' || ch == '-';
            });
            if (mostlyWord) vocab_.push_back(line);
        }
    }

    void loadHelp(const fs::path &path)
    {
        for (std::string line : readTextLines(path)) {
            line = stripDolDoc(line);
            if (!line.empty()) helpLines_.push_back(line);
        }
        helpLines_.push_back("");
        helpLines_.push_back("Add your own story-line...");
        helpLines_.push_back("Like old school toys.");
    }
};

struct CampObj {
    double x = 0.0;
    double z = 0.0;
    double dx = 0.0;
    double dz = 0.0;
    bool tent = false;
};

struct Cloud {
    double x = 0.0;
    double y = 0.0;
    double dx = 0.0;
    double scale = 1.0;
    uint32_t seed = 0;
};

struct Quail {
    double x = 0.0;
    double y = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    double phase = 0.0;
    bool dead = false;
};

struct HorebObj {
    double x = 0.0;
    double z = 0.0;
    int bi = 1;
    int seed = 0;
};

struct HorebProjection {
    bool visible = false;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    double depth = 0.0;
    double side = 0.0;
};

enum class Mode {
    Camp,
    God,
    Clouds,
    Court,
    Map,
    WaterRock,
    Battle,
    Quail,
    Comics,
    Help,
};

enum class Flow {
    CampWatch,
    Menu,
    Scene,
};

enum class Action {
    BreakCamp,
    God,
    Clouds,
    Court,
    Map,
    WaterRock,
    Battle,
    Quail,
    Comics,
    Help,
    Quit,
};

enum class GodStage {
    Climb,
    Horeb,
    Talking,
};

struct MenuButton {
    SDL_FRect rect;
    std::string label;
    Action action;
};

struct Tone {
    double hz = 440.0;
    double seconds = 0.18;
};

class AudioEngine {
public:
    ~AudioEngine()
    {
        shutdown();
    }

    void init()
    {
        SDL_AudioSpec spec{SDL_AUDIO_F32, 1, sampleRate_};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_) return;
        SDL_ResumeAudioStreamDevice(stream_);
    }

    void shutdown()
    {
        if (stream_) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
    }

    void toggleMuted()
    {
        muted_ = !muted_;
        if (muted_ && stream_) SDL_ClearAudioStream(stream_);
    }

    bool muted() const
    {
        return muted_;
    }

    void cue(double hz, double seconds)
    {
        cueFreq_ = hz;
        cueRemaining_ = std::max(cueRemaining_, seconds);
    }

    void pump(Mode mode, double)
    {
        if (!stream_ || muted_) return;
        const int queued = SDL_GetAudioStreamQueued(stream_);
        const int targetBytes = static_cast<int>(sampleRate_ * sizeof(float) * 0.12);
        if (queued >= targetBytes) return;

        constexpr int frames = 2048;
        samples_.assign(frames, 0.0f);
        const std::vector<Tone> &melody = melodyFor(mode);
        if (noteIndex_ >= melody.size()) {
            noteIndex_ = 0;
            noteRemaining_ = 0.0;
        }
        for (int i = 0; i < frames; ++i) {
            const double step = 1.0 / static_cast<double>(sampleRate_);
            if (noteRemaining_ <= 0.0) {
                noteIndex_ = (noteIndex_ + 1) % melody.size();
                noteRemaining_ = melody[noteIndex_].seconds;
            }
            noteRemaining_ -= step;
            const double hz = melody[noteIndex_].hz;
            melodyPhase_ = wrap(melodyPhase_ + hz * step, 0.0, 1.0);
            double sample = std::sin(melodyPhase_ * 2.0 * kPi) * 0.026;

            if (cueRemaining_ > 0.0) {
                cuePhase_ = wrap(cuePhase_ + cueFreq_ * step, 0.0, 1.0);
                const double envelope = clamp(cueRemaining_ / 0.18, 0.0, 1.0);
                sample += std::sin(cuePhase_ * 2.0 * kPi) * 0.11 * envelope;
                cueRemaining_ = std::max(0.0, cueRemaining_ - step);
            }

            samples_[static_cast<size_t>(i)] = static_cast<float>(clamp(sample, -0.25, 0.25));
        }
        SDL_PutAudioStreamData(stream_, samples_.data(),
                               static_cast<int>(samples_.size() * sizeof(float)));
    }

private:
    static double noteFrequency(char note, int octave)
    {
        int semitone = 0;
        switch (note) {
        case 'C': semitone = 0; break;
        case 'D': semitone = 2; break;
        case 'E': semitone = 4; break;
        case 'F': semitone = 5; break;
        case 'G': semitone = 7; break;
        case 'A': semitone = 9; break;
        case 'B': semitone = 11; break;
        default: break;
        }
        const int midi = (octave + 1) * 12 + semitone;
        return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    }

    static double templeDuration(char ch)
    {
        switch (ch) {
        case 'w': return 1.10;
        case 'h': return 0.72;
        case 'q': return 0.36;
        case 'e': return 0.18;
        case 't': return 0.12;
        case 's': return 0.09;
        default: return 0.18;
        }
    }

    static std::vector<Tone> parseTempleSong(std::initializer_list<const char *> lines)
    {
        std::vector<Tone> notes;
        int octave = 4;
        double duration = 0.18;
        for (const char *line : lines) {
            for (const char *p = line; *p; ++p) {
                const char ch = *p;
                if (ch >= '0' && ch <= '8') {
                    octave = ch - '0';
                } else if (ch == 'w' || ch == 'h' || ch == 'q' ||
                           ch == 'e' || ch == 't' || ch == 's') {
                    duration = templeDuration(ch);
                } else if (ch >= 'A' && ch <= 'G') {
                    notes.push_back({noteFrequency(ch, octave), duration});
                }
            }
        }
        if (notes.empty()) notes = {{196.0, 0.18}, {246.94, 0.18}, {293.66, 0.18}, {392.0, 0.18}};
        return notes;
    }

    static std::vector<Tone> tones(std::initializer_list<double> hz)
    {
        std::vector<Tone> out;
        out.reserve(hz.size());
        for (double note : hz) out.push_back({note, 0.18});
        return out;
    }

    const std::vector<Tone> &melodyFor(Mode mode) const
    {
        static const std::vector<Tone> afterEgyptSong = parseTempleSong({
            "5eGFqG4etB5EF4eB5EEF4etB5DCeCFqF",
            "5eGFqG4etB5EF4eB5EEF4etB5DCeCFqF",
            "4A5etD4AAqB5D4eB5EqEetECDG4B5G",
            "4qA5etD4AAqB5D4eB5EqEetECDG4B5G",
        });
        static const std::vector<Tone> clouds = parseTempleSong({
            "4qB5etD4AG5qD4sG5E4G5EetCEDqFEeDC",
            "4qB5etD4AG5qD4sG5E4G5EetCEDqFEeDC",
            "5CGqD4eA5DsDCDCqGEetD4A5D4sG5D4G5D",
            "5eCGqD4eA5DsDCDCqGEetD4A5D4sG5D4G5D",
        });
        static const std::vector<Tone> battle = tones({220.0, 246.94, 261.63, 293.66, 261.63, 246.94, 220.0, 196.0});
        static const std::vector<Tone> water = tones({293.66, 369.99, 440.0, 587.33, 440.0, 369.99, 293.66, 246.94});
        static const std::vector<Tone> quail = tones({392.0, 392.0, 440.0, 493.88, 523.25, 493.88, 440.0, 392.0});
        static const std::vector<Tone> god = tones({261.63, 329.63, 392.0, 523.25, 392.0, 329.63, 293.66, 349.23});
        switch (mode) {
        case Mode::Clouds: return clouds;
        case Mode::Battle: return battle;
        case Mode::WaterRock: return water;
        case Mode::Quail: return quail;
        case Mode::God: return god;
        default: return afterEgyptSong;
        }
    }

    SDL_AudioStream *stream_ = nullptr;
    int sampleRate_ = 48000;
    bool muted_ = false;
    size_t noteIndex_ = 0;
    double noteRemaining_ = 0.0;
    double melodyPhase_ = 0.0;
    double cuePhase_ = 0.0;
    double cueFreq_ = 440.0;
    double cueRemaining_ = 0.0;
    std::vector<float> samples_;
};

struct SpriteImage {
    int width = 0;
    int height = 0;
    int originX = 0;
    int originY = 0;
    std::vector<uint32_t> pixels;
};

struct TempleSprite {
    int width = 0;
    int height = 0;
    int originX = 0;
    int originY = 0;
    SDL_Texture *texture = nullptr;
};

SDL_FColor fcolor(Color c)
{
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

class TempleSprites {
public:
    void load(SDL_Renderer *renderer)
    {
        renderer_ = renderer;
        const std::optional<fs::path> root = findAfterEgyptDir();
        if (!root) return;

        const std::array<std::string, 8> files = {
            "Camp.HC", "WaterRock.HC", "Battle.HC", "Quail.HC",
            "Mountain.HC", "GodTalking.HC", "AfterEgypt.HC", "HorebA.HC",
        };
        for (const auto &file : files) loadDoc(*root / file, file);
        loadDoc(*root / "AESplash.DD", "AESplash.DD");

        const fs::path comics = *root / "Comics";
        const std::array<std::string, 7> comicFiles = {
            "Moses01.DD", "Moses02.DD", "Moses04.DD", "Moses05.DD",
            "Moses06.DD", "Moses07.DD", "Moses08.DD",
        };
        for (const auto &file : comicFiles) loadDoc(comics / file, "Comics/" + file);
    }

    void clear()
    {
        for (auto &[_, sprite] : sprites_) {
            if (sprite.texture) SDL_DestroyTexture(sprite.texture);
        }
        sprites_.clear();
    }

    bool draw(const std::string &file, int bi, float x, float y, float scale = 1.0f) const
    {
        const auto it = sprites_.find(key(file, bi));
        if (it == sprites_.end() || !it->second.texture) return false;
        const TempleSprite &sprite = it->second;
        SDL_FRect dst{
            x + sprite.originX * scale,
            y + sprite.originY * scale,
            sprite.width * scale,
            sprite.height * scale,
        };
        SDL_RenderTexture(renderer_, sprite.texture, nullptr, &dst);
        return true;
    }

    bool has(const std::string &file, int bi) const
    {
        return sprites_.contains(key(file, bi));
    }

    bool drawCentered(const std::string &file, int bi, float cx, float cy,
                      float maxW, float maxH) const
    {
        const auto it = sprites_.find(key(file, bi));
        if (it == sprites_.end() || !it->second.texture) return false;
        const TempleSprite &sprite = it->second;
        const float scale = std::min(maxW / std::max(1, sprite.width),
                                     maxH / std::max(1, sprite.height));
        const float anchorX = cx - (sprite.originX + sprite.width * 0.5f) * scale;
        const float anchorY = cy - (sprite.originY + sprite.height * 0.5f) * scale;
        return draw(file, bi, anchorX, anchorY, scale);
    }

private:
    struct Bounds {
        int minX = 1000000;
        int minY = 1000000;
        int maxX = -1000000;
        int maxY = -1000000;
    };

    SDL_Renderer *renderer_ = nullptr;
    std::map<std::string, TempleSprite> sprites_;

    static std::string key(const std::string &file, int bi)
    {
        return file + ":" + std::to_string(bi);
    }

    static std::optional<fs::path> findAfterEgyptDir()
    {
        fs::path path = fs::current_path();
        for (int i = 0; i < 10; ++i) {
            const fs::path candidate = path / "TinkerOS" / "Apps" / "AfterEgypt";
            if (fs::exists(candidate / "Camp.HC")) return candidate;
            if (!path.has_parent_path() || path == path.parent_path()) break;
            path = path.parent_path();
        }
        return std::nullopt;
    }

    static uint32_t readU32(const std::vector<uint8_t> &bytes, size_t off)
    {
        if (off + 4 > bytes.size()) return 0;
        return static_cast<uint32_t>(bytes[off]) |
               static_cast<uint32_t>(bytes[off + 1]) << 8 |
               static_cast<uint32_t>(bytes[off + 2]) << 16 |
               static_cast<uint32_t>(bytes[off + 3]) << 24;
    }

    static int32_t readI32(const std::vector<uint8_t> &bytes, size_t off)
    {
        return static_cast<int32_t>(readU32(bytes, off));
    }

    static size_t elemSize(const std::vector<uint8_t> &data, size_t p)
    {
        if (p >= data.size()) return 0;
        const uint8_t type = data[p] & 0x7F;
        switch (type) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 3;
        case 3: return 5;
        case 4:
        case 10:
        case 12:
        case 26: return 17;
        case 5:
        case 6: return 1;
        case 7:
        case 8:
        case 21:
        case 22: return 9;
        case 14: return 13;
        case 13:
        case 15: return 25;
        case 16: return 29;
        case 11: {
            const int32_t n = readI32(data, p + 1);
            return n < 0 ? 0 : 5 + static_cast<size_t>(n) * 8;
        }
        case 17:
        case 18:
        case 19:
        case 20: {
            const int32_t n = readI32(data, p + 1);
            return n < 0 ? 0 : 5 + static_cast<size_t>(n) * 12;
        }
        case 23: {
            const int32_t w = readI32(data, p + 9);
            const int32_t h = readI32(data, p + 13);
            if (w < 0 || h < 0) return 0;
            return 17 + static_cast<size_t>((w + 7) & ~7) * static_cast<size_t>(h);
        }
        case 27:
        case 28:
        case 29: {
            size_t e = p + 9;
            while (e < data.size() && data[e] != 0) ++e;
            return e < data.size() ? e - p + 1 : 0;
        }
        default:
            return 0;
        }
    }

    static void addPoint(Bounds &bounds, int x, int y, int pad = 0)
    {
        bounds.minX = std::min(bounds.minX, x - pad);
        bounds.minY = std::min(bounds.minY, y - pad);
        bounds.maxX = std::max(bounds.maxX, x + pad);
        bounds.maxY = std::max(bounds.maxY, y + pad);
    }

    static void addRect(Bounds &bounds, int x1, int y1, int x2, int y2, int pad = 0)
    {
        addPoint(bounds, std::min(x1, x2), std::min(y1, y2), pad);
        addPoint(bounds, std::max(x1, x2), std::max(y1, y2), pad);
    }

    static std::array<Color, 16> palette()
    {
        return {{
            {0, 0, 0},       {0, 0, 170},     {0, 170, 0},     {0, 170, 170},
            {170, 0, 0},     {170, 0, 170},   {170, 85, 0},    {170, 170, 170},
            {85, 85, 85},    {85, 85, 255},   {85, 255, 85},   {85, 255, 255},
            {255, 85, 85},   {255, 85, 255},  {255, 255, 85},  {255, 255, 255},
        }};
    }

    void loadDoc(const fs::path &path, const std::string &name)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        const std::vector<char> raw((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
        const std::vector<uint8_t> bytes(raw.begin(), raw.end());
        const auto nul = std::find(bytes.begin(), bytes.end(), 0);
        if (nul == bytes.end()) return;

        size_t p = static_cast<size_t>(std::distance(bytes.begin(), nul)) + 1;
        while (p + 16 <= bytes.size()) {
            const uint32_t num = readU32(bytes, p);
            const uint32_t size = readU32(bytes, p + 8);
            if (size == 0 || p + 16 + size > bytes.size()) break;

            std::vector<uint8_t> data(bytes.begin() + static_cast<std::ptrdiff_t>(p + 16),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(p + 16 + size));
            if (SpriteImage image = renderSprite(data); image.width > 0 && image.height > 0) {
                SDL_Texture *texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                                         SDL_TEXTUREACCESS_STATIC,
                                                         image.width, image.height);
                if (texture) {
                    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
                    SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * 4);
                    sprites_[key(name, static_cast<int>(num))] = {
                        image.width, image.height, image.originX, image.originY, texture};
                }
            }
            p += 16 + size;
        }
    }

    static SpriteImage renderSprite(const std::vector<uint8_t> &data)
    {
        Bounds bounds;
        int shiftX = 0;
        int shiftY = 0;
        int thick = 1;

        for (size_t p = 0; p < data.size();) {
            const uint8_t type = data[p] & 0x7F;
            const size_t size = elemSize(data, p);
            if (size == 0 || p + size > data.size()) break;
            if (type == 0) break;

            switch (type) {
            case 3:
                thick = std::max(1, readI32(data, p + 1));
                break;
            case 7:
                shiftX += readI32(data, p + 1);
                shiftY += readI32(data, p + 5);
                break;
            case 8:
            case 21:
            case 22:
                addPoint(bounds, shiftX + readI32(data, p + 1), shiftY + readI32(data, p + 5), thick);
                break;
            case 10:
            case 12:
            case 26:
                addRect(bounds, shiftX + readI32(data, p + 1), shiftY + readI32(data, p + 5),
                        shiftX + readI32(data, p + 9), shiftY + readI32(data, p + 13), thick);
                break;
            case 11: {
                const int32_t n = readI32(data, p + 1);
                for (int i = 0; i < n; ++i) {
                    const size_t off = p + 5 + static_cast<size_t>(i) * 8;
                    addPoint(bounds, shiftX + readI32(data, off), shiftY + readI32(data, off + 4), thick);
                }
                break;
            }
            case 14: {
                const int x = shiftX + readI32(data, p + 1);
                const int y = shiftY + readI32(data, p + 5);
                const int r = readI32(data, p + 9);
                addRect(bounds, x - r, y - r, x + r, y + r, thick);
                break;
            }
            case 17:
            case 18:
            case 19:
            case 20: {
                const int32_t n = readI32(data, p + 1);
                for (int i = 0; i < n; ++i) {
                    const size_t off = p + 5 + static_cast<size_t>(i) * 12;
                    addPoint(bounds, shiftX + readI32(data, off), shiftY + readI32(data, off + 4), thick);
                }
                break;
            }
            case 23: {
                const int x = shiftX + readI32(data, p + 1);
                const int y = shiftY + readI32(data, p + 5);
                const int w = readI32(data, p + 9);
                const int h = readI32(data, p + 13);
                addRect(bounds, x, y, x + w - 1, y + h - 1);
                break;
            }
            case 27:
            case 28:
            case 29:
                addRect(bounds, shiftX + readI32(data, p + 1), shiftY + readI32(data, p + 5),
                        shiftX + readI32(data, p + 1) + static_cast<int>(size) * 8,
                        shiftY + readI32(data, p + 5) + 16);
                break;
            default:
                break;
            }
            p += size;
        }

        if (bounds.maxX < bounds.minX || bounds.maxY < bounds.minY) return {};

        SpriteImage image;
        image.originX = bounds.minX;
        image.originY = bounds.minY;
        image.width = bounds.maxX - bounds.minX + 1;
        image.height = bounds.maxY - bounds.minY + 1;
        std::vector<uint8_t> canvas(static_cast<size_t>(image.width) * image.height, 0xFF);

        auto plot = [&](int x, int y, uint8_t color, int pen = 1) {
            if (color == 0xFF) return;
            const int r = std::max(0, pen / 2);
            for (int yy = y - r; yy <= y + r; ++yy) {
                for (int xx = x - r; xx <= x + r; ++xx) {
                    const int cx = xx - image.originX;
                    const int cy = yy - image.originY;
                    if (cx >= 0 && cy >= 0 && cx < image.width && cy < image.height) {
                        canvas[static_cast<size_t>(cy) * image.width + cx] = color;
                    }
                }
            }
        };

        auto drawLine = [&](int x1, int y1, int x2, int y2, uint8_t color, int pen) {
            int dx = std::abs(x2 - x1);
            int sx = x1 < x2 ? 1 : -1;
            int dy = -std::abs(y2 - y1);
            int sy = y1 < y2 ? 1 : -1;
            int err = dx + dy;
            while (true) {
                plot(x1, y1, color, pen);
                if (x1 == x2 && y1 == y2) break;
                const int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x1 += sx; }
                if (e2 <= dx) { err += dx; y1 += sy; }
            }
        };

        auto drawCircle = [&](int cx, int cy, int r, uint8_t color, int pen) {
            constexpr int steps = 96;
            int px = cx + r;
            int py = cy;
            for (int i = 1; i <= steps; ++i) {
                const double a = 2.0 * kPi * i / steps;
                const int x = cx + static_cast<int>(std::lround(std::cos(a) * r));
                const int y = cy + static_cast<int>(std::lround(std::sin(a) * r));
                drawLine(px, py, x, y, color, pen);
                px = x;
                py = y;
            }
        };

        shiftX = 0;
        shiftY = 0;
        thick = 1;
        uint8_t color = 15;
        for (size_t p = 0; p < data.size();) {
            const uint8_t type = data[p] & 0x7F;
            const size_t size = elemSize(data, p);
            if (size == 0 || p + size > data.size()) break;
            if (type == 0) break;

            switch (type) {
            case 1:
                color = data[p + 1];
                break;
            case 2:
                color = static_cast<uint8_t>(data[p + 1] & 0x0F);
                break;
            case 3:
                thick = std::max(1, readI32(data, p + 1));
                break;
            case 7:
                shiftX += readI32(data, p + 1);
                shiftY += readI32(data, p + 5);
                break;
            case 8:
                plot(shiftX + readI32(data, p + 1), shiftY + readI32(data, p + 5), color, thick);
                break;
            case 10:
            case 26:
                drawLine(shiftX + readI32(data, p + 1), shiftY + readI32(data, p + 5),
                         shiftX + readI32(data, p + 9), shiftY + readI32(data, p + 13), color, thick);
                break;
            case 12: {
                const int x1 = shiftX + readI32(data, p + 1);
                const int y1 = shiftY + readI32(data, p + 5);
                const int x2 = shiftX + readI32(data, p + 9);
                const int y2 = shiftY + readI32(data, p + 13);
                for (int y = std::min(y1, y2); y <= std::max(y1, y2); ++y)
                    for (int x = std::min(x1, x2); x <= std::max(x1, x2); ++x)
                        plot(x, y, color);
                break;
            }
            case 11: {
                const int32_t n = readI32(data, p + 1);
                if (n > 1) {
                    int px = shiftX + readI32(data, p + 5);
                    int py = shiftY + readI32(data, p + 9);
                    for (int i = 1; i < n; ++i) {
                        const size_t off = p + 5 + static_cast<size_t>(i) * 8;
                        const int x = shiftX + readI32(data, off);
                        const int y = shiftY + readI32(data, off + 4);
                        drawLine(px, py, x, y, color, thick);
                        px = x;
                        py = y;
                    }
                }
                break;
            }
            case 14:
                drawCircle(shiftX + readI32(data, p + 1), shiftY + readI32(data, p + 5),
                           readI32(data, p + 9), color, thick);
                break;
            case 17:
            case 18:
            case 19:
            case 20: {
                const int32_t n = readI32(data, p + 1);
                if (n > 1) {
                    int firstX = shiftX + readI32(data, p + 5);
                    int firstY = shiftY + readI32(data, p + 9);
                    int px = firstX;
                    int py = firstY;
                    for (int i = 1; i < n; ++i) {
                        const size_t off = p + 5 + static_cast<size_t>(i) * 12;
                        const int x = shiftX + readI32(data, off);
                        const int y = shiftY + readI32(data, off + 4);
                        drawLine(px, py, x, y, color, thick);
                        px = x;
                        py = y;
                    }
                    if (type == 18 || type == 20) {
                        drawLine(px, py, firstX, firstY, color, thick);
                    }
                }
                break;
            }
            case 23: {
                const int x = shiftX + readI32(data, p + 1);
                const int y = shiftY + readI32(data, p + 5);
                const int w = readI32(data, p + 9);
                const int h = readI32(data, p + 13);
                const int wi = (w + 7) & ~7;
                const size_t body = p + 17;
                for (int yy = 0; yy < h; ++yy) {
                    for (int xx = 0; xx < w; ++xx) {
                        plot(x + xx, y + yy, data[body + static_cast<size_t>(yy) * wi + xx]);
                    }
                }
                break;
            }
            default:
                break;
            }
            p += size;
        }

        const auto pal = palette();
        image.pixels.resize(canvas.size());
        for (size_t i = 0; i < canvas.size(); ++i) {
            const uint8_t c = canvas[i];
            if (c == 0xFF) {
                image.pixels[i] = 0x00000000;
            } else {
                const Color rgb = pal[c & 0x0F];
                image.pixels[i] = 0xFF000000u |
                                  static_cast<uint32_t>(rgb.r) << 16 |
                                  static_cast<uint32_t>(rgb.g) << 8 |
                                  static_cast<uint32_t>(rgb.b);
            }
        }
        return image;
    }
};

class AfterEgyptApp {
public:
    AfterEgyptApp()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_InitSubSystem(SDL_INIT_AUDIO);
        if (!TTF_Init()) {
            throw std::runtime_error(SDL_GetError());
        }
        if (!SDL_CreateWindowAndRenderer("AfterEgypt SDL3", kWindowW, kWindowH,
                                         kWindowFlags, &window_, &renderer_)) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

        const fs::path font = findFont();
        font_ = TTF_OpenFont(font.string().c_str(), 17.0f);
        smallFont_ = TTF_OpenFont(font.string().c_str(), 14.0f);
        titleFont_ = TTF_OpenFont(font.string().c_str(), 32.0f);
        if (!font_ || !smallFont_ || !titleFont_) {
            throw std::runtime_error(SDL_GetError());
        }

        sprites_.load(renderer_);
        textAssets_.load();
        audio_.init();
        resetAll();
        introStart_ = nowSeconds();
        showIntro_ = true;
    }

    ~AfterEgyptApp()
    {
        audio_.shutdown();
        sprites_.clear();
        if (titleFont_) TTF_CloseFont(titleFont_);
        if (smallFont_) TTF_CloseFont(smallFont_);
        if (font_) TTF_CloseFont(font_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        TTF_Quit();
        SDL_Quit();
    }

    int run(int maxFrames = 0)
    {
        bool running = true;
        int frames = 0;
        double last = nowSeconds();

        while (running) {
            const double t = nowSeconds();
            const double dt = clamp(t - last, 0.0, 0.05);
            last = t;

            handleEvents(running);
            if (quitRequested_) running = false;
            update(dt, t);
            audio_.pump(showIntro_ ? Mode::Camp : mode_, dt);
            render(t);

            if (maxFrames > 0 && ++frames >= maxFrames) {
                running = false;
            }
            SDL_Delay(10);
        }

        return 0;
    }

    int smokeScenes()
    {
        if (!textAssets_.hasBible()) {
            std::cerr << "after-egypt-sdl3: Bible.TXT asset not found\n";
            return 2;
        }
        const std::array<fs::path, 5> required = {
            "TinkerOS/Apps/AfterEgypt/Camp.HC",
            "TinkerOS/Apps/AfterEgypt/WaterRock.HC",
            "TinkerOS/Apps/AfterEgypt/Battle.HC",
            "TinkerOS/Apps/AfterEgypt/Quail.HC",
            "TinkerOS/Apps/AfterEgypt/HorebA.HC",
        };
        for (const auto &rel : required) {
            if (!findFromAncestors(rel)) {
                std::cerr << "after-egypt-sdl3: missing asset " << rel << '\n';
                return 2;
            }
        }

        showIntro_ = false;
        const auto frame = [&] {
            const double t = nowSeconds();
            update(1.0 / 60.0, t);
            audio_.pump(mode_, 1.0 / 60.0);
            render(t);
            SDL_Delay(5);
        };

        flow_ = Flow::CampWatch;
        frame();
        flow_ = Flow::Menu;
        frame();

        const std::array<Mode, 8> modes = {
            Mode::Clouds, Mode::Court, Mode::Map, Mode::WaterRock,
            Mode::Battle, Mode::Quail, Mode::Comics, Mode::Help,
        };
        for (Mode mode : modes) {
            startScene(mode);
            frame();
            if (mode == Mode::Quail) {
                beginQuailAnimation();
                frame();
            }
            if (mode == Mode::Comics) {
                comicPicker_ = false;
                frame();
            }
        }

        startScene(Mode::God);
        godStage_ = GodStage::Climb;
        frame();
        godStage_ = GodStage::Horeb;
        frame();
        godStage_ = GodStage::Talking;
        godTextLines_ = textAssets_.randomGodText(rng_);
        frame();
        return 0;
    }

private:
    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    TTF_Font *font_ = nullptr;
    TTF_Font *smallFont_ = nullptr;
    TTF_Font *titleFont_ = nullptr;
    TempleSprites sprites_;
    TextAssets textAssets_;
    AudioEngine audio_;

    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<double> unit_{0.0, 1.0};
    Flow flow_ = Flow::CampWatch;
    Mode mode_ = Mode::Camp;
    int day_ = 0;
    int people_ = 100;

    std::vector<CampObj> camp_;
    std::vector<Cloud> clouds_;
    std::vector<SDL_FPoint> mapPath_;
    std::vector<Quail> quail_;
    std::vector<HorebObj> horebObjs_;
    std::vector<std::string> comicFiles_;
    std::vector<std::string> godTextLines_;
    std::string courtPrompt_;
    std::string courtResult_;
    bool showIntro_ = true;
    bool quitRequested_ = false;
    double introStart_ = 0.0;
    double sceneStart_ = 0.0;
    double campStart_ = 0.0;
    int campCalfCycle_ = -1;
    bool campShowCalf_ = false;
    double mountainStart_ = 0.0;
    GodStage godStage_ = GodStage::Climb;
    double godStageStart_ = 0.0;
    double horebAngle_ = 0.0;
    double horebX_ = 0.0;
    double horebZ_ = 0.0;
    bool horebFound_ = false;
    double horebFoundTime_ = 0.0;
    double mapAngle_ = 0.0;
    double mapStepDue_ = 0.0;
    double waterDownTime_ = -1.0;
    double waterUpTime_ = -1.0;
    double waterAutoReleaseAt_ = -1.0;
    bool waterDownStroke_ = false;
    double battleT0_ = 0.0;
    double battleLast_ = 0.0;
    double battleTT_ = 0.0;
    double battleShift_ = 0.0;
    bool battleHeld_ = false;
    bool quailReading_ = true;
    bool comicPicker_ = true;
    int docScroll_ = 0;
    int comicIndex_ = 0;

    double random()
    {
        return unit_(rng_);
    }

    double range(double lo, double hi)
    {
        return lo + (hi - lo) * random();
    }

    int irange(int lo, int hi)
    {
        return lo + static_cast<int>(random() * (hi - lo + 1));
    }

    void resetAll()
    {
        day_ = 0;
        people_ = 100;
        initClouds();
        initMap();
        initQuail();
        initHoreb();
        initComics();
        generateCourt();
        resetWater();
        resetBattle();
        startTurn();
        mountainStart_ = nowSeconds();
        godStageStart_ = mountainStart_;
        godStage_ = GodStage::Climb;
    }

    void startTurn()
    {
        ++day_;
        const double growth = 1.0 + 0.01 * static_cast<double>(irange(-30, 69));
        people_ = static_cast<int>(clamp(std::round(people_ * growth), 100.0, 1024.0));
        initCamp();
        flow_ = Flow::CampWatch;
        mode_ = Mode::Camp;
        campStart_ = nowSeconds();
        campCalfCycle_ = -1;
        campShowCalf_ = false;
        docScroll_ = 0;
        audio_.cue(392.0, 0.10);
    }

    void initCamp()
    {
        camp_.clear();
        const int tents = std::max(4, (people_ + 39) / 40);
        const int personCount = std::min(people_, 1024);

        for (int i = 0; i < personCount; ++i) {
            CampObj obj;
            obj.x = range(-300.0, 300.0);
            obj.z = range(-610.0, 0.0);
            obj.dx = range(-16.0, 16.0);
            obj.dz = range(-16.0, 16.0);
            obj.tent = false;
            camp_.push_back(obj);
        }

        for (int i = 0; i < tents; ++i) {
            CampObj obj;
            obj.x = range(-285.0, 285.0);
            obj.z = range(-585.0, -30.0);
            obj.tent = true;
            camp_.push_back(obj);
        }
    }

    void initClouds()
    {
        clouds_.clear();
        for (int i = 0; i < 16; ++i) {
            Cloud cloud;
            cloud.x = range(-80.0, 880.0);
            cloud.y = range(50.0, 260.0);
            cloud.dx = range(8.0, 36.0);
            cloud.scale = range(0.65, 1.45);
            cloud.seed = static_cast<uint32_t>(irange(1, 1000000));
            clouds_.push_back(cloud);
        }
    }

    void initMap()
    {
        mapPath_.clear();
        mapPath_.push_back({0.0f, 0.0f});
        mapAngle_ = -0.4;
        mapStepDue_ = 0.0;
    }

    void initQuail()
    {
        quail_.clear();
        for (int i = 0; i < 128; ++i) {
            Quail q;
            q.x = range(-60.0, 820.0);
            q.y = range(34.0, 300.0);
            q.dx = range(18.0, 70.0);
            q.dy = range(-24.0, 24.0);
            q.phase = range(0.0, 1.0);
            q.dead = false;
            quail_.push_back(q);
        }
    }

    void initHoreb()
    {
        horebObjs_.clear();
        horebAngle_ = 0.0;
        horebX_ = 0.0;
        horebZ_ = 0.0;
        horebFound_ = false;
        horebFoundTime_ = 0.0;

        horebObjs_.push_back({640.0, 1820.0, 2, 0});
        for (int i = 1; i < 180; ++i) {
            const double lane = (random() < 0.5 ? -1.0 : 1.0) * range(170.0, 1850.0);
            const double z = range(180.0, 3900.0);
            int bi = 1;
            const double r = random();
            if (r < 0.34) bi = 1;
            else if (r < 0.54) bi = 2;
            else if (r < 0.72) bi = 3;
            else if (r < 0.88) bi = 4;
            else bi = irange(6, 8);
            horebObjs_.push_back({lane, z, bi, i});
        }
    }

    void initComics()
    {
        comicFiles_ = {
            "Comics/Moses01.DD",
            "Comics/Moses02.DD",
            "Comics/Moses04.DD",
            "Comics/Moses05.DD",
            "Comics/Moses06.DD",
            "Comics/Moses07.DD",
            "Comics/Moses08.DD",
        };
    }

    void breakCamp()
    {
        startTurn();
    }

    void restartCurrent()
    {
        if (flow_ == Flow::CampWatch) {
            initCamp();
            campStart_ = nowSeconds();
            return;
        }
        if (flow_ == Flow::Menu) return;
        switch (mode_) {
        case Mode::Camp: startTurn(); break;
        case Mode::God: startGod(); break;
        case Mode::Clouds: initClouds(); break;
        case Mode::Court: generateCourt(); break;
        case Mode::Map: initMap(); break;
        case Mode::WaterRock: resetWater(); break;
        case Mode::Battle: resetBattle(); break;
        case Mode::Quail: initQuail(); quailReading_ = true; docScroll_ = 0; break;
        case Mode::Comics: comicIndex_ = 0; comicPicker_ = true; break;
        case Mode::Help: docScroll_ = 0; break;
        }
        audio_.cue(523.25, 0.08);
    }

    void startScene(Mode mode)
    {
        flow_ = Flow::Scene;
        mode_ = mode;
        sceneStart_ = nowSeconds();
        docScroll_ = 0;
        if (mode_ == Mode::God) startGod();
        if (mode_ == Mode::Clouds) initClouds();
        if (mode_ == Mode::Court) generateCourt();
        if (mode_ == Mode::Map) initMap();
        if (mode_ == Mode::WaterRock) resetWater();
        if (mode_ == Mode::Battle) resetBattle();
        if (mode_ == Mode::Quail) {
            initQuail();
            quailReading_ = true;
        }
        if (mode_ == Mode::Comics) {
            comicPicker_ = true;
            comicIndex_ = 0;
        }
        audio_.cue(440.0, 0.05);
    }

    void finishScene()
    {
        startTurn();
    }

    void startGod()
    {
        mountainStart_ = nowSeconds();
        godStageStart_ = mountainStart_;
        godStage_ = GodStage::Climb;
        godTextLines_.clear();
        initHoreb();
    }

    void resetWater()
    {
        waterDownTime_ = -1.0;
        waterUpTime_ = -1.0;
        waterAutoReleaseAt_ = -1.0;
        waterDownStroke_ = false;
    }

    void resetBattle()
    {
        battleT0_ = nowSeconds();
        battleLast_ = 0.0;
        battleTT_ = 0.0;
        battleShift_ = 0.0;
        battleHeld_ = false;
    }

    void perform(Action action)
    {
        switch (action) {
        case Action::BreakCamp: breakCamp(); break;
        case Action::God: startScene(Mode::God); break;
        case Action::Clouds: startScene(Mode::Clouds); break;
        case Action::Court: startScene(Mode::Court); break;
        case Action::Map: startScene(Mode::Map); break;
        case Action::WaterRock: startScene(Mode::WaterRock); break;
        case Action::Battle: startScene(Mode::Battle); break;
        case Action::Quail: startScene(Mode::Quail); break;
        case Action::Comics: startScene(Mode::Comics); break;
        case Action::Help: startScene(Mode::Help); break;
        case Action::Quit: quitRequested_ = true; break;
        }
    }

    void handleEvents(bool &running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (showIntro_) {
                    showIntro_ = false;
                    audio_.cue(659.25, 0.10);
                    continue;
                }
                onMouseDown(event.button.x, event.button.y);
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (flow_ == Flow::Scene && mode_ == Mode::Battle) releaseBattle();
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (showIntro_) {
                    showIntro_ = false;
                    audio_.cue(659.25, 0.10);
                    continue;
                }
                onKeyDown(event.key.key, event.key.scancode, event.key.repeat, running);
            } else if (event.type == SDL_EVENT_KEY_UP) {
                if (event.key.scancode == SDL_SCANCODE_SPACE) {
                    if (flow_ == Flow::Scene && mode_ == Mode::Battle) releaseBattle();
                    if (flow_ == Flow::Scene && mode_ == Mode::WaterRock) releaseWaterStrike();
                }
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                onWheel(event.wheel.y);
            }
        }
    }

    void onMouseDown(float x, float y)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window_, &w, &h);

        if (flow_ == Flow::CampWatch) {
            openMenu();
            return;
        }

        if (flow_ == Flow::Menu) {
            for (const auto &button : layoutButtons(w, h)) {
                if (pointIn(button.rect, x, y)) {
                    perform(button.action);
                    return;
                }
            }
            return;
        }

        if (mode_ == Mode::WaterRock) {
            quickWaterStrike();
        } else if (mode_ == Mode::God && godStage_ == GodStage::Horeb) {
            audio_.cue(293.66, 0.04);
        } else if (mode_ == Mode::Battle) {
            holdBattle();
        } else if (mode_ == Mode::Court) {
            const int choice = courtChoiceAt(x, y, w, h);
            if (choice >= 0) judge(choice);
        } else if (mode_ == Mode::Comics) {
            if (comicPicker_) {
                const int choice = comicChoiceAt(x, y, w, h);
                if (choice >= 0) {
                    comicIndex_ = choice;
                    comicPicker_ = false;
                    audio_.cue(440.0, 0.05);
                }
            } else {
                comicPicker_ = true;
            }
        } else if (mode_ == Mode::Quail && quailReading_) {
            beginQuailAnimation();
        } else if (mode_ == Mode::Map || mode_ == Mode::Clouds) {
            finishScene();
        }
    }

    void onWheel(float y)
    {
        if (flow_ != Flow::Scene) return;
        if (mode_ != Mode::Help && !(mode_ == Mode::Quail && quailReading_)) return;
        docScroll_ = std::max(0, docScroll_ - static_cast<int>(std::lround(y * 54.0f)));
    }

    void onKeyDown(SDL_Keycode key, SDL_Scancode scancode, bool repeat, bool &running)
    {
        (void)running;
        if (key == SDLK_M) {
            audio_.toggleMuted();
            return;
        }

        if (flow_ == Flow::CampWatch) {
            openMenu();
            return;
        }

        if (flow_ == Flow::Menu) {
            if (key == SDLK_ESCAPE) {
                flow_ = Flow::CampWatch;
                return;
            }
            if (key == SDLK_Q) {
                quitRequested_ = true;
                return;
            }
            const int actionIndex = menuShortcutIndex(key);
            if (actionIndex >= 0) {
                const auto buttons = layoutButtons(1000, 760);
                if (actionIndex < static_cast<int>(buttons.size())) perform(buttons[static_cast<size_t>(actionIndex)].action);
            }
            return;
        }

        if (key == SDLK_ESCAPE || key == SDLK_Q) {
            if (mode_ == Mode::Comics && !comicPicker_) {
                comicPicker_ = true;
                return;
            }
            finishScene();
            return;
        }

        if ((mode_ == Mode::Help || (mode_ == Mode::Quail && quailReading_)) &&
            handleDocKey(scancode)) {
            return;
        }

        if (mode_ == Mode::Quail && quailReading_ &&
            (key == SDLK_RETURN || key == SDLK_SPACE)) {
            beginQuailAnimation();
            return;
        }

        if (key == SDLK_RETURN && mode_ == Mode::God && godStage_ == GodStage::Horeb) {
            initHoreb();
            godStageStart_ = nowSeconds();
            audio_.cue(523.25, 0.08);
            return;
        }

        if (key == SDLK_RETURN || key == SDLK_R) {
            restartCurrent();
            return;
        }

        if (mode_ == Mode::God && godStage_ == GodStage::Horeb) {
            if (scancode == SDL_SCANCODE_UP || scancode == SDL_SCANCODE_DOWN ||
                scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_RIGHT) {
                moveHoreb(scancode);
                return;
            }
        }

        if (key == SDLK_SPACE) {
            if (mode_ == Mode::WaterRock && !repeat) beginWaterStrike();
            else if (mode_ == Mode::Battle) holdBattle();
            else if (mode_ == Mode::Map || mode_ == Mode::Clouds ||
                     (mode_ == Mode::Quail && !quailReading_)) finishScene();
            return;
        }

        if (mode_ == Mode::Court) {
            if (key == SDLK_1) judge(0);
            if (key == SDLK_2) judge(1);
            if (key == SDLK_3) judge(2);
        } else if (mode_ == Mode::Comics) {
            if (comicPicker_) {
                if (scancode == SDL_SCANCODE_LEFT) previousComic();
                if (scancode == SDL_SCANCODE_RIGHT) nextComic();
                if (key == SDLK_RETURN || key == SDLK_SPACE) comicPicker_ = false;
            } else {
                if (scancode == SDL_SCANCODE_LEFT) previousComic();
                if (scancode == SDL_SCANCODE_RIGHT) nextComic();
            }
        } else if (mode_ == Mode::Map || mode_ == Mode::Clouds ||
                   (mode_ == Mode::Quail && !quailReading_)) {
            finishScene();
        }
    }

    void update(double dt, double t)
    {
        if (showIntro_ && t - introStart_ >= 6.6) {
            showIntro_ = false;
        }
        if (waterAutoReleaseAt_ > 0.0 && t >= waterAutoReleaseAt_) {
            releaseWaterStrike();
            waterAutoReleaseAt_ = -1.0;
        }
        if (flow_ == Flow::CampWatch) updateCamp(dt, t);
        if (flow_ != Flow::Scene) return;
        updateGod(t);
        if (mode_ == Mode::Clouds) updateClouds(dt);
        if (mode_ == Mode::Map) updateMap(dt, t);
        if (mode_ == Mode::Battle) updateBattle(dt, t);
        if (mode_ == Mode::Quail && !quailReading_) updateQuail(dt, t);
    }

    void updateGod(double t)
    {
        if (mode_ != Mode::God) return;
        if (godStage_ == GodStage::Climb && t - mountainStart_ >= 7.5) {
            godStage_ = GodStage::Horeb;
            godStageStart_ = t;
            audio_.cue(523.25, 0.14);
        }
        if (godStage_ == GodStage::Horeb && horebFound_ && t - horebFoundTime_ >= 0.85) {
            advanceGodStage();
        }
    }

    void updateCamp(double dt, double t)
    {
        const double localT = t - campStart_;
        const bool gather = std::fmod(localT, 10.0) >= 5.0;
        const int cycle = static_cast<int>(std::floor(localT / 10.0));
        if (cycle != campCalfCycle_) {
            campCalfCycle_ = cycle;
            campShowCalf_ = random() < 0.5;
        }
        const double speedMax = 72.0;
        const int personTotal = std::max(1, static_cast<int>(std::count_if(camp_.begin(), camp_.end(),
                                                                           [](const CampObj &obj) {
                                                                               return !obj.tent;
                                                                           })));
        int i = 0;
        for (auto &obj : camp_) {
            if (obj.tent) continue;
            if (gather) {
                const double a = (static_cast<double>(i) / personTotal) * 2.0 * kPi;
                const double tx = 150.0 * std::cos(a);
                const double tz = -310.0 + 150.0 * std::sin(a);
                const double dx = tx - obj.x;
                const double dz = tz - obj.z;
                const double len = std::sqrt(dx * dx + dz * dz) + 0.001;
                obj.dx += (dx / len) * 120.0 * dt;
                obj.dz += (dz / len) * 120.0 * dt;
            } else {
                obj.dx += range(-70.0, 70.0) * dt;
                obj.dz += range(-70.0, 70.0) * dt;
            }
            obj.dx = clamp(obj.dx, -speedMax, speedMax);
            obj.dz = clamp(obj.dz, -speedMax, speedMax);
            obj.x += obj.dx * dt;
            obj.z += obj.dz * dt;
            if (obj.x < -320.0 || obj.x > 320.0) {
                obj.x = clamp(obj.x, -320.0, 320.0);
                obj.dx = -obj.dx;
            }
            if (obj.z < -620.0 || obj.z > 0.0) {
                obj.z = clamp(obj.z, -620.0, 0.0);
                obj.dz = -obj.dz;
            }
            ++i;
        }
    }

    void updateClouds(double dt)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        for (auto &cloud : clouds_) {
            cloud.x += cloud.dx * dt;
            if (cloud.x > w + 120.0) {
                cloud.x = -140.0;
                cloud.y = range(40.0, std::max(80.0, h * 0.38));
            }
        }
    }

    void updateMap(double dt, double t)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        if (t < mapStepDue_) return;
        mapStepDue_ = t + 0.025;

        SDL_FPoint p = mapPath_.back();
        mapAngle_ += range(-0.17, 0.17);
        mapAngle_ = wrap(mapAngle_, -kPi, kPi);
        p.x += static_cast<float>(5.5 * std::cos(mapAngle_));
        p.y += static_cast<float>(5.5 * std::sin(mapAngle_));
        p.x = static_cast<float>(clamp(p.x, -w * 0.43, w * 0.43));
        p.y = static_cast<float>(clamp(p.y, -h * 0.34, h * 0.34));
        mapPath_.push_back(p);
        if (mapPath_.size() > 1200) {
            mapPath_.erase(mapPath_.begin(), mapPath_.begin() + 60);
        }
        (void)dt;
    }

    void updateBattle(double dt, double t)
    {
        if (battleHeld_) {
            battleTT_ = 0.0;
            battleT0_ = t;
        } else {
            battleTT_ = clamp(std::pow((t - battleT0_) / 2.0, 4.0), 0.0, 1.0);
        }
        const double direction = battleTT_ < 0.5 ? -1.0 : 1.0;
        battleShift_ += direction * 50.0 * dt;
        battleLast_ = t;
    }

    void updateQuail(double dt, double t)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        const double skyH = h * 0.58;
        for (auto &q : quail_) {
            if (q.dead) {
                q.x += q.dx * 0.15 * dt;
                q.y += 135.0 * dt;
                if (q.y > skyH + 18.0) {
                    q.y = skyH + 18.0;
                    q.dx = 0.0;
                }
            } else {
                q.x += q.dx * dt;
                q.y += q.dy * dt;
                if (q.y < 30.0 || q.y > skyH - 26.0) q.dy = -q.dy;
                if (q.x > w + 80.0) q.x = -70.0;
                if (random() < dt * 0.13 && t > 1.0) q.dead = true;
            }
        }
    }

    void openMenu()
    {
        flow_ = Flow::Menu;
        audio_.cue(523.25, 0.05);
    }

    int menuShortcutIndex(SDL_Keycode key) const
    {
        if (key >= SDLK_1 && key <= SDLK_9) return static_cast<int>(key - SDLK_1);
        if (key == SDLK_0) return 9;
        return -1;
    }

    bool handleDocKey(SDL_Scancode scancode)
    {
        constexpr int line = 28;
        if (scancode == SDL_SCANCODE_DOWN) {
            docScroll_ += line;
            return true;
        }
        if (scancode == SDL_SCANCODE_UP) {
            docScroll_ = std::max(0, docScroll_ - line);
            return true;
        }
        if (scancode == SDL_SCANCODE_PAGEDOWN) {
            docScroll_ += line * 12;
            return true;
        }
        if (scancode == SDL_SCANCODE_PAGEUP) {
            docScroll_ = std::max(0, docScroll_ - line * 12);
            return true;
        }
        return false;
    }

    void beginWaterStrike()
    {
        waterDownTime_ = nowSeconds();
        waterDownStroke_ = true;
        waterAutoReleaseAt_ = -1.0;
        audio_.cue(146.83, 0.16);
    }

    void releaseWaterStrike()
    {
        if (!waterDownStroke_) return;
        waterDownStroke_ = false;
        waterUpTime_ = nowSeconds();
    }

    void quickWaterStrike()
    {
        beginWaterStrike();
        waterAutoReleaseAt_ = nowSeconds() + 0.13;
    }

    void holdBattle()
    {
        if (!battleHeld_) audio_.cue(196.0, 0.08);
        battleHeld_ = true;
    }

    void releaseBattle()
    {
        if (battleHeld_) {
            battleHeld_ = false;
            battleT0_ = nowSeconds();
        }
    }

    void beginQuailAnimation()
    {
        quailReading_ = false;
        sceneStart_ = nowSeconds();
        docScroll_ = 0;
        initQuail();
        audio_.cue(523.25, 0.08);
    }

    void moveHoreb(SDL_Scancode scancode)
    {
        constexpr double turn = 0.12;
        constexpr double stride = 58.0;
        if (scancode == SDL_SCANCODE_LEFT) horebAngle_ -= turn;
        if (scancode == SDL_SCANCODE_RIGHT) horebAngle_ += turn;
        if (scancode == SDL_SCANCODE_UP) {
            horebX_ += std::sin(horebAngle_) * stride;
            horebZ_ += std::cos(horebAngle_) * stride;
        }
        if (scancode == SDL_SCANCODE_DOWN) {
            horebX_ -= std::sin(horebAngle_) * stride;
            horebZ_ -= std::cos(horebAngle_) * stride;
        }
        horebAngle_ = wrap(horebAngle_, -kPi, kPi);
        horebX_ = clamp(horebX_, -2400.0, 2400.0);
        horebZ_ = clamp(horebZ_, -400.0, 4300.0);
        audio_.cue(scancode == SDL_SCANCODE_UP ? 349.23 : 293.66, 0.05);
    }

    void advanceGodStage()
    {
        if (mode_ != Mode::God) return;
        if (godStage_ == GodStage::Climb) {
            godStage_ = GodStage::Horeb;
        } else if (godStage_ == GodStage::Horeb) {
            godStage_ = GodStage::Talking;
            godTextLines_ = textAssets_.randomGodText(rng_);
        }
        godStageStart_ = nowSeconds();
        audio_.cue(587.33, 0.12);
    }

    void nextComic()
    {
        comicIndex_ = (comicIndex_ + 1) % static_cast<int>(comicFiles_.size());
        audio_.cue(440.0, 0.04);
    }

    void previousComic()
    {
        comicIndex_ = (comicIndex_ + static_cast<int>(comicFiles_.size()) - 1) %
                      static_cast<int>(comicFiles_.size());
        audio_.cue(392.0, 0.04);
    }

    void generateCourt()
    {
        const std::array<std::string, 3> accused = {"A man", "A woman", "A child"};
        const std::array<std::string, 4> crimes = {
            "commits murder", "commits adultery", "commits blasphemy", "commits idolatry"};
        const std::array<std::string, 4> victims = {"to a man", "to a woman", "to a child", "to an animal"};

        const int a = irange(0, 2);
        const int c = irange(0, 3);
        courtPrompt_ = accused[a] + " " + crimes[c];
        if (c <= 1) courtPrompt_ += " " + victims[irange(0, 3)];
        courtPrompt_ += (irange(0, 4) == 0) ? ", again!" : ".";
        courtResult_.clear();
    }

    void judge(int choice)
    {
        static const std::array<std::string, 3> result = {
            "Mercy given. The camp keeps talking about it.",
            "Punishment ordered. The elders nod and write it down.",
            "A severe punishment. The desert gets quiet.",
        };
        courtResult_ = result[static_cast<size_t>(choice)];
        generateCourt();
        courtResult_ = result[static_cast<size_t>(choice)];
        audio_.cue(261.63 + choice * 65.41, 0.08);
    }

    void render(double t)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window_, &w, &h);

        if (showIntro_) {
            drawIntro(w, h, t);
            SDL_RenderPresent(renderer_);
            return;
        }

        if (flow_ == Flow::CampWatch) {
            drawCamp(w, h, t);
            SDL_RenderPresent(renderer_);
            return;
        }

        if (flow_ == Flow::Menu) {
            drawMenu(w, h);
            SDL_RenderPresent(renderer_);
            return;
        }

        switch (mode_) {
        case Mode::Camp: drawCamp(w, h, t); break;
        case Mode::God: drawGod(w, h, t); break;
        case Mode::Clouds: drawCloudScene(w, h, t); break;
        case Mode::Court: drawCourt(w, h); break;
        case Mode::Map: drawMap(w, h); break;
        case Mode::WaterRock: drawWaterRock(w, h, t); break;
        case Mode::Battle: drawBattle(w, h, t); break;
        case Mode::Quail: drawQuail(w, h, t); break;
        case Mode::Comics: drawComics(w, h); break;
        case Mode::Help: drawHelp(w, h); break;
        }

        SDL_RenderPresent(renderer_);
    }

    void drawIntro(int w, int h, double t)
    {
        fill({0, 0, static_cast<float>(w), static_cast<float>(h)}, kBlack);
        if (!sprites_.drawCentered("AESplash.DD", 1, w * 0.5f, h * 0.47f,
                                   w * 0.96f, h * 0.86f) &&
            !sprites_.drawCentered("AfterEgypt.HC", 1, w * 0.5f, h * 0.47f,
                                   w * 0.88f, h * 0.76f)) {
            text(titleFont_, "AfterEgypt", w * 0.5f - 96.0f, h * 0.40f, kYellow);
            drawPerson(w * 0.45, h * 0.58, 2.7, false, {76, 42, 148});
            drawPerson(w * 0.53, h * 0.59, 2.2, true, {58, 106, 174});
        }

        const double age = t - introStart_;
        const std::array<std::string, 4> lines = {
            "Leaving all behind, they fled.",
            "Found themselves in a desert.",
            "God!  We're gonna die!",
            "\"Trust Me!\"",
        };
        if (age >= 0.5) {
            const int index = std::min(3, static_cast<int>((age - 0.5) / 1.5));
            const float stripH = 54.0f;
            const float stripY = static_cast<float>(h) - stripH - 18.0f;
            const bool blinkRed = std::fmod(age * 5.0, 1.0) >= 0.5;
            fill({0.0f, stripY, static_cast<float>(w), stripH}, blinkRed ? kRed : kBlack);
            fill({2.0f, stripY + 2.0f, static_cast<float>(w) - 4.0f, stripH - 4.0f}, kBlack);
            const std::string &msg = lines[static_cast<size_t>(index)];
            const float approxW = static_cast<float>(msg.size()) * 9.0f;
            text(font_, msg, w * 0.5f - approxW * 0.5f, stripY + 17.0f, kYellow);
        }
    }

    void sceneBackdrop(int contentW, int h, bool river = false)
    {
        fill({0, 0, static_cast<float>(contentW), static_cast<float>(h)}, kSky);
        for (int y = 0; y < h; y += 4) {
            const float mix = static_cast<float>(y) / static_cast<float>(std::max(1, h));
            setDraw(blend(kDeepSky, kSky, mix));
            SDL_RenderLine(renderer_, 0.0f, static_cast<float>(y),
                           static_cast<float>(contentW), static_cast<float>(y));
        }
        const bool drewMountain = sprites_.draw("Mountain.HC", 1, 0.0f, h * 0.56f,
                                                static_cast<float>(contentW) / 640.0f);
        if (!drewMountain) {
            triangle({{0.0f, h * 0.58f}, {contentW * 0.32f, h * 0.25f}, {contentW * 0.62f, h * 0.58f}},
                     {118, 97, 87});
            triangle({{contentW * 0.24f, h * 0.58f}, {contentW * 0.56f, h * 0.2f},
                      {static_cast<float>(contentW), h * 0.58f}},
                     {137, 112, 91});
        }
        fill({0, h * 0.56f, static_cast<float>(contentW), h * 0.44f}, kDesert);
        if (river) {
            triangle({{contentW * 0.42f, h * 0.56f}, {contentW * 0.55f, static_cast<float>(h)},
                      {contentW * 0.73f, static_cast<float>(h)}},
                     {31, 128, 215});
        }
        filledCircle(62.0f, 54.0f, 24.0f, kYellow);
    }

    void drawVersePanel(const std::string &book, const std::string &marker, int lineCount,
                        int w, int maxH = 140)
    {
        const float panelH = static_cast<float>(std::min(maxH, std::max(92, w / 9)));
        fill({0, 0, static_cast<float>(w), panelH}, {104, 225, 235, 238});
        fill({0, 0, static_cast<float>(w), 24.0f}, kBlue);
        fill({0, panelH - 22.0f, static_cast<float>(w), 22.0f}, kYellow);
        const std::vector<std::string> lines = textAssets_.bibleVerse(book, marker, lineCount);
        text(smallFont_, joinLines(lines), 18.0f, 31.0f, kInk, std::max(260, w - 36));
    }

    void drawScrollableDoc(const std::string &title, const std::vector<std::string> &lines,
                           int w, int h, const std::string &footer)
    {
        fill({0, 0, static_cast<float>(w), static_cast<float>(h)}, kYellow);
        fill({0, 0, static_cast<float>(w), 58.0f}, kBlue);
        fill({0, 58.0f, static_cast<float>(w), 14.0f}, {104, 225, 235});
        text(titleFont_, title, 28.0f, 14.0f, kYellow);

        SDL_Rect clip{24, 88, std::max(1, w - 48), std::max(1, h - 146)};
        SDL_SetRenderClipRect(renderer_, &clip);
        text(font_, joinLines(lines), 34.0f, 92.0f - static_cast<float>(docScroll_),
             kInk, std::max(260, w - 68));
        SDL_SetRenderClipRect(renderer_, nullptr);

        fill({0, static_cast<float>(h - 42), static_cast<float>(w), 42.0f}, {104, 225, 235});
        text(smallFont_, footer, 28.0f, static_cast<float>(h - 29), kBlue);
    }

    void drawCamp(int contentW, int h, double t)
    {
        const double localT = t - campStart_;
        const double viewY = 200.0 - 100.0 * std::sin(localT);
        const double viewZ = 225.0 + 100.0 * std::cos(localT);
        sceneBackdrop(contentW, h);
        fill({0, h * 0.72f, static_cast<float>(contentW), h * 0.28f}, {187, 130, 67});

        std::vector<const CampObj *> drawList;
        drawList.reserve(camp_.size());
        for (const auto &obj : camp_) drawList.push_back(&obj);
        std::sort(drawList.begin(), drawList.end(), [](const CampObj *a, const CampObj *b) {
            return a->z < b->z;
        });

        int spriteFrame = 0;
        for (const CampObj *obj : drawList) {
            const double depth = (obj->z + 620.0) / 620.0;
            const double sx = contentW * 0.5 + obj->x * (0.46 + 0.32 * depth) -
                              (viewY - 200.0) * 0.18;
            const double sy = h * 0.75 + obj->z * 0.39 + (viewZ - 225.0) * (0.12 + depth * 0.13);
            const double scale = 0.45 + 0.9 * depth;
            if (obj->tent) {
                if (!sprites_.draw("Camp.HC", 7, static_cast<float>(sx), static_cast<float>(sy),
                                   static_cast<float>(scale * 0.36))) {
                    drawTent(sx, sy, scale);
                }
            } else {
                const std::array<int, 4> right = {2, 1, 3, 1};
                const std::array<int, 4> left = {5, 4, 6, 4};
                const int frame = (static_cast<int>(std::floor(t * 6.0)) + spriteFrame++) & 3;
                const int bi = obj->dx < 0.0 ? left[frame] : right[frame];
                if (!sprites_.draw("Camp.HC", bi, static_cast<float>(sx), static_cast<float>(sy),
                                   static_cast<float>(scale * 0.58))) {
                    drawPerson(sx, sy, scale, obj->dx < 0.0, {82, 48, 164});
                }
            }
        }

        if (campShowCalf_ && std::fmod(localT, 10.0) >= 5.0 && std::fmod(t, 0.7) < 0.46) {
            if (!sprites_.draw("Camp.HC", 8, contentW * 0.5f, h * 0.53f, 1.8f)) {
                drawCalf(contentW * 0.5, h * 0.53);
            }
            text(font_, "!! Golden Calf !!", contentW * 0.5f - 72.0f, h * 0.44f, kRed);
        }

        text(titleFont_, "AfterEgypt", 26.0f, 22.0f, kYellow);
        text(font_, "Day " + std::to_string(day_) + "   People " + std::to_string(people_),
             30.0f, 62.0f, kWhite);
        if (std::fmod(t, 1.0) < 0.62) {
            text(font_, "Press any key or click.", 30.0f, h - 42.0f, kInk);
        }
    }

    void drawGod(int contentW, int h, double t)
    {
        if (godStage_ == GodStage::Horeb) {
            drawHoreb(contentW, h, t);
            return;
        }
        if (godStage_ == GodStage::Talking) {
            drawGodTalking(contentW, h, t);
            return;
        }

        sceneBackdrop(contentW, h);
        const std::vector<SDL_FPoint> path = mountainPath(contentW, h);
        setDraw(kBrown);
        for (size_t i = 1; i < path.size(); ++i) {
            thickLine(path[i - 1].x, path[i - 1].y, path[i].x, path[i].y, 3.0f, kBrown);
        }

        const double progress = clamp((t - mountainStart_) / 7.5, 0.0, 1.0);
        const SDL_FPoint moses = pathPoint(path, progress);
        const std::array<int, 4> climb = {2, 3, 4, 3};
        const int climbFrame = climb[static_cast<size_t>(std::floor(t * 6.0)) & 3];
        if (!sprites_.draw("Mountain.HC", climbFrame, moses.x, moses.y, 1.65f)) {
            drawPerson(moses.x, moses.y, 1.25, false, {76, 42, 148});
        }
        text(font_, "Mt. Horeb", contentW * 0.5f - 44.0f, h * 0.16f, kInk);

        text(font_, "Moses walks the mountain path.", 42.0f, 44.0f, kInk);
    }

    void drawGodTalking(int contentW, int h, double t)
    {
        fill({0, 0, static_cast<float>(contentW), static_cast<float>(h)}, {118, 218, 230});
        fill({0, h * 0.16f, static_cast<float>(contentW), h * 0.84f}, {234, 206, 73});

        const float backdropScale = static_cast<float>(contentW) / 640.0f;
        if (!sprites_.draw("GodTalking.HC", 4, 0.0f, h * 0.55f, backdropScale) &&
            !sprites_.draw("Mountain.HC", 1, 0.0f, h * 0.58f, backdropScale)) {
            triangle({{0.0f, h * 0.58f}, {contentW * 0.28f, h * 0.24f}, {contentW * 0.58f, h * 0.58f}},
                     {112, 92, 82});
            triangle({{contentW * 0.24f, h * 0.58f}, {contentW * 0.57f, h * 0.2f},
                      {static_cast<float>(contentW), h * 0.58f}},
                     {134, 106, 82});
        }

        const float s = std::min(contentW / 640.0f, h / 480.0f) * 1.45f;
        const float ox = contentW * 0.5f - 320.0f * s;
        const float oy = h * 0.52f - 240.0f * s;
        const int talkFrame = (std::fmod(t, 0.8) < 0.4) ? 1 : 2;
        if (!sprites_.draw("GodTalking.HC", talkFrame, ox + 44.0f * s, oy + 99.0f * s, s)) {
            drawPerson(ox + 80.0f * s, oy + 170.0f * s, 1.8 * s, false, {76, 42, 148});
        }

        const float bushX = ox + 213.0f * s;
        const float bushY = oy + 91.0f * s;
        if (!sprites_.draw("GodTalking.HC", 3, bushX, bushY, s)) {
            drawBurningBush(bushX, bushY, t);
        }

        for (int i = 0; i < 80; ++i) {
            const double a1 = range(0.0, 2.0 * kPi);
            const double a2 = range(0.0, 2.0 * kPi);
            const double r1 = std::sqrt(random()) * 34.0;
            const double r2 = std::sqrt(random()) * 34.0;
            line(bushX + 30.0f * s + std::cos(a1) * r1, bushY - 35.0f * s + std::sin(a1) * r1,
                 bushX + 30.0f * s + std::cos(a2) * r2, bushY - 35.0f * s + std::sin(a2) * r2,
                 paletteCycle(t + i));
        }

        const double wave = std::fmod(t, 4.0) < 2.0 ? std::sin(t * kPi) : 0.0;
        for (int i = 10; i < contentW - 10; ++i) {
            line(i, h * 0.78f + 4.0 * wave * std::sin(i / 18.0),
                 i + 1, h * 0.78f + 4.0 * wave * std::sin((i + 1) / 18.0), kBrown);
        }

        if (godTextLines_.empty()) godTextLines_ = textAssets_.randomGodText(rng_);
        text(titleFont_, "God Says...", 42.0f, 44.0f, kRed);
        std::vector<std::string> body = godTextLines_;
        if (!body.empty() && body.front() == "God Says...") body.erase(body.begin());
        text(font_, joinLines(body), 48.0f, 92.0f, kInk, std::min(640, contentW - 96));
        text(smallFont_, "Esc returns.", 48.0f, h - 38.0f, kInk);
    }

    HorebProjection projectHoreb(const HorebObj &obj, int contentW, int h) const
    {
        const double dx = obj.x - horebX_;
        const double dz = obj.z - horebZ_;
        const double side = dx * std::cos(horebAngle_) - dz * std::sin(horebAngle_);
        const double depth = dx * std::sin(horebAngle_) + dz * std::cos(horebAngle_);
        if (depth <= 32.0 || depth > 4200.0) return {};

        const float horizon = h * 0.37f;
        const double ground = clamp(500.0 / (depth + 500.0), 0.0, 1.0);
        const float x = contentW * 0.5f +
                        static_cast<float>(side * (contentW * 0.82) / (depth + 160.0));
        const float y = horizon + static_cast<float>((h - horizon) * ground);
        if (x < -180.0f || x > contentW + 180.0f) return {};

        HorebProjection p;
        p.visible = true;
        p.x = x;
        p.y = y;
        p.scale = static_cast<float>(clamp(2.1 * ground, 0.10, 1.75));
        p.depth = depth;
        p.side = side;
        return p;
    }

    void drawHorebObject(const HorebObj &obj, const HorebProjection &p, double t)
    {
        float scale = p.scale;
        if (obj.bi == 1) scale *= 0.28f;
        if (obj.seed == 0) scale *= 1.2f;

        if (!sprites_.draw("HorebA.HC", obj.bi, p.x, p.y, scale)) {
            if (obj.bi == 1) {
                filledCircle(p.x, p.y, std::max(2.0f, scale * 5.0f), {86, 78, 72});
            } else if (obj.bi >= 6) {
                drawPerson(p.x, p.y, scale * 0.9, obj.seed % 2 == 0, {236, 236, 220});
            } else {
                line(p.x, p.y, p.x + std::sin(obj.seed) * 15.0f * scale, p.y - 30.0f * scale, kBrown);
                filledCircle(p.x, p.y - 28.0f * scale, 9.0f * scale, {39, 108, 63});
            }
        }

        if (obj.seed == 0) {
            for (int i = 0; i < 45; ++i) {
                const double a1 = t * 4.0 + i * 1.7;
                const double a2 = t * 3.2 + i * 2.1;
                const double r1 = std::sqrt(std::abs(std::sin(i * 12.3))) * 22.0 * scale;
                const double r2 = std::sqrt(std::abs(std::cos(i * 8.7))) * 22.0 * scale;
                line(p.x + std::cos(a1) * r1, p.y - 24.0f * scale + std::sin(a1) * r1,
                     p.x + std::cos(a2) * r2, p.y - 24.0f * scale + std::sin(a2) * r2,
                     paletteCycle(t + i));
            }
        }
    }

    void drawHoreb(int contentW, int h, double t)
    {
        fill({0, 0, static_cast<float>(contentW), static_cast<float>(h)}, {112, 191, 231});
        const float horizon = h * 0.37f;
        for (int y = 0; y < static_cast<int>(horizon); y += 4) {
            const float mix = static_cast<float>(y) / std::max(1.0f, horizon);
            setDraw(blend({55, 136, 210}, {190, 220, 232}, mix));
            SDL_RenderLine(renderer_, 0.0f, static_cast<float>(y),
                           static_cast<float>(contentW), static_cast<float>(y));
        }
        fill({0, horizon, static_cast<float>(contentW), h - horizon}, {203, 151, 78});
        triangle({{0.0f, horizon}, {contentW * 0.28f, h * 0.16f}, {contentW * 0.56f, horizon}},
                 {111, 88, 78});
        triangle({{contentW * 0.35f, horizon}, {contentW * 0.67f, h * 0.12f},
                  {static_cast<float>(contentW), horizon}},
                 {131, 101, 77});

        const float cx = contentW * 0.5f;
        for (int i = -8; i <= 8; ++i) {
            const float endX = cx + i * contentW * 0.115f -
                               static_cast<float>(std::sin(horebAngle_) * contentW * 0.18);
            line(cx, horizon, endX, h, {134, 96, 56, 130});
        }
        for (int i = 1; i < 11; ++i) {
            const float y = horizon + (h - horizon) * (i * i) / 122.0f;
            line(0.0, y, contentW, y + std::sin(horebAngle_ + i) * 8.0, {176, 121, 62, 110});
        }

        struct DrawItem {
            HorebObj obj;
            HorebProjection p;
        };
        std::vector<DrawItem> draws;
        draws.reserve(horebObjs_.size());
        for (const HorebObj &obj : horebObjs_) {
            HorebProjection p = projectHoreb(obj, contentW, h);
            if (p.visible) draws.push_back({obj, p});
        }
        std::sort(draws.begin(), draws.end(), [](const DrawItem &a, const DrawItem &b) {
            return a.p.depth > b.p.depth;
        });
        for (const DrawItem &item : draws) {
            drawHorebObject(item.obj, item.p, t);
        }

        if (!horebObjs_.empty()) {
            const HorebProjection bush = projectHoreb(horebObjs_.front(), contentW, h);
            const float targetY = h * 0.56f;
            const double screenDistance = bush.visible
                                              ? std::hypot(bush.x - cx, bush.y - targetY)
                                              : 100000.0;
            if (!horebFound_ && bush.visible && bush.depth < 520.0 && screenDistance < 170.0) {
                horebFound_ = true;
                horebFoundTime_ = nowSeconds();
                audio_.cue(659.25, 0.22);
            }
        }

        drawVersePanel("Exodus", "3:1", 21, contentW, 150);
        if (horebFound_) {
            text(font_, "Burning Bush Found.", contentW * 0.5f - 86.0f, h * 0.5f - 11.0f, kYellow);
        } else if (std::fmod(t, 1.0) < 0.58) {
            text(font_, "Find the Burning Bush.", contentW * 0.5f - 104.0f,
                 h * 0.5f - 11.0f, kRed);
        }
        text(smallFont_, "Arrow keys move and turn.", 34.0f, h - 34.0f, kInk);
    }

    void drawCloudScene(int contentW, int h, double t)
    {
        sceneBackdrop(contentW, h);
        for (const auto &cloud : clouds_) drawCloud(cloud, t);
        drawVersePanel("Exodus", "14:19", 7, contentW, 120);
    }

    void drawCourt(int contentW, int h)
    {
        fill({0, 0, static_cast<float>(contentW), static_cast<float>(h)}, {38, 43, 55});
        sceneBackdrop(contentW, h);
        fill({contentW * 0.1f, h * 0.16f, contentW * 0.8f, h * 0.52f}, {236, 219, 172, 235});
        text(titleFont_, "Hold Court", contentW * 0.14f, h * 0.2f, kInk);
        text(font_, courtPrompt_, contentW * 0.14f, h * 0.29f, kInk, static_cast<int>(contentW * 0.66));
        const auto rects = courtRects(contentW, h);
        const std::array<std::string, 3> labels = {"Show Mercy", "Punish", "Really Punish"};
        const std::array<Color, 3> colors = {kGreen, kGold, kRed};
        for (size_t i = 0; i < rects.size(); ++i) {
            fill(rects[i], colors[i]);
            text(font_, labels[i], rects[i].x + 16.0f, rects[i].y + 12.0f, kBlack);
        }
        if (!courtResult_.empty()) {
            text(font_, courtResult_, contentW * 0.14f, h * 0.59f, kBlue, static_cast<int>(contentW * 0.66));
        }
    }

    void drawMap(int contentW, int h)
    {
        fill({0, 0, static_cast<float>(contentW), static_cast<float>(h)}, {227, 190, 96});
        for (int i = 0; i < 34; ++i) {
            const float y = static_cast<float>((i * 43) % std::max(1, h));
            line(0.0, y, contentW, y + std::sin(i) * 50.0, {196, 139, 70, 85});
        }

        const float cx = contentW * 0.5f;
        const float cy = h * 0.5f;
        setDraw(kInk);
        for (size_t i = 1; i < mapPath_.size(); i += 2) {
            line(cx + mapPath_[i - 1].x, cy + mapPath_[i - 1].y,
                 cx + mapPath_[i].x, cy + mapPath_[i].y, kBlack);
        }
        if (!mapPath_.empty()) {
            const SDL_FPoint p = mapPath_.back();
            const bool left = mapAngle_ > kPi / 2 || mapAngle_ < -kPi / 2;
            const std::array<int, 4> right = {2, 3, 4, 3};
            const std::array<int, 4> leftFrames = {5, 6, 7, 6};
            const int frame = (static_cast<int>(std::floor(nowSeconds() * 6.0)) +
                               static_cast<int>(mapPath_.size())) & 3;
            const int bi = left ? leftFrames[frame] : right[frame];
            if (!sprites_.draw("Mountain.HC", bi, cx + p.x, cy + p.y, 1.8f)) {
                drawPerson(cx + p.x, cy + p.y, 1.15, left, {76, 42, 148});
            }
        }
        drawVersePanel("Exodus", "16:35", 3, contentW, 108);
    }

    void drawWaterRock(int contentW, int h, double t)
    {
        sceneBackdrop(contentW, h, true);
        const float cx = contentW * 0.48f;
        const float cy = h * 0.56f;
        if (!sprites_.draw("WaterRock.HC", 5, cx - 25.0f, cy + 80.0f, 5.2f)) {
            fill({cx - 90.0f, cy - 10.0f, 130.0f, 92.0f}, kRock);
            triangle({{cx - 120.0f, cy + 82.0f}, {cx - 70.0f, cy - 28.0f}, {cx + 48.0f, cy + 82.0f}}, kRock);
            triangle({{cx - 90.0f, cy + 82.0f}, {cx + 20.0f, cy - 10.0f}, {cx + 82.0f, cy + 82.0f}}, {121, 112, 102});
        }

        constexpr double downDelay = 0.075;
        constexpr double upTime = 0.2;
        const bool waterMade = waterDownTime_ >= 0.0 && t - waterDownTime_ >= downDelay;
        const double age = waterMade ? t - waterDownTime_ - downDelay : -1.0;
        if (waterMade) {
            const float pulse = static_cast<float>(std::sin(t * 5.5) * 3.0);
            const float r = static_cast<float>(std::min(110.0, 18.0 + age * 44.0));
            filledCircle(cx - 72.0f, cy + 18.0f, std::min(r, 48.0f) + pulse, {36, 150, 220, 190});
            ring(cx - 72.0f, cy + 18.0f, std::min(110.0f, r + pulse), kWater);
            triangle({{cx - 74.0f, cy + 18.0f}, {cx - 6.0f, static_cast<float>(h)},
                      {cx - 130.0f, static_cast<float>(h)}},
                     {39, 154, 220, 180});
            for (int i = 0; i < 5; ++i) {
                const float x = cx - 110.0f + i * 22.0f + static_cast<float>(std::sin(t * 3.0 + i) * 8.0);
                line(x, cy + 70.0f, x + 28.0f, h, {160, 232, 255, 120});
            }
        }

        float arm = 0.0f;
        if (waterDownStroke_ && waterDownTime_ >= 0.0) {
            arm = static_cast<float>(clamp((t - waterDownTime_) / downDelay, 0.0, 1.0));
        } else if (waterUpTime_ >= 0.0) {
            arm = static_cast<float>(1.0 - clamp((t - waterUpTime_) / upTime, 0.0, 1.0));
        }
        int mosesBi = 1;
        if (arm > 0.66f) mosesBi = 4;
        else if (arm > 0.20f) mosesBi = 3;
        else if (std::fmod(t, 1.2) >= 0.6) mosesBi = 2;
        if (!sprites_.draw("WaterRock.HC", mosesBi, cx + 150.0f, cy + 82.0f, 1.45f)) {
            drawPerson(cx + 150.0f, cy + 70.0f, 1.55, true, {76, 42, 148});
            line(cx + 124.0f, cy + 22.0f + arm * 28.0f, cx + 50.0f, cy - 38.0f + arm * 88.0f, kBrown);
        }
        drawVersePanel("Exodus", "17:6", 4, contentW, 118);
        text(font_, "<SPACE>", 34.0f, 126.0f, kInk);
    }

    void drawBattle(int contentW, int h, double t)
    {
        sceneBackdrop(contentW, h);
        fill({0, h * 0.62f, static_cast<float>(contentW), h * 0.38f}, {155, 100, 59});
        triangle({{contentW * 0.25f, h * 0.72f}, {contentW * 0.5f, h * 0.42f},
                  {contentW * 0.75f, h * 0.72f}},
                 {118, 86, 63});
        const float sceneScale = std::min(contentW / 640.0f, h / 480.0f);
        const float cx = contentW * 0.5f;
        const float cy = h * 0.5f;
        const float spacing = 45.0f * sceneScale;
        const bool handsUp = battleTT_ < 0.5;
        const int mosesBi = handsUp ? 3 : 4;
        if (!sprites_.draw("Battle.HC", mosesBi, cx, cy + spacing, sceneScale)) {
            drawMosesArms(cx, cy + spacing - 54.0f * sceneScale, handsUp);
        }

        const float offset = static_cast<float>(battleShift_);
        const std::array<SDL_FPoint, 3> fighters = {{
            {cx + offset + spacing, cy - spacing},
            {cx + offset + 2.0f * spacing, cy - spacing},
            {cx + offset + spacing, cy - 2.0f * spacing},
        }};
        const std::array<double, 3> phase = {0.0, 0.333, 0.666};
        for (size_t i = 0; i < fighters.size(); ++i) {
            double saw = std::fmod(t + phase[i] * 0.25, 0.25) / 0.25;
            saw *= 2.0;
            if (saw > 1.0) saw = 2.0 - saw;
            const int bi = saw > 0.5 ? 2 : 1;
            if (!sprites_.draw("Battle.HC", bi, fighters[i].x, fighters[i].y, sceneScale)) {
                drawFighter(fighters[i].x, fighters[i].y, true, t + i);
            }
        }

        drawVersePanel("Exodus", "17:11", 8, contentW, 132);
        text(font_, "Hold <SPACE>", 34.0f, 140.0f, kInk);
    }

    void drawQuail(int contentW, int h, double t)
    {
        if (quailReading_) {
            drawScrollableDoc("Numbers 11:11",
                              textAssets_.bibleVerse("Numbers", "11:11", 88),
                              contentW, h,
                              "Scroll down to finish reading. Enter/Space begins. Esc returns.");
            return;
        }

        sceneBackdrop(contentW, h);
        const float skyH = h * 0.58f;
        fill({0, skyH, static_cast<float>(contentW), h - skyH}, {219, 184, 85});
        for (const auto &q : quail_) {
            if (q.dead) {
                if (!sprites_.draw("Quail.HC", 3, static_cast<float>(q.x), static_cast<float>(q.y), 0.72f)) {
                    drawDeadBird(q.x, q.y);
                }
            } else {
                const int bi = std::sin((t + q.phase) * 8.0) >= 0.0 ? 1 : 2;
                if (!sprites_.draw("Quail.HC", bi, static_cast<float>(q.x), static_cast<float>(q.y), 0.72f)) {
                    drawBird(q.x, q.y, 8.0 + 5.0 * std::sin((t + q.phase) * 8.0));
                }
            }
        }
        text(font_, "Press any key.", 28.0f, h - 42.0f, kInk);
    }

    void drawComics(int contentW, int h)
    {
        fill({0, 0, static_cast<float>(contentW), static_cast<float>(h)}, {46, 44, 51});
        if (comicPicker_) {
            fill({0, 0, static_cast<float>(contentW), 58.0f}, kBlue);
            fill({0, 58.0f, static_cast<float>(contentW), h - 58.0f}, kYellow);
            text(titleFont_, "Moses Comics", 28.0f, 14.0f, kYellow);
            const float bw = std::min(230.0f, contentW * 0.26f);
            const float bh = 44.0f;
            const float gap = 16.0f;
            const float gridW = bw * 3.0f + gap * 2.0f;
            const float x0 = (contentW - gridW) * 0.5f;
            const float y0 = h * 0.22f;
            for (size_t i = 0; i < comicFiles_.size(); ++i) {
                const int col = static_cast<int>(i % 3);
                const int row = static_cast<int>(i / 3);
                SDL_FRect rect{x0 + col * (bw + gap), y0 + row * (bh + gap), bw, bh};
                fill(rect, i == static_cast<size_t>(comicIndex_) ? kGold : kPanelLite);
                text(font_, comicName(static_cast<int>(i)), rect.x + 14.0f, rect.y + 12.0f,
                     i == static_cast<size_t>(comicIndex_) ? kBlack : kYellow);
            }
            text(smallFont_, "Select a file. Esc returns.", 28.0f, h - 34.0f, kBlue);
            return;
        }

        const SDL_FRect panel{contentW * 0.08f, h * 0.10f, contentW * 0.84f, h * 0.72f};
        fill(panel, {244, 228, 185});
        fill({panel.x + 10, panel.y + 10, panel.w - 20, panel.h - 20}, {236, 201, 119});
        drawComicArt(panel, comicIndex_);
        text(titleFont_, comicName(comicIndex_), panel.x + 26.0f, panel.y + panel.h + 18.0f, kWhite);
        text(font_, std::to_string(comicIndex_ + 1) + " / " + std::to_string(comicFiles_.size()),
             contentW * 0.5f - 20.0f, h - 42.0f, kWhite);
        text(smallFont_, "Left/right changes file. Esc returns to picker.", 28.0f, h - 34.0f, kWhite);
    }

    void drawMenu(int w, int h)
    {
        fill({0, 0, static_cast<float>(w), static_cast<float>(h)}, kBlue);
        fill({0, 0, static_cast<float>(w), h * 0.18f}, {90, 215, 232});
        fill({0, h * 0.18f, static_cast<float>(w), h * 0.82f}, kYellow);
        if (sprites_.drawCentered("AESplash.DD", 1, w * 0.5f, h * 0.17f,
                                  w * 0.62f, h * 0.26f)) {
            fill({0, h * 0.30f, static_cast<float>(w), 2.0f}, kBlue);
        } else {
            text(titleFont_, "AfterEgypt", w * 0.5f - 94.0f, 38.0f, kBlue);
        }

        for (const auto &button : layoutButtons(w, h)) {
            fill(button.rect, button.action == Action::Quit ? kRed : kPanelLite);
            text(font_, button.label, button.rect.x + 16.0f, button.rect.y + 12.0f,
                 button.action == Action::Quit ? kWhite : kYellow);
        }

        text(smallFont_, "Camp " + std::to_string(day_) + "  People " + std::to_string(people_),
             28.0f, h - 38.0f, kBlue);
        text(smallFont_, audio_.muted() ? "M unmutes audio" : "M mutes audio",
             static_cast<float>(w) - 160.0f, h - 38.0f, kBlue);
    }

    std::vector<MenuButton> layoutButtons(int w, int h) const
    {
        const float bw = std::min(310.0f, std::max(220.0f, w * 0.32f));
        constexpr float bh = 46.0f;
        constexpr float gapX = 22.0f;
        constexpr float gapY = 12.0f;
        const float totalW = bw * 2.0f + gapX;
        const float x0 = (static_cast<float>(w) - totalW) * 0.5f;
        const float y0 = std::max(130.0f, h * 0.28f);

        std::vector<MenuButton> out;
        auto add = [&](int index, const std::string &label, Action action) {
            const int col = index % 2;
            const int row = index / 2;
            const float x = x0 + col * (bw + gapX);
            const float y = y0 + row * (bh + gapY);
            out.push_back({{x, y, bw, bh}, label, action});
        };
        add(0, "1  Break Camp", Action::BreakCamp);
        add(1, "2  Talk with God", Action::God);
        add(2, "3  View Clouds", Action::Clouds);
        add(3, "4  Hold Court", Action::Court);
        add(4, "5  View Map", Action::Map);
        add(5, "6  Make Water", Action::WaterRock);
        add(6, "7  Battle", Action::Battle);
        add(7, "8  Beg for Meat", Action::Quail);
        add(8, "9  Moses Comics", Action::Comics);
        add(9, "0  Help", Action::Help);
        out.push_back({{x0 + bw * 0.5f + gapX * 0.5f, y0 + 5.0f * (bh + gapY),
                        bw, bh},
                       "Q  Quit", Action::Quit});
        return out;
    }

    bool actionActive(Action action) const
    {
        switch (action) {
        case Action::BreakCamp: return mode_ == Mode::Camp;
        case Action::God: return mode_ == Mode::God;
        case Action::Clouds: return mode_ == Mode::Clouds;
        case Action::Court: return mode_ == Mode::Court;
        case Action::Map: return mode_ == Mode::Map;
        case Action::WaterRock: return mode_ == Mode::WaterRock;
        case Action::Battle: return mode_ == Mode::Battle;
        case Action::Quail: return mode_ == Mode::Quail;
        case Action::Comics: return mode_ == Mode::Comics;
        case Action::Help: return mode_ == Mode::Help;
        case Action::Quit: return false;
        }
        return false;
    }

    std::string modeName() const
    {
        switch (mode_) {
        case Mode::Camp: return "Camp";
        case Mode::God: return "Talk with God";
        case Mode::Clouds: return "View Clouds";
        case Mode::Court: return "Hold Court";
        case Mode::Map: return "View Map";
        case Mode::WaterRock: return "Make Water";
        case Mode::Battle: return "Battle";
        case Mode::Quail: return "Beg for Meat";
        case Mode::Comics: return "Moses Comics";
        case Mode::Help: return "Help";
        }
        return "";
    }

    void setDraw(Color color)
    {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    }

    void fill(SDL_FRect rect, Color color)
    {
        setDraw(color);
        SDL_RenderFillRect(renderer_, &rect);
    }

    void line(double x1, double y1, double x2, double y2, Color color)
    {
        setDraw(color);
        SDL_RenderLine(renderer_, static_cast<float>(x1), static_cast<float>(y1),
                       static_cast<float>(x2), static_cast<float>(y2));
    }

    void thickLine(float x1, float y1, float x2, float y2, float thickness, Color color)
    {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy) + 0.001f;
        const float nx = -dy / len * thickness * 0.5f;
        const float ny = dx / len * thickness * 0.5f;
        triangle({{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}}, color);
        triangle({{x1 + nx, y1 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}}, color);
    }

    void triangle(std::initializer_list<SDL_FPoint> pts, Color color)
    {
        if (pts.size() != 3) return;
        const SDL_FColor c = fcolor(color);
        SDL_Vertex verts[3];
        int i = 0;
        for (const SDL_FPoint &p : pts) {
            verts[i++] = {p, c, {0, 0}};
        }
        SDL_RenderGeometry(renderer_, nullptr, verts, 3, nullptr, 0);
    }

    void filledCircle(float cx, float cy, float r, Color color)
    {
        setDraw(color);
        const int radius = static_cast<int>(std::ceil(r));
        for (int dy = -radius; dy <= radius; ++dy) {
            const float span = std::sqrt(std::max(0.0f, r * r - static_cast<float>(dy * dy)));
            SDL_RenderLine(renderer_, cx - span, cy + dy, cx + span, cy + dy);
        }
    }

    void ring(float cx, float cy, float r, Color color)
    {
        setDraw(color);
        constexpr int steps = 72;
        for (int i = 0; i < steps; ++i) {
            const double a1 = 2.0 * kPi * i / steps;
            const double a2 = 2.0 * kPi * (i + 1) / steps;
            line(cx + std::cos(a1) * r, cy + std::sin(a1) * r,
                 cx + std::cos(a2) * r, cy + std::sin(a2) * r, color);
        }
    }

    void text(TTF_Font *font, const std::string &s, float x, float y, Color color, int wrapWidth = 0)
    {
        SDL_Color c{color.r, color.g, color.b, color.a};
        SDL_Surface *surface = wrapWidth > 0
                                   ? TTF_RenderText_Blended_Wrapped(font, s.c_str(), s.size(), c, wrapWidth)
                                   : TTF_RenderText_Blended(font, s.c_str(), s.size(), c);
        if (!surface) return;
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
        if (!texture) {
            SDL_DestroySurface(surface);
            return;
        }
        SDL_FRect dst{x, y, static_cast<float>(surface->w), static_cast<float>(surface->h)};
        SDL_RenderTexture(renderer_, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
        SDL_DestroySurface(surface);
    }

    Color blend(Color a, Color b, float t) const
    {
        return {
            static_cast<uint8_t>(a.r + (b.r - a.r) * t),
            static_cast<uint8_t>(a.g + (b.g - a.g) * t),
            static_cast<uint8_t>(a.b + (b.b - a.b) * t),
            static_cast<uint8_t>(a.a + (b.a - a.a) * t),
        };
    }

    static bool pointIn(SDL_FRect rect, float x, float y)
    {
        return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
    }

    void drawTent(double x, double y, double s)
    {
        const float w = static_cast<float>(36.0 * s);
        const float h = static_cast<float>(28.0 * s);
        triangle({{static_cast<float>(x - w), static_cast<float>(y)},
                  {static_cast<float>(x), static_cast<float>(y - h)},
                  {static_cast<float>(x + w), static_cast<float>(y)}},
                 {189, 65, 48});
        triangle({{static_cast<float>(x - w * 0.25), static_cast<float>(y)},
                  {static_cast<float>(x), static_cast<float>(y - h * 0.76)},
                  {static_cast<float>(x + w * 0.18), static_cast<float>(y)}},
                 {77, 38, 44});
    }

    void drawPerson(double x, double y, double s, bool left, Color robe)
    {
        const float head = static_cast<float>(4.2 * s);
        const float body = static_cast<float>(13.0 * s);
        const float sx = static_cast<float>(x);
        const float sy = static_cast<float>(y);
        filledCircle(sx, sy - body - head, head, {102, 61, 34});
        triangle({{sx, sy - body}, {sx - body * 0.48f, sy}, {sx + body * 0.48f, sy}}, robe);
        line(sx - body * 0.22f, sy - body * 0.1f, sx - body * 0.45f, sy + body * 0.38f, kBlack);
        line(sx + body * 0.22f, sy - body * 0.1f, sx + body * 0.45f, sy + body * 0.38f, kBlack);
        line(sx, sy - body * 0.75f, sx + (left ? -1.0f : 1.0f) * body * 0.55f, sy - body * 0.42f, kBrown);
    }

    void drawCalf(double x, double y)
    {
        fill({static_cast<float>(x - 34.0), static_cast<float>(y - 18.0), 58.0f, 28.0f}, kGold);
        filledCircle(static_cast<float>(x + 30.0), static_cast<float>(y - 15.0), 14.0f, kGold);
        line(x + 24.0, y - 30.0, x + 15.0, y - 44.0, kGold);
        line(x + 38.0, y - 30.0, x + 50.0, y - 42.0, kGold);
        for (int i = 0; i < 4; ++i) {
            const double lx = x - 20.0 + i * 14.0;
            line(lx, y + 8.0, lx - 4.0, y + 28.0, kBrown);
        }
    }

    void drawCloud(const Cloud &cloud, double t)
    {
        std::mt19937 local(cloud.seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (int i = 0; i < 120; ++i) {
            const float ox = dist(local) * 88.0f * static_cast<float>(cloud.scale) +
                             std::sin(t * 0.7 + i) * 2.0f;
            const float oy = dist(local) * 32.0f * static_cast<float>(cloud.scale) +
                             std::cos(t * 0.5 + i * 0.7f) * 1.5f;
            const float rr = (2.0f + std::abs(dist(local)) * 5.5f) * static_cast<float>(cloud.scale);
            const Color color = i % 4 == 0 ? Color{210, 212, 218, 185} : Color{250, 250, 250, 215};
            filledCircle(static_cast<float>(cloud.x) + ox, static_cast<float>(cloud.y) + oy, rr, color);
        }
    }

    std::vector<SDL_FPoint> mountainPath(int contentW, int h) const
    {
        const float cx = contentW * 0.5f;
        const float cy = h * 0.69f;
        const float s = std::min(contentW / 900.0f, h / 690.0f);
        const std::array<SDL_FPoint, 9> src = {
            SDL_FPoint{0, 0}, {100, -20}, {-100, -40}, {80, -60}, {-80, -60},
            {60, -80}, {-60, -100}, {40, -120}, {-37, -140},
        };
        std::vector<SDL_FPoint> out;
        for (const auto &p : src) out.push_back({cx + p.x * s * 2.2f, cy + p.y * s * 2.35f});
        return out;
    }

    SDL_FPoint pathPoint(const std::vector<SDL_FPoint> &path, double progress) const
    {
        if (path.empty()) return {0, 0};
        const double scaled = clamp(progress, 0.0, 1.0) * (path.size() - 1);
        const int i = std::min(static_cast<int>(scaled), static_cast<int>(path.size()) - 2);
        const double f = scaled - i;
        return {
            static_cast<float>(path[i].x + (path[i + 1].x - path[i].x) * f),
            static_cast<float>(path[i].y + (path[i + 1].y - path[i].y) * f),
        };
    }

    void drawBurningBush(double x, double y, double t)
    {
        line(x, y, x - 22.0, y + 38.0, kBrown);
        line(x, y, x + 24.0, y + 38.0, kBrown);
        for (int i = 0; i < 10; ++i) {
            const double a = 2.0 * kPi * i / 10.0 + std::sin(t * 3.0 + i) * 0.15;
            filledCircle(static_cast<float>(x + std::cos(a) * 22.0),
                         static_cast<float>(y + std::sin(a) * 15.0),
                         10.0f, i % 2 ? kRed : kYellow);
        }
    }

    Color paletteCycle(double t) const
    {
        const int i = static_cast<int>(std::floor(t * 7.0)) & 3;
        if (i == 0) return kYellow;
        if (i == 1) return kWater;
        if (i == 2) return kPurple;
        return kRed;
    }

    std::array<SDL_FRect, 3> courtRects(int contentW, int h) const
    {
        const float x = contentW * 0.14f;
        const float y = h * 0.39f;
        const float w = contentW * 0.21f;
        return {{
            {x, y, w, 48.0f},
            {x + w + 18.0f, y, w, 48.0f},
            {x + 2.0f * (w + 18.0f), y, w + 24.0f, 48.0f},
        }};
    }

    int courtChoiceAt(float x, float y, int contentW, int h) const
    {
        const auto rects = courtRects(contentW, h);
        for (size_t i = 0; i < rects.size(); ++i) {
            if (pointIn(rects[i], x, y)) return static_cast<int>(i);
        }
        return -1;
    }

    std::string comicName(int index) const
    {
        if (index < 0 || static_cast<size_t>(index) >= comicFiles_.size()) return "";
        return fs::path(comicFiles_[static_cast<size_t>(index)]).stem().string();
    }

    int comicChoiceAt(float x, float y, int w, int h) const
    {
        const float bw = std::min(230.0f, w * 0.26f);
        const float bh = 44.0f;
        const float gap = 16.0f;
        const float gridW = bw * 3.0f + gap * 2.0f;
        const float x0 = (w - gridW) * 0.5f;
        const float y0 = h * 0.22f;
        for (size_t i = 0; i < comicFiles_.size(); ++i) {
            const int col = static_cast<int>(i % 3);
            const int row = static_cast<int>(i / 3);
            SDL_FRect rect{x0 + col * (bw + gap), y0 + row * (bh + gap), bw, bh};
            if (pointIn(rect, x, y)) return static_cast<int>(i);
        }
        return -1;
    }

    void drawMosesArms(float x, float y, bool held)
    {
        filledCircle(x, y - 34.0f, 8.0f, {102, 61, 34});
        triangle({{x, y - 20.0f}, {x - 16.0f, y + 44.0f}, {x + 16.0f, y + 44.0f}}, {76, 42, 148});
        if (held) {
            line(x - 8.0f, y - 14.0f, x - 54.0f, y - 64.0f, kBrown);
            line(x + 8.0f, y - 14.0f, x + 54.0f, y - 64.0f, kBrown);
        } else {
            line(x - 8.0f, y - 14.0f, x - 58.0f, y + 18.0f, kBrown);
            line(x + 8.0f, y - 14.0f, x + 58.0f, y + 18.0f, kBrown);
        }
    }

    void drawFighter(float x, float y, bool leftSide, double t)
    {
        const float slash = 10.0f + 4.0f * std::sin(t * 9.0);
        filledCircle(x, y - 20.0f, 5.0f, {102, 61, 34});
        triangle({{x, y - 12.0f}, {x - 7.0f, y + 16.0f}, {x + 7.0f, y + 16.0f}},
                 leftSide ? kBlue : kRed);
        const float dir = leftSide ? 1.0f : -1.0f;
        line(x, y - 6.0f, x + dir * slash, y - 28.0f, kBlack);
        line(x, y + 16.0f, x - 6.0f, y + 28.0f, kBlack);
        line(x, y + 16.0f, x + 6.0f, y + 28.0f, kBlack);
    }

    void drawBird(double x, double y, double flap)
    {
        line(x - 12.0, y, x, y - flap, kBlack);
        line(x, y - flap, x + 12.0, y, kBlack);
        filledCircle(static_cast<float>(x), static_cast<float>(y - flap), 2.5f, kBlack);
    }

    void drawDeadBird(double x, double y)
    {
        line(x - 7.0, y - 6.0, x + 7.0, y + 6.0, kBlack);
        line(x + 7.0, y - 6.0, x - 7.0, y + 6.0, kBlack);
    }

    void drawComicArt(SDL_FRect panel, int index)
    {
        const float x = panel.x;
        const float y = panel.y;
        const float w = panel.w;
        const float h = panel.h;
        fill({x + 10, y + 10, w - 20, h - 20}, index % 2 ? Color{98, 177, 214} : Color{221, 179, 86});
        triangle({{x + 20, y + h - 20}, {x + w * 0.36f, y + h * 0.28f}, {x + w * 0.62f, y + h - 20}},
                 {116, 93, 82});
        triangle({{x + w * 0.32f, y + h - 20}, {x + w * 0.65f, y + h * 0.22f}, {x + w - 18, y + h - 20}},
                 {134, 108, 84});

        if (index >= 0 && static_cast<size_t>(index) < comicFiles_.size()) {
            const std::string &file = comicFiles_[static_cast<size_t>(index)];
            int bi = 1;
            if ((index == 1 || index == 5) && sprites_.has(file, 2) &&
                std::fmod(nowSeconds(), 0.8) >= 0.4) {
                bi = 2;
            }
            const float scale = (w - 36.0f) / 640.0f;
            if (sprites_.draw(file, bi, x + 18.0f, y + h * 0.66f, scale)) return;
        }

        if (index == 1) {
            drawBurningBush(x + w * 0.67f, y + h * 0.52f, nowSeconds());
        }
        if (index == 3) {
            fill({x + 34.0f, y + h * 0.58f, w - 68.0f, h * 0.22f}, kWater);
            triangle({{x + w * 0.42f, y + h * 0.78f}, {x + w * 0.5f, y + h * 0.42f},
                      {x + w * 0.58f, y + h * 0.78f}},
                     {235, 211, 128});
        }
        if (index == 5) {
            fill({x + w * 0.55f, y + h * 0.55f, 90.0f, 60.0f}, kRock);
            triangle({{x + w * 0.56f, y + h * 0.65f}, {x + w * 0.42f, y + h - 20},
                      {x + w * 0.76f, y + h - 20}},
                     kWater);
        }
        if (index == 6) {
            for (int i = 0; i < 24; ++i) drawBird(x + 54 + (i * 37) % static_cast<int>(w - 100),
                                                   y + 48 + (i * 23) % static_cast<int>(h * 0.48), 8.0);
        }
        drawPerson(x + w * 0.28f, y + h * 0.72f, 2.2, false, {76, 42, 148});
        drawPerson(x + w * 0.38f, y + h * 0.74f, 1.8, false, {58, 106, 174});
    }

    void drawHelp(int contentW, int h)
    {
        drawScrollableDoc("Help", textAssets_.helpLines(), contentW, h,
                          "Scroll with wheel/PageUp/PageDown/arrows. Esc returns.");
    }
};

struct Args {
    bool smokeWindow = false;
    bool smokeScenes = false;
};

Args parseArgs(int argc, char **argv)
{
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-window") {
            args.smokeWindow = true;
        } else if (arg == "--smoke-scenes") {
            args.smokeScenes = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "usage: after-egypt-sdl3 [--smoke-window] [--smoke-scenes]\n";
            std::exit(0);
        }
    }
    return args;
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const Args args = parseArgs(argc, argv);
        AfterEgyptApp app;
        if (args.smokeScenes) return app.smokeScenes();
        return app.run(args.smokeWindow ? 3 : 0);
    } catch (const std::exception &e) {
        std::cerr << "after-egypt-sdl3: " << e.what() << '\n';
        return 1;
    }
}
