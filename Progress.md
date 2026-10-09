### Commit 1
Pehle commit mein code **skeleton** rakha gaya, jisme **WSL Linux install** karke VS Code ke sath connect kiya aur Git repository initialize karke starter code skeleton commit kiye.

### Commit 2
Dusre commit mein **linked-list based Stack and Timeline class** banai hai, jisme singly linked Stack<T> banaya runtime call stack manage karne ke liye (push, pop, peek, snapshot_into), aur doubly linked Timeline implement kiya gaya execution state ko store karne ke liye (head, tail, record, stepCount).

### Commit 3
Teesre commit mein sample source.bin banai to complete **Stage 0**, jisme C-- demo program ke mutabiq baseline source.bin file create ki gayi taake Stage 0 (Receive) ki requirement poori ho sake aur agle validation aur execution passes ke liye input ready ho jaye.

### Commit 4
Chauthe commit mein Stage 1 validation ki for C-- program, jisme **readSourceLine** banaya gaya taake source.bin se carriage return clean karke instructions parhi ja sakein, keywords aur identifiers extract karne ke liye **firstWord aur secondWord** likha gaya, aur custom Stack use karke **validateProgram** implement kiya gaya taake 1:1 func / func_end matching ho sake aur nested functions khatam ho sakein.

### Commit 5
panchve commit mein sab se pehlay hamny **readRoseolveRecord** and **writeResolveRecord** ka function banaya jin ka maqsad offsets ko write karna hai aur readResolveRecord ka kaam offsets read karna and and debugger ko ye batana hai ke call ka keyword aye to kidhr jump karna hai. Isky ilawa hamny **resolveProgram** ka function likha hai jiska maqsad har instruction ka offset save karna hai aur jab koi call instruction aye to usy usky ofset ke sath link karna hai.
