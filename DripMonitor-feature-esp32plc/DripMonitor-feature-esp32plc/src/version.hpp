#pragma once
#include <cstdio>

#include "version_info.h"

class Version {
public:
    static const char* getVersion() { return GIT_VERSION; }
    static const char* getCommitHash() { return GIT_COMMIT_HASH; }
    static const char* getBranch() { return GIT_BRANCH; }
    static const char* getBuildTime() { return BUILD_TIMESTAMP; }
    
    // Formatted version string for display
    static const char* getFullVersionString() {
        static char version_str[256];
        snprintf(version_str, sizeof(version_str), 
                "v%s (%s)\n%s\nBuilt: %s", 
                GIT_VERSION, GIT_COMMIT_HASH, GIT_BRANCH, BUILD_TIMESTAMP);
        return version_str;
    }
}; 