### Commit 01: Setup & Initial Codebase
- **Message:** `chore: setup WSL environment and add starter code skeleton`
- **Environment Configuration:** **Installed and configured Linux (WSL)** connected directly to **VS Code**.
- **Code Scaffolding:** Initialized the **Git repository** and committed the provided **starter code skeleton**, **struct definitions**, and **template declarations**.

---

### Commit 02: Core Data Structures Implementation
- **Message:** `feat: implement linked-list based Stack and Timeline classes`
- **Stack Data Structure:** Built a **singly linked `Stack<T>`** (`push`, `pop`, `peek`, `snapshot_into`) to dynamically manage the **runtime call stack**.
- **Timeline Data Structure:** Implemented a **doubly linked `Timeline`** (`head`, `tail`, `record`, `stepCount`) to store **execution state snapshots** sequentially.


### **Commit 03: Complete Stage 0 Setup**
- **Message:** `chore: add sample source.bin to complete Stage 0 receive mock`
- **Stage 0 Setup:** Created the baseline **`source.bin`** file using the specification's **C-- demo program**.
- **Pipeline Integration:** Satisfied the **Stage 0 (Receive)** requirement to provide local source trace input for upcoming validation and execution passes.

### **Commit 04: Implement Stage 1 (Pass 0x0) Program Validation**
- **Message:** `feat: implement Stage 1 validation pass for C-- program structure`
- **File Ingestion:** Implemented `readSourceLine` to sequentially read non-empty instructions from `source.bin` while trimming carriage return artifacts.
- **Word Parsing:** Implemented `firstWord` and `secondWord` to extract instruction keywords and identifiers[cite: 10].
- **Structural Integrity:** Implemented `validateProgram` using the custom `Stack` to enforce 1:1 `func` / `func_end` matching and reject nested function declarations or premature EOF states.
