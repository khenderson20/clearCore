#pragma once

#include <QString>
#include <vector>

namespace nsc::qt {

    struct ExampleProgram {
        QString name;
        QString source;
    };

    // Curated example MIPS programs. Chosen to demonstrate the pipeline features
    // this simulator actually visualizes -- forwarding, load-use stalls, and
    // branch flushes -- rather than generic "hello world" assembly. Kept short
    // (Hick's Law: a handful of well-chosen options beats a long list) and
    // ordered from simplest to most involved.
    const std::vector<ExampleProgram>& exampleProgramCatalog();

} // namespace nsc::qt