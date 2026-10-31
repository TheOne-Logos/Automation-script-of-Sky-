#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct NoteEvent {
    size_t timeStep;
    int    duration;
    char   key;
    NoteEvent() : timeStep(0), duration(0), key(0) {}
};

struct ChordEvent {
    size_t timeStep;
    int    duration;
    std::vector<char> keys;
    ChordEvent() : timeStep(0), duration(0) {}
};

std::vector<NoteEvent> parseTrack(const std::string& track);
std::vector<std::vector<NoteEvent>> splitAndParse(const std::string& content);
std::vector<ChordEvent> mergeTracks(const std::vector<std::vector<NoteEvent>>& tracks);
std::vector<ChordEvent> parseScoreString(const std::string& content);