#include "ScoreParser.h"
#include <map>
#include <utility>

std::vector<NoteEvent> parseTrack(const std::string& track) {
    std::vector<NoteEvent> events;
    size_t time = 0;
    for (char c : track) {
        if (c == '0') { time++; continue; }
        if (c == '-') {
            if (!events.empty()) {
                NoteEvent& last = events.back();
                if (last.timeStep + (size_t)last.duration == time) last.duration++;
            }
            time++;
            continue;
        }
        if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
        if ((c >= '1' && c <= '9') || (c >= 'A' && c <= 'F')) {
            NoteEvent e;
            e.timeStep = time;
            e.duration = 1;
            e.key = c;
            events.push_back(e);
            time++;
        }
    }
    return events;
}

std::vector<std::vector<NoteEvent>> splitAndParse(const std::string& content) {
    std::vector<std::vector<NoteEvent>> tracks;
    std::string cur;
    for (char c : content) {
        if (c == '/') { tracks.push_back(parseTrack(cur)); cur.clear(); }
        else if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        else cur += c;
    }
    if (!cur.empty()) tracks.push_back(parseTrack(cur));
    return tracks;
}

std::vector<ChordEvent> mergeTracks(const std::vector<std::vector<NoteEvent>>& tracks) {
    std::map<size_t, std::pair<std::vector<char>, int>> byTime;
    for (const auto& tr : tracks) {
        for (const auto& e : tr) {
            auto& slot = byTime[e.timeStep];
            slot.first.push_back(e.key);
            if (e.duration > slot.second) slot.second = e.duration;
        }
    }
    std::vector<ChordEvent> result;
    for (auto& kv : byTime) {
        ChordEvent ce;
        ce.timeStep = kv.first;
        ce.duration = kv.second.second;
        ce.keys = kv.second.first;
        result.push_back(ce);
    }
    return result;
}

std::vector<ChordEvent> parseScoreString(const std::string& content) {
    return mergeTracks(splitAndParse(content));
}