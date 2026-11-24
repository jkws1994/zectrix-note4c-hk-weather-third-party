#pragma once
#include <stdint.h>

enum class Language : uint8_t { english, zhHK };

struct Words {
    Language language;
    bool chinese() const { return language == Language::zhHK; }
    const char* operator()(const char* en, const char* hk) const {
        return chinese() && hk ? hk : en;
    }
};
