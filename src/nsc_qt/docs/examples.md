# examples.h / examples.cpp

## A. Overview

This file provides a small, curated catalog of example MIPS programs offered from the "Load example…" dropdown on the Code Editor tab. Rather than a large library of generic assembly snippets, the four included programs were each chosen to demonstrate a specific pipeline behavior this simulator visualizes — back-to-back data dependencies (forwarding), a load-use stall, and a taken branch (control-hazard flush) — so picking an example doubles as a guided tour of the Datapath tab's hazard indicators. A developer reaches for `exampleProgramCatalog()` only from `MainWindow`'s Code Editor tab setup; it has no other callers in the project.

## B. Namespaces

- **`nsc::qt`** — same namespace as the rest of the Qt-GUI-specific code.

## C. Types and Type Aliases

| Name | Kind | Description |
|------|------|-------------|
| `ExampleProgram` | struct | One catalog entry: a display `name` shown in the dropdown, and the MIPS assembly `source` text loaded into the Code Editor when that entry is selected. |

## D. Constants

None declared in the header — the actual catalog contents are a function-local `static const std::vector<ExampleProgram>` inside `exampleProgramCatalog()`'s definition in the `.cpp`, not a header-visible constant.

## E. Functions

#### `const std::vector<ExampleProgram>& exampleProgramCatalog()`
Returns a reference to the fixed, ordered catalog of example programs: "Hello registers" (basic arithmetic, no hazards), "Data hazard (forwarding)", "Load-use stall", and "Branch flush (control hazard)". The returned reference is to a function-local `static` — valid for the lifetime of the program, safe to hold onto, and always describes the same four entries (the catalog is not user-editable or persisted).

## F. Dependencies

- `<QString>` — `ExampleProgram::name` and `ExampleProgram::source` are both `QString`, since the catalog is consumed directly by `QComboBox::addItem()` and `CodeEditor::setPlainText()` without any `std::string` conversion step.
- `<vector>` — the catalog container type.

No other Qt or project-internal types are used.

## G. Usage Example

```cpp
#include "nsc_qt/examples.h"

for (const auto& example : nsc::qt::exampleProgramCatalog())
    examplesComboBox->addItem(example.name);

// ...later, when the user picks entry `idx` (1-based, 0 is a placeholder):
const auto& chosen = nsc::qt::exampleProgramCatalog()[idx - 1];
codeEditor->setPlainText(chosen.source);
```
