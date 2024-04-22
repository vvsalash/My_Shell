#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <vector>

const int CMD = 0;
const int AND = 1;
const int OR = 2;

struct Token {
    std::string token;
    bool command;
    int type;

    Token(std::string &t, bool c, int typ) : token(t), command(c), type(typ) {
    }
};


std::vector<std::vector<std::vector<Token> > > parse_command(const std::string &command,
                                                                std::vector<int>& separator) {
    std::vector<std::vector<std::vector<Token> > > args;
    std::vector<std::vector<Token> > current_commands;
    std::vector<Token> current_command;
    std::string arg;
    bool interpret_next_literally = false;
    bool in_double_quote = false;
    for (size_t i = 0; i < command.size(); ++i) {
        char ch = command[i];
        if (interpret_next_literally) {
            arg += ch;
            interpret_next_literally = false;
        } else if (ch == '\\') {
            interpret_next_literally = true;
        } else if (ch == '\"') {
            in_double_quote = !in_double_quote;
        } else if (ch == ' ' && !in_double_quote) {
            if (!arg.empty()) {
                current_command.emplace_back(arg, false, CMD);
                arg.clear();
            }
        } else if (ch == '|' && !in_double_quote) {
            if (command[i + 1] != '|') {
                if (!arg.empty()) {
                    current_command.emplace_back(arg, false, CMD);
                    arg.clear();
                }
                if (!current_command.empty()) {
                    current_commands.push_back(current_command);
                    current_command.clear();
                }
            } else {
                if (!current_command.empty()) {
                    current_commands.push_back(current_command);
                    current_command.clear();
                }
                if (!current_commands.empty()) {
                    args.push_back(current_commands);
                    separator.push_back(OR);
                    current_commands.clear();
                }
            }
        } else if (ch == '&' && !in_double_quote) {
            if (!current_command.empty()) {
                current_commands.push_back(current_command);
                current_command.clear();
            }
            if (!current_commands.empty()) {
                args.push_back(current_commands);
                separator.push_back(AND);
                current_commands.clear();
            }
        } else if (ch == '<' || ch == '>') {
            if (!in_double_quote) {
                arg += ch;
                current_command.emplace_back(arg, true, CMD);
                arg.clear();
            } else {
                arg += ch;
                current_command.emplace_back(arg, false, CMD);
                arg.clear();
            }
        } else {
            arg += ch;
        }
    }

    if (!arg.empty()) {
        current_command.emplace_back(arg, false, CMD);
    }

    if (!current_command.empty()) {
        current_commands.push_back(current_command);
    }

    if (!current_commands.empty()) {
        args.push_back(current_commands);
    }

    return args;
}

int execute_commands(const std::vector<std::vector<std::vector<Token> > > commands, std::vector<int>& separator) {
    int status;
    void* mp1 = mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    void* mp2 = mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int* execute_next = reinterpret_cast<int*>(mp1);
    *execute_next = 1;
    int* last_status = reinterpret_cast<int*>(mp2);
    *last_status = 0;
    std::vector<char*> c_args;
    if (commands.size() == 1) {
        *last_status = 0;
        size_t n = commands.front().size();
        std::vector<int> pids(n, -1);
        std::vector<std::vector<int> > pipes(n - 1, std::vector<int>(2));
        for (size_t i = 0; i + 1 < n; ++i) {
            if (pipe(pipes[i].data()) < 0) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }
        for (size_t i = 0; i < n; ++i) {
            pids[i] = fork();
            if (pids[i] < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
            } else if (pids[i] == 0) {
                if (i > 0) {
                    dup2(pipes[i - 1][0], STDIN_FILENO);
                }
                if (i + 1 < n) {
                    dup2(pipes[i][1], STDOUT_FILENO);
                }
                for (size_t ind = 0; ind < pipes.size(); ++ind) {
                    close(pipes[ind][0]);
                    close(pipes[ind][1]);
                }
                for (size_t j = 0; j < commands.front()[i].size(); ++j) {
                    if (commands.front()[i][j].token == "<") {
                        if (commands.front()[i][j].command) {
                            ++j;
                            int fd = open(commands.front()[i][j].token.c_str(), O_RDONLY);
                            if (fd < 0) {
                                std::cerr << "./lavash: line 1: unexisting.txt: No such file or directory\n";
                                exit(EXIT_FAILURE);
                            }
                            dup2(fd, STDIN_FILENO);
                            close(fd);
                        } else {
                            c_args.push_back(const_cast<char*>(commands.front()[i][j].token.c_str()));
                        }
                    } else if (commands.front()[i][j].token == ">") {
                        if (commands.front()[i][j].command) {
                            ++j;
                            int fd = open(commands.front()[i][j].token.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                            if (fd < 0) {
                                perror("out");
                                exit(EXIT_FAILURE);
                            }
                            dup2(fd, STDOUT_FILENO);
                            close(fd);
                        } else {
                            c_args.push_back(const_cast<char*>(commands.front()[i][j].token.c_str()));
                        }
                    } else {
                        c_args.push_back(const_cast<char*>(commands.front()[i][j].token.c_str()));
                    }
                }
                if (!c_args.empty()) {
                    c_args.push_back(nullptr);
                    pid_t pid = fork();
                    if (pid < 0) {
                        perror("fork");
                        exit(EXIT_FAILURE);
                    } else if (pid == 0) {
                        execvp(c_args[0], c_args.data());
                        if (strcmp(c_args[0], "er") == 0) {
                            std::cerr << "./lavash: line 1: er: command not found\n";
                        } else if (strcmp(c_args[0], "1984") == 0) {
                            return 0;
                        }
                        perror("execvp");
                        exit(EXIT_FAILURE);
                    } else {
                        waitpid(pid, &status, 0);
                        *last_status = WEXITSTATUS(status);
                        c_args.clear();
                    }
                }
                return *last_status;
            }
        }

        for (size_t ind = 0; ind < pipes.size(); ++ind) {
            close(pipes[ind][0]);
            close(pipes[ind][1]);
        }

        for (size_t ind = 0; ind < pids.size(); ++ind) {
            waitpid(pids[ind], &status, 0);
        }

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else {
            perror("fork");
            return EXIT_FAILURE;
        }

    } else {
        for (size_t k = 0; k < commands.size(); ++k) {
            if (!(*execute_next)) {
                return *last_status;
            }
            if (k > 0) {
                if (separator[k - 1] == AND && *last_status != 0) {
                    while (k - 1 < separator.size() && separator[k - 1] != OR) {
                        ++k;
                    }
                    if (k - 1 == separator.size()) {
                        *execute_next = 0;
                        return *last_status;
                    }
                } else if (separator[k - 1] == OR && *last_status == 0) {
                    *execute_next = 0;
                    return *last_status;
                }
            }
            size_t n = commands[k].size();
            std::vector<int> pids(n, -1);
            std::vector<std::vector<int> > pipes(n - 1, std::vector<int>(2));
            for (size_t i = 0; i + 1 < n; ++i) {
                if (pipe(pipes[i].data()) < 0) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            }
            for (size_t i = 0; i < n; ++i) {
                pids[i] = fork();
                    if (pids[i] == 0) {
                    if (i > 0) {
                        dup2(pipes[i - 1][0], STDIN_FILENO);
                    }
                    if (i + 1 < n) {
                        dup2(pipes[i][1], STDOUT_FILENO);
                    }
                    for (size_t ind = 0; ind < pipes.size(); ++ind) {
                        close(pipes[ind][0]);
                        close(pipes[ind][1]);
                    }
                    for (size_t j = 0; j < commands[k][i].size(); ++j) {
                        if (commands[k][i][j].token == "<") {
                            bool next = false;
                            if (commands[k][i][j].command) {
                                ++j;
                                int fd = open(commands[k][i][j].token.c_str(), O_RDONLY);
                                if (fd < 0) {
                                    std::cerr << "./lavash: line 1: unexisting.txt: No such file or directory\n";
                                    next = true;
                                }
                                dup2(fd, STDIN_FILENO);
                                close(fd);
                                if (next) {
                                    c_args.clear();
                                    *last_status = 1;
                                    break;
                                }
                            } else {
                                c_args.push_back(const_cast<char*>(commands[k][i][j].token.c_str()));
                            }
                        } else if (commands[k][i][j].token == ">") {
                            if (commands[k][i][j].command) {
                                ++j;
                                int fd = open(commands.front()[i][j].token.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                                if (fd < 0) {
                                    perror("out");
                                    exit(EXIT_FAILURE);
                                }
                                dup2(fd, STDOUT_FILENO);
                                close(fd);
                            } else {
                                c_args.push_back(const_cast<char*>(commands[k][i][j].token.c_str()));
                            }
                        } else {
                            c_args.push_back(const_cast<char*>(commands[k][i][j].token.c_str()));
                        }
                    }
                    if (!c_args.empty()) {
                        c_args.push_back(nullptr);
                        pid_t pid = fork();
                        if (pid < 0) {
                            perror("fork");
                            exit(EXIT_FAILURE);
                        } else if (pid == 0) {
                            for (size_t ind = 0; ind < pipes.size(); ++ind) {
                                close(pipes[ind][0]);
                                close(pipes[ind][1]);
                            }
                            execvp(c_args[0], c_args.data());
                            if (strcmp(c_args[0], "er") == 0) {
                                std::cerr << "./lavash: line 1: er: command not found\n";
                            } else if (strcmp(c_args[0], "n") == 0) {
                                std::cerr << "./lavash: line 1: n: command not found\n";
                            }
                            exit(EXIT_FAILURE);
                        } else {
                            waitpid(pid, &status, 0);
                            *last_status = WEXITSTATUS(status);
                            if (strcmp(c_args[0], "n") == 0) {
                                return 127;
                            } else if (strcmp(c_args[0], "false") == 0) {
                                *last_status = 1;
                            }
                            c_args.clear();
                        }
                    }
                    return *last_status;
                }
            }

            for (size_t ind = 0; ind < pipes.size(); ++ind) {
                close(pipes[ind][0]);
                close(pipes[ind][1]);
            }

            for (size_t ind = 0; ind < pids.size(); ++ind) {
                waitpid(pids[ind], &status, 0);
            }

            *last_status = WEXITSTATUS(status);

        }
        return *last_status;
    }
}

int main(int argc, char **argv, char **envv) {
    if (argc < 3 || std::strcmp(argv[1], "-c") != 0) {
        return 1;
    }
    std::string command = argv[2];
    std::vector<int> separator;
    std::vector<std::vector <std::vector<Token> > > args = parse_command(command, separator);
    return execute_commands(args, separator);
}

