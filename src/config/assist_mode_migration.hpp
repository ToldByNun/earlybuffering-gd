#ifndef ASSIST_MODE_MIGRATION_HPP
#define ASSIST_MODE_MIGRATION_HPP

#include <string>

class EbAssistModeMigration {
public:
    static char const* canonical(std::string const& stored);

private:
    EbAssistModeMigration() = delete;
};

#endif
