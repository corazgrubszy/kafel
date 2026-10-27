#include "appearance.hpp"
#include <cstdio>
// Offline build tool: the weather-owned table is the only WMO source of truth.
int main(int argc, char **) {
    if (argc != 1)
        return 2;
    for (const auto &entry : kafel::weather::appearance_catalog())
        std::printf("%u\t%.*s\t%.*s\t%.*s\n", entry.code, int(entry.day.size()), entry.day.data(),
                    int(entry.night.size()), entry.night.data(), int(entry.description.size()),
                    entry.description.data());
}
