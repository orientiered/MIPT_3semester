#include <cerrno>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <cctype>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <cstring>

#include <assert.h>

constexpr unsigned MAX_CMD_SIZE = 16384; ///< maximum length of command line string in bytes
constexpr unsigned MAX_ARG_SIZE = 2048; ///< maximum length of one argument in bytes

const int STDOUT_FD = 1;
const int STDIN_FD = 0;

// #define LOGGING

#ifdef LOGGING
    #define errprintf(...) fprintf(stderr, __VA_ARGS__)
#else
    #define errprintf(...)
#endif

#define execerr(...) do {fprintf(stderr, "\033[31m"); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\033[0m"); } while(0)
#define syntaxerr(...) do {fprintf(stderr, "\033[31m"); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\033[0m"); } while(0)

/* =========================== UTILS ==================================== */

/// @brief Stack-based allocator
class memoryArena {
    char *memory = nullptr;
    size_t capacity = 0;
    size_t size = 0;

    size_t expand_buffer(size_t new_capacity);
public:
    memoryArena(size_t start_capacity) { expand_buffer(start_capacity); }
    ~memoryArena() { free(memory); }

    void clear() { size = 0; }
    void *data() {return memory; };

    // expands buffer if needed, may invalidate pointers
    void *force_alloc(size_t number, size_t elem_size);
    // doesn't invalidate pointers, but may return nullptr
    void *alloc(size_t number, size_t elem_size);
};

/// @brief Simpler pipe creation with error handling
struct pipe_fd {
    int read_fd = -1;
    int write_fd = -1;
    bool valid() {return read_fd >= 0 && write_fd >= 0; }


    void close() {
        if (valid()) {
            ::close(read_fd);
            ::close(write_fd);
        }
    }
};

pipe_fd pipe_create();

enum MicroBashStatus {
    SUCCESS = 0,
    BAD_INPUT,          ///< unclosed quotes
    BAD_PIPE,
    SYNTAX_ERROR,       ///< wrong |, <, >, etc. usage
    FORK_ERROR,
    EXIT,               ///< exit command
    EXEC_NO_FILE = 245, ///< Redirected in/out file can't be opened
    EXEC_FAIL,          ///< Execvp fail
};

/// @brief Process abstraction
/// Actual argument strings are stored in memoryArena
struct proc_t {
    std::vector<const char*> argv;

    bool pipe = false; ///< connect this and next process with pipe
    bool pass = false; ///< don't execute this process (i.e. echo abc | exit -> exit does nothing)
    const char *redirected_in  = nullptr; ///< Path for redirected stdin
    const char *redirected_out = nullptr; ///< Path for redirected stdout

    proc_t(): argv() {}

    MicroBashStatus execute(pipe_fd in, pipe_fd out);
};


enum TokenType {
    NOT_TOKEN = 0,
    KEYWORD,
    ARGUMENT,
};

enum class Keyword {
    NOT_KEYWORD = 0,
    EXIT,
    PIPE,
    REDIRECT_IN,
    REDIRECT_OUT
};

struct Token {
    // constants
    constexpr static const char PIPE_DELIMETER = '|';
    constexpr static const char REDIRECT_IN = '<';
    constexpr static const char REDIRECT_OUT = '>';

    constexpr static const char * const EXIT_COMMAND = "exit";

    static const char * keyword_to_string(Keyword kword);

    static Keyword getKeywordFromSymbol(const char symbol);

    // actual structure
    TokenType type_ = NOT_TOKEN;
    union {
        Keyword kword_;
        const char *arg_;
    };

    /// @brief Construct keyword token from symbol; may return NOT_TOKEN
    Token(const char symbol) {
        Keyword k = getKeywordFromSymbol(symbol);
        if (k != Keyword::NOT_KEYWORD) {
            type_ = KEYWORD;
            kword_ = k;
        } else {
            type_ = NOT_TOKEN;
        }
    }

    /// @brief Construct argument token from string (no copy)
    /// With search_keyword=true checks whether argument is keyword
    Token(const char *arg, bool search_keyword=true) {
        assert(arg);
        if (search_keyword && strcmp(arg, EXIT_COMMAND) == 0) {
            type_ = KEYWORD;
            kword_ = Keyword::EXIT;
        } else {
            type_ = ARGUMENT;
            arg_ = arg;
        }
    }

    void print() const;

    bool isKeyword(Keyword kword) const {return type_ == KEYWORD && kword_ == kword; }
};

struct tokenizerContext {
    memoryArena args_mem;
    std::vector<Token> tokens;
    char arg_buffer[MAX_ARG_SIZE];
    char *arg_ptr = arg_buffer;

    bool in_arg = false;
    bool quoted = false; // argument was created from qouted string -> do not interpet as bash command

    tokenizerContext(): args_mem(MAX_CMD_SIZE), tokens() {}

    void clear() {
        args_mem.clear();
        tokens.clear();
        arg_ptr = arg_buffer;
        in_arg = false;
        quoted = false;
    }

    void push_symbol(const char c) {
        *arg_ptr = c;
        arg_ptr++;
    }

    const char *save_arg(char *buffer, size_t size) {
        void *allocated_arg = args_mem.alloc(size+1, 1);
        buffer[size] = '\0';
        memcpy(allocated_arg, buffer, size + 1);

        errprintf("saved: '%s'\n", (const char *) allocated_arg);
        return (const char *) allocated_arg;
    }

    void push_arg() {
        if (in_arg) {
            in_arg = false;
            tokens.push_back(Token{save_arg(arg_buffer, size_t(arg_ptr - arg_buffer)), !quoted});
            quoted = false;
            arg_ptr = arg_buffer;
        }
    }

    void push_arg(const Token& token) {
        in_arg = false;
        quoted = false;
        arg_ptr = arg_buffer;
        tokens.push_back(token);
    }
};

struct microBash {

private:
    tokenizerContext tokenizer;
    std::vector<proc_t> proc;


    MicroBashStatus tokenize_cmd(const char *cmd);
    MicroBashStatus parse_tokens();
    MicroBashStatus execute_cmd();

    void print_propmt() {
        fprintf(stderr, "$ "); //printing to stderr because of line buffering
    }
public:
    microBash(): tokenizer(), proc() {}


    void run();
};
