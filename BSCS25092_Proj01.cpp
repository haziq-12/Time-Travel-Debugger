// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)


#include <iostream>
#include <sstream>
#include <string>
#include <cstdint>
#include <fstream>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdio>
#include <cstdlib>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *top;
    int32_t count;

public:
    Stack()
    {
        top = nullptr;
        count = 0;
    }

    ~Stack()
    {
        while (!isEmpty()) {
            pop();
        }
    }

    void push(const T &val)
    {
        // pushes the value on the stack if max limit is not reached yet.
        if (count >= MAX_STACK_DEPTH) {
            return;
        }
        Node *newNode = new Node{val, top};
        top = newNode;
        count++;
    }

    T pop()
    {
        if (isEmpty()) {
            return T();
        }
        Node *temp = top;
        T val = temp->data;
        top = top->next;
        delete temp;
        count--;
        return val;
    }

    T &peek()
    {
        return top->data;
    }

    bool isEmpty()
    {
        return top == nullptr;
    }

    int32_t depth()
    {
        return count;
    }

    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        int32_t written = 0;
        Node *curr = top;
        while (curr != nullptr && written < maxLen) {
            out[written++] = curr->data;
            curr = curr->next;
        }
        return written;
    }
};

// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot *data;
    TimelineNode *next;
    TimelineNode *prev;
};
class Timeline
{
    TimelineNode *head, *tail;
    int32_t stepCount;

public:
    Timeline()
    {
        head = nullptr;
        tail = nullptr;
        stepCount = 0;
    }
    void record(Snapshot *s)
    {
        TimelineNode *n = new TimelineNode{s, nullptr, tail};
        if (tail != nullptr) {
            tail->next = n;
        }
        else {
            head = n;
        }
        tail = n;
        stepCount++;
    }
    TimelineNode *begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

// Core structs
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
void writeHeader(FILE *f, const TTDBHeader &h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);
    fwrite(&h.stepCount, sizeof(int32_t), 1, f);
    fwrite(&h.indexOffset, sizeof(int64_t), 1, f);
}

// resolve.bin - bookkeeping
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};

// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream &in, string &out)
{
    while (getline(in, out)) {
        if (!out.empty() && out.back() == '\r') {
            out.pop_back();
        }
        if (!out.empty()) {
            return true;
        }
    }
    return false;
}
string firstWord(const string &line)
{
    stringstream ss(line);
    string s = "";
    if (ss >> s) {
        return s;
    }
    return s;
}
string secondWord(const string &line)
{
    stringstream ss(line);
    string s1 = "", s2 = "";
    if (ss >> s1 && ss >> s2) {
        return s2;
    }
    return s2;
}
bool validateProgram(const char *sourcePath)
{
    ifstream fin(sourcePath);
    if (!fin.is_open()) {
        return false;
    }
    Stack<string> st;
    string ln;
    bool flag = false;
    while (readSourceLine(fin, ln)) {
        string s = firstWord(ln);
        if (s == "func") {
            if (!st.isEmpty()) {
                return false;
            }
            st.push(secondWord(ln));
            flag = true;
        }
        else if (s == "func_end") {
            if (st.isEmpty()) {
                return false;
            }
            st.pop();
        }
        else {
            if (st.isEmpty()) {
                return false;
            }
        }
    }
    return flag && st.isEmpty();
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE *f, int64_t offsetField, const string &text)
{
    int32_t len = text.size();
    fwrite(&offsetField, sizeof(int64_t), 1, f);
    fwrite(&len, sizeof(int32_t), 1, f);
    if (len > 0) {
        fwrite(text.data(), sizeof(char), len, f);
    }
    return 8 + 4 + len;
}
int64_t readResolveRecord(FILE *f, string &outText)
{
    int64_t offset = 0;
    int32_t len = 0;
    if (fread(&offset, sizeof(int64_t), 1, f) != 1) {
        return -1;
    }
    if (fread(&len, sizeof(int32_t), 1, f) != 1) {
        return -1;
    }
    outText.resize(len);
    if (len > 0) {
        if (fread(&outText[0], sizeof(char), len, f) != (size_t)len) {
            return -1;
        }
    }
    return offset;
}

int64_t resolveProgram(const char *sourcePath, const char *resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;

    ifstream read(sourcePath);
    if (!read.is_open()) {
        return -1;
    }
    int64_t OffsetNow = 0;
    string line;
    while (readSourceLine(read, line)) {
        string wd = firstWord(line);
        if (wd == "func") {
            if (funcCount < MAX_FUNCS) {
                funcArray[funcCount].funcName = secondWord(line);
                funcArray[funcCount].byteOffsetInResolveBin = OffsetNow;
                funcCount++;
            }
        }
        int32_t len = line.size();
        OffsetNow += (8 + 4 + len);
    }
    int64_t OffsetMain = -1;
    for (int32_t i = 0; i < funcCount; i++) {
        if (funcArray[i].funcName == "main") {
            OffsetMain = funcArray[i].byteOffsetInResolveBin;
            break;
        }
    }
    if (OffsetMain == -1) {
        read.close();
        return -1;
    }
    ifstream read1(sourcePath);
    if (!read1.is_open()) {
        read.close();
        return -1;
    }
    FILE *fout = fopen(resolveBinPath, "wb");
    if (!fout) {
        read.close();
        read1.close();
        return -1;
    }
    while (readSourceLine(read1, line)) {
        string wd = firstWord(line);
        int64_t TargetOffset = 0;
        if (wd == "call") {
            string TargetName = secondWord(line);
            bool flag = false;
            int32_t idx = 0;
            while (idx < funcCount) {
                if (funcArray[idx].funcName == TargetName) {
                    TargetOffset = funcArray[idx].byteOffsetInResolveBin;
                    flag = true;
                    break;
                }
                idx++;
            }
            if (!flag) {
                read.close();
                read1.close();
                fclose(fout);
                return -1;
            }
        }
        writeResolveRecord(fout, TargetOffset, line);
    }
    read.close();
    read1.close();
    fclose(fout);
    return OffsetMain;
}

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string &line, Token tokens[], int32_t maxTokens)
{
    stringstream ss(line);
    string word;
    int32_t count = 0;
    while (ss >> word && count < maxTokens) {
        tokens[count].text = word;
        if (count == 0) {
            tokens[count].type = KEYWORD;
        } 
        else if (count == 1) {
            tokens[count].type = IDENTIFIER;
        } 
        else {
            tokens[count].type = PARAM;
        }
        count++;
    }
    return count;
}
Snapshot *buildSnapshot(Stack<Frame> &callStack)
{
    Snapshot *s = new Snapshot();
    s->stackDepth = callStack.snapshot_into(s->callStack, MAX_STACK_DEPTH);
    return s;
}

int32_t getVarValue(Stack<Frame> &callStack, const string &name)
{
    Frame &cur = callStack.peek();
    for (int32_t i = 0; i < cur.argc; i++) {
        if (cur.argv[i].name == name) {
            return cur.argv[i].value;
        }
    }
    for (int32_t i = 0; i < cur.localCount; i++) {
        if (cur.locals[i].name == name) {
            return cur.locals[i].value;
        }
    }
    return atoi(name.c_str());
}

void setVarValue(Stack<Frame> &callStack, const string &name, int32_t val)
{
    Frame &cur = callStack.peek();
    for (int32_t i = 0; i < cur.argc; i++) {
        if (cur.argv[i].name == name) {
            cur.argv[i].value = val;
            return;
        }
    }
    for (int32_t i = 0; i < cur.localCount; i++) {
        if (cur.locals[i].name == name) {
            cur.locals[i].value = val;
            return;
        }
    }
    if (cur.localCount < MAX_VARS_PER_FRAME) {
        cur.locals[cur.localCount].name = name;
        cur.locals[cur.localCount].value = val;
        cur.localCount++;
    }
}

void jumpToOffset(FILE *&f, const char *path, int64_t targetOffset, string &outLine)
{
    fclose(f);
    f = fopen(path, "rb");
    int64_t current = 0;
    while (readResolveRecord(f, outLine) != -1) {
        if (current == targetOffset) {
            break;
        }
        current += (8 + 4 + (int32_t)outLine.size());
    }
}

void executeProgram(const char *resolveBinPath, int64_t mainOffset, Timeline &timeline)
{
    FILE *f = fopen(resolveBinPath, "rb");
    if (!f) {
        return;
    }
    Stack<Frame> callStack;
    string ln;
    jumpToOffset(f, resolveBinPath, mainOffset, ln);

    Frame mainFrame;
    mainFrame.func_name = "main";
    mainFrame.argc = 0;
    mainFrame.localCount = 0;
    mainFrame.returnLine = -1;
    callStack.push(mainFrame);

    Token tokens[MAX_TOKENS];
    while (!callStack.isEmpty()) {
        int64_t callTarget = readResolveRecord(f, ln);
        if (callTarget == -1 && ln.empty()) {
            break;
        }
        int32_t tokenCount = tokenizeLine(ln, tokens, MAX_TOKENS);
        if (tokenCount == 0) {
            continue;
        }
        string kw = tokens[0].text;
        if (kw == "set") {
            int32_t val = getVarValue(callStack, tokens[2].text);
            setVarValue(callStack, tokens[1].text, val);
            timeline.record(buildSnapshot(callStack));
        }
        else if (kw == "add" || kw == "sub" || kw == "mul" || kw == "div") {
            int32_t v1 = getVarValue(callStack, tokens[1].text);
            int32_t v2 = getVarValue(callStack, tokens[2].text);
            int32_t res = 0;

            if (kw == "add") {
                res = v1 + v2;
            }
            else if (kw == "sub") {
                res = v1 - v2;
            }
            else if (kw == "mul") {
                res = v1 * v2;
            }
            else if (kw == "div" && v2 != 0) {
                res = v1 / v2;
            }
            setVarValue(callStack, tokens[1].text, res);
            timeline.record(buildSnapshot(callStack));
        }
        else if (kw == "call") {
            Frame child;
            child.func_name = tokens[1].text;
            child.argc = 0;
            child.localCount = 0;
            child.returnLine = ftell(f);

            for (int32_t i = 2; i < tokenCount && child.argc < MAX_VARS_PER_FRAME; i++) {
                child.argv[child.argc].value = getVarValue(callStack, tokens[i].text);
                child.argc++;
            }

            string targetLn;
            jumpToOffset(f, resolveBinPath, callTarget, targetLn);

            Token defTokens[MAX_TOKENS];
            int32_t defCount = tokenizeLine(targetLn, defTokens, MAX_TOKENS);
            for (int32_t i = 2; i < defCount && (i - 2) < child.argc; i++) {
                child.argv[i - 2].name = defTokens[i].text;
            }

            callStack.push(child);
            timeline.record(buildSnapshot(callStack));
        }
        else if (kw == "func_end") {
            Frame returningFrame = callStack.pop();
            int64_t retPos = returningFrame.returnLine;
            if (!callStack.isEmpty() && retPos != -1) {
                fclose(f);
                f = fopen(resolveBinPath, "rb");
                int64_t current = 0;
                string retLine;
                while (current < retPos && readResolveRecord(f, retLine) != -1) {
                    current += (8 + 4 + (int32_t)retLine.size());
                }
                timeline.record(buildSnapshot(callStack));
            }
        }
    }
    fclose(f);
}

// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline &timeline, const char *tdbgPath)
{
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}

// main section
int32_t main()
{
    if (!validateProgram("source.bin"))
    {
        // send an error response instead of a .tdbg file
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}