#include "microBash.hpp"

#include<sys/types.h>
#include<sys/stat.h>

/* ==================== UTILS ==================================== */

size_t memoryArena::expand_buffer(size_t new_capacity) {
    if (new_capacity <= capacity) return capacity;
    char *new_memory = (char *) realloc(memory, new_capacity);
    if (!new_memory) {
        fprintf(stderr, "Failed to allocate memory");
        exit(EXIT_FAILURE);
    }

    capacity = new_capacity;
    memory = new_memory;
    return new_capacity;
}

void *memoryArena::force_alloc(size_t number, size_t elem_size) {
    if (size + number*elem_size > capacity) {
        expand_buffer(2*(size + number*elem_size));
    }

    char *result = memory + size;
    size += number*elem_size;
    return result;
}

void *memoryArena::alloc(size_t number, size_t elem_size) {
    if (size + number*elem_size > capacity) {
        return nullptr;
    }

    char *result = memory + size;
    size += number*elem_size;
    return result;
}


pipe_fd pipe_create() {
    int fd[2] = {-1, -1};
    if (pipe(fd) < 0) {
        perror("Failed to create pipe");
        exit(1);
    }

    return pipe_fd{fd[0], fd[1]};
}

/* ==================== PROCESS ABSTRACTION ====================== */
MicroBashStatus proc_t::execute(pipe_fd in, pipe_fd out) {
    // creating pipes
    if (in.valid()) {
        close(in.write_fd);
        dup2(in.read_fd, STDIN_FD);
        close(in.read_fd);
    }

    if (pipe && out.valid()) {
        close(out.read_fd);
        dup2(out.write_fd, STDOUT_FD);
        close(out.write_fd);
    }

    // redirections have higher priority than pipes
    if (redirected_in) {
        int in_fd = open(redirected_in, O_RDONLY);
        if (in_fd < 0) {
            execerr("microBash: failed to open '%s':'%s'\n", redirected_in, strerror(errno));
            return EXEC_NO_FILE;
        }
        dup2(in_fd, STDIN_FD);
        close(in_fd);
    }

    if (redirected_out) {
        umask(0);
        int out_fd = open(redirected_out, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out_fd < 0) {
            execerr("microBash: failed to open '%s':'%s'\n", redirected_out, strerror(errno));
            return EXEC_NO_FILE;
        }
        dup2(out_fd, STDOUT_FD);
        close(out_fd);
    }

    // executing
    if (pass) return SUCCESS;
    if (argv.back() != nullptr) argv.push_back(nullptr); // last argument must be nullptr
    if (execvp(argv[0], (char * const *)argv.data()) < 0) {
        execerr("microBash: failed to execute '%s':'%s'\n", argv[0], strerror(errno));
        return EXEC_FAIL;
    }
    return SUCCESS;
}

/* ============================ Token ================================ */
const char * Token::keyword_to_string(Keyword kword) {
    switch(kword) {
        case Keyword::NOT_KEYWORD: return "NONE";
        case Keyword::EXIT: return "EXIT";
        case Keyword::PIPE: return "PIPE";
        case Keyword::REDIRECT_IN: return "REDIR_IN";
        case Keyword::REDIRECT_OUT: return "REDIR_OUT";
        default: return "UNKNOWN";
    }
}

Keyword Token::getKeywordFromSymbol(const char symbol) {
    switch(symbol) {
        case PIPE_DELIMETER:
            return Keyword::PIPE;
        case REDIRECT_IN:
            return Keyword::REDIRECT_IN;
        case REDIRECT_OUT:
            return Keyword::REDIRECT_OUT;
        default:
            return Keyword::NOT_KEYWORD;
    }
}

void Token::print() const {
    switch(type_) {
        case ARGUMENT:
            errprintf("A:'%s' ", arg_);
            break;
        case KEYWORD:
            errprintf("K:'%s' ", keyword_to_string(kword_));
            break;
        case NOT_TOKEN:
            errprintf("N ");
            break;
        default:
            errprintf("U ");
            break;
    }
}
/* ============================ microBash ============================ */

/**
 * @brief Parse cmd creating vector of tokens (pointer to string argument or microBash keyword)
*/
MicroBashStatus microBash::tokenize_cmd(const char *cmd) {

    for (const char *cmd_ptr = cmd; *cmd_ptr != '\0';) {
        // quoted string
        if (*cmd_ptr == '"') {
            tokenizer.in_arg = true;
            tokenizer.quoted = true;
            cmd_ptr++;
            while (*cmd_ptr && *cmd_ptr != '"') {
                tokenizer.push_symbol(*cmd_ptr);
                cmd_ptr++;
            }

            if (*cmd_ptr == '"') {
                cmd_ptr++;
            } else {
                syntaxerr("Syntax error: unclosed qoutes\n");
                return BAD_INPUT;
            }
        // one-symbol keywords
        } else if (Token::getKeywordFromSymbol(*cmd_ptr) != Keyword::NOT_KEYWORD) {
            tokenizer.push_arg();
            tokenizer.push_arg(Token{*cmd_ptr});
            cmd_ptr++;
        // regular argument
        } else if (!isspace(*cmd_ptr)) {
            tokenizer.in_arg = true;
            tokenizer.push_symbol(*cmd_ptr);
            cmd_ptr++;
        // space
        } else {
            tokenizer.push_arg();

            while (*cmd_ptr && isspace(*cmd_ptr)) {
                cmd_ptr++;
            }
        }
    }

    tokenizer.push_arg();

    return SUCCESS;
}

/// @brief Parse vector of tokens creating vector of processes
/// Process is an argv vector + redirected in/out paths + additional info
MicroBashStatus microBash::parse_tokens() {
    std::vector<Token>& tokens = tokenizer.tokens;
    if (tokens.empty()) return SUCCESS;

    if (tokens.front().isKeyword(Keyword::EXIT)) {
        return EXIT;
    }

    if (tokens.front().isKeyword(Keyword::PIPE) || tokens.back().isKeyword(Keyword::PIPE)) {
        syntaxerr("Syntax error: Pipe must be used between commands\n");
        return SYNTAX_ERROR;
    }

    proc_t current = proc_t();

    for (size_t idx = 0; idx < tokens.size(); idx++) {
        if (tokens[idx].type_ == KEYWORD) {
            switch(tokens[idx].kword_) {
                case Keyword::REDIRECT_IN:
                    idx++;
                    if (idx >= tokens.size() || tokens[idx].type_ != ARGUMENT) {
                        syntaxerr("Syntax error: no argument after < \n");
                        return SYNTAX_ERROR;
                    }
                    current.redirected_in = tokens[idx].arg_;
                    break;
                case Keyword::REDIRECT_OUT:
                    idx++;
                    if (idx >= tokens.size() || tokens[idx].type_ != ARGUMENT) {
                        syntaxerr("Syntax error: no argument after < \n");
                        return SYNTAX_ERROR;
                    }
                    current.redirected_out = tokens[idx].arg_;
                    break;
                case Keyword::EXIT:
                    current.pass = true;
                    break;
                case Keyword::PIPE:
                    if (current.argv.empty()) {
                        syntaxerr("Syntax error: no process to pipe\n");
                        return SYNTAX_ERROR;
                    }
                    current.pipe = true;
                    proc.push_back(current);
                    current = proc_t();
                    break;
                case Keyword::NOT_KEYWORD:
                default:
                    syntaxerr("Unknown keyword: tokens[%zu] = %d\n", idx, (int)tokens[idx].kword_);
                    exit(EXIT_FAILURE);
            }
        } else {
            current.argv.push_back(tokens[idx].arg_);
        }
    }

    if (!current.argv.empty()) proc.push_back(current);

    return SUCCESS;
}

/// @brief Execute processes from proc vector
MicroBashStatus microBash::execute_cmd() {
    if (proc.empty()) return SUCCESS;

    const pipe_fd invalid = pipe_fd{};
    pipe_fd in = invalid, out = invalid;

    for (size_t proc_idx = 0; proc_idx < proc.size(); proc_idx++) {
        in.close();
        in = out;
        if (proc_idx != proc.size() - 1)
            out = pipe_create();
        else
            out = invalid;

        pid_t pid = fork();
        if (pid < 0) {
            execerr("Failed to fork:%s\n", strerror(errno));
            return FORK_ERROR;
        } else if (pid == 0) {
            exit(proc[proc_idx].execute(in, out));
        }

    }

    in.close();
    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {}
    errprintf("Status: all processes ended\n");

    return SUCCESS;
}


/*======================== Core microBash function ============================*/
/// @brief microBash execute loop
void microBash::run() {
    char buffer[MAX_CMD_SIZE];

    while (true) {
        // printing prompt and reading user input
        print_propmt();
        bool continue_read = fgets(buffer, MAX_CMD_SIZE, stdin);
        if (!continue_read) break;

        // clearing previous arguments
        tokenizer.clear();
        proc.clear();

        // tokenization
        MicroBashStatus status = tokenize_cmd(buffer);
        if (status != SUCCESS) continue;

        errprintf("Done: tokenization\n");

        #ifdef LOGGING
        for (const Token& token: tokenizer.tokens) {
            token.print();
        }
        errprintf("\n");
        #endif

        // creating array of processes
        status = parse_tokens();
        errprintf("Done: token parsing (%d)\n", (int) status);
        if (status == EXIT) {
            break;
        } else if (status != SUCCESS) {
            continue;
        }

        #ifdef LOGGING
        for (const proc_t& process: proc) {
            errprintf("argc = %zu;", process.argv.size());
            for (const char *arg: process.argv) {
                errprintf("'%s' ", arg);
            }
            errprintf("\n");
        }
        #endif

        // executing processes
        status = execute_cmd();

        errprintf("Done: execution (%d)\n", (int) status);
    }

}
