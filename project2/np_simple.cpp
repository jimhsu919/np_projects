#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <string.h>
#include <dirent.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <list>
#include <vector>
#include <queue>
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>

using namespace std;

struct pipe {
    list<string> cmd;
    int prior;
    short ptype;
}oncall;

struct numpipe {
    int prior;
    int fd[2];
    int count;
}npipe;


bool pass;
vector<pid_t> wait_;
list<string> cmd_;
vector<struct numpipe> npipe_;
vector<struct numpipe>::iterator it;
queue<int> outpipe;

void recycle(int signo) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void init() {
    pass = false;
    outpipe = {};
    wait_.clear();
    npipe = {};
    oncall = {};
    cmd_.clear();
}

bool isbuilt_in(string c) {
    char *cstr = new char[c.length() + 1];
    strcpy(cstr, c.c_str());
    char *exec = strtok(cstr, " ");
    if(string(exec) == "exit")
        exit(0);
    else if(string(exec) == "printenv") {
        char *name = strtok(NULL, " ");
        if(name == NULL) {
            char *s = *environ;
            for (int i = 1; s; i++) {
                printf("%s\n", s);
                s = *(environ + i);
            }
            return true;
        }
        char *env = getenv(name);
        if (env != NULL)
            cout << string(getenv(name)) << endl;
        else
            cout << endl;
    }
    else if(string(exec) == "setenv") {
        char *name = strtok(NULL, " ");
        char *value = strtok(NULL, " ");
        setenv(name, value, 1);
    }
    else if(string(exec) == "unsetenv") {
        char *name = strtok(NULL, " ");
        unsetenv(name);
    }
    else 
        return false;

    free(cstr);
    return true;
}

void parser(string cmd) {
    //check if blank
    if(cmd.back() == 13) cmd.pop_back();
    if(cmd.empty() || cmd.find_first_not_of(' ') == std::string::npos){
        pass = true;
        return;
    }

    int pos;
    string cut;
    while((pos = cmd.find("|")) != string::npos) {
        cut = cmd.substr(0, pos);
        cmd_.push_back(cut);
        cmd.erase(0, pos + 1);
    }
    
    //deal with the last segment
    long num;
    if((pos = cmd.find("!")) != string::npos) {
        cut = cmd.substr(0, pos);
        cmd_.push_back(cut);
        cmd.erase(0, pos + 1);

        if((num = strtol(cmd.c_str(), NULL, 10)) != 0) {
            oncall.cmd = cmd_;
            oncall.prior = num;
            oncall.ptype = 2;
        }
        else 
            pass = true;
    }
    else if((num = strtol(cmd.c_str(), NULL, 10)) != 0) {
        oncall.cmd = cmd_;
        oncall.prior = num;
        oncall.ptype = 1;
    }
    else {
        if(isbuilt_in(cmd))
           pass = true;
        cmd_.push_back(cmd);
        oncall.cmd = cmd_;
        oncall.ptype = 0;
    }
}

string cutcmd(string c, char **args) {
    char *arg, cstr[1000];
    bzero(cstr, sizeof(cstr));
    strcpy(cstr, c.c_str());
    arg = (strtok(cstr, " "));
    while(arg != NULL) {
        if(!strcmp(arg, ">")) {
            arg = strtok(NULL, " ");
            *args = NULL;
            return string(arg);
        }
        char *tmp = new char[strlen(arg)];
        strcpy(tmp, arg);
        *args++ = tmp;
        arg = strtok(NULL, " ");
    }
    *args = NULL;
    return "";
}

void close_pipe() {
    for(it = npipe_.begin(); it != npipe_.end(); it++) {
        close(it->fd[0]);
        close(it->fd[1]);
    }
}

void end_pipe(string c, short t, int prior) {
    pid_t child;
    int out_r, std_w[2];
    char *args[100];
    bool newpipe;
    string file = cutcmd(c, args);
    out_r = outpipe.empty() ? -1 : outpipe.front();
    

    if(t == 1 || t == 2) {
        newpipe = true;
        for(it = npipe_.begin(); it != npipe_.end(); it++) {
            if(it->prior == prior) {
                std_w[0] = it->fd[0];
                std_w[1] = it->fd[1];
                it->count++;
                npipe = *it;
                npipe_.erase(it);
                newpipe = false;
                break;
            }
        }
        if(newpipe) { 
            pipe(std_w);
            npipe.count = 1;
            npipe.fd[0] = std_w[0];
            npipe.fd[1] = std_w[1];
            npipe.prior = prior;
        }
    }   

    while((child = fork()) == -1) {
        waitpid(-1, NULL, 0);
    }
    if(child == 0) {
        close_pipe();
        if(out_r != -1)
            dup2(out_r, 0);
        
        if(!file.empty()) {
            mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            int fd = open(file.c_str(), O_WRONLY | O_CREAT, mode);
            if(fd != -1) dup2(fd, 1);
            else         perror("Error open");
        }
        if(t == 1 || t == 2) {
            close(std_w[0]);
            dup2(std_w[1], 1);
        }
        if(t == 2) 
            dup2(std_w[1], 2);

        signal(SIGPIPE, SIG_IGN);
        execvp(*args, args);
        if(errno == ENOENT)
            fprintf(stderr, "Unknown command: [%s].\n", *args);
        else
            perror("execvp");
        ::exit(1);
    }

    wait_.push_back(child);
    if(out_r != -1) {
        close(outpipe.front());
        outpipe.pop();
    }
    if(t == 1 || t == 2)
        npipe_.push_back(npipe);
    else {
        for(auto it = wait_.begin(); it != wait_.end(); it++)
            waitpid(*it, NULL, 0); 
    }
}


void run_pipe(string c) {
    pid_t child;
    int out_r, out_w[2];
    char *args[100];
    cutcmd(c, args);
    out_r = outpipe.empty() ? -1 : outpipe.front();

    pipe(out_w);
    while((child = fork()) == -1) {
        waitpid(-1, NULL, 0);
    }
    if(child == 0) {
        close_pipe();
        if(out_r != -1)
            dup2(out_r, 0);
        close(out_w[0]);
        dup2(out_w[1], 1);

        signal(SIGPIPE, SIG_IGN);
        execvp(*args, args);
        if(errno == ENOENT)
            fprintf(stderr, "Unknown command: [%s].\n", *args);
        else
            perror("execvp");
        ::exit(1);
    }
    else {
        wait_.push_back(child);
        close(out_w[1]);
        outpipe.push(out_w[0]);
    }

    if(out_r != -1) {
        close(outpipe.front());
        outpipe.pop();
    }
}

void init_pipein() {
    for(it = npipe_.begin(); it != npipe_.end(); it++) {
        it->prior--;
        if(it->prior == 0) {
            close(it->fd[1]);
            outpipe.push(it->fd[0]);
            npipe_.erase(it);
            it--;
        }
    }

}

void start_pipe() {
    string cmd;
    
    init_pipein();

    // save last cmd
    cmd = oncall.cmd.back();
    oncall.cmd.pop_back();
    
    for(auto it = oncall.cmd.begin(); it != oncall.cmd.end(); it++)
        run_pipe(*it);

    //end the pipe
    end_pipe(cmd, oncall.ptype, oncall.prior);
}

void shell() {
    setenv("PATH", "bin:.", 1);

    string cmd;
    while(1) {
        if(!cin) exit(0);
        init();

        cout << "% ";

        getline(cin, cmd);
        parser(cmd);
        
        if(pass) continue;
        
        start_pipe();
        
    }
}

int init_server(int port) {
    int sock, enable = 1;
    struct sockaddr_in sinfo;

    sinfo.sin_family = PF_INET;
    sinfo.sin_addr.s_addr = INADDR_ANY;
    sinfo.sin_port = htons(port);

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror("socket");
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) 
        perror("setsockopt");
    if(bind(sock, (struct sockaddr *)&sinfo, sizeof(sinfo)) < 0)
        perror("bind");
    if(listen(sock, 30) < 0)
        perror("listen");

    return sock;
}

int main(int argc, char **argv) {
    int sock, client, port;
    struct sockaddr_in sinfo, cinfo;
	socklen_t infolen = sizeof(sinfo);
    signal(SIGCHLD, recycle);

    if(argc = 2) 
        port = atoi(argv[1]);
    else {
        printf("Usage: ./np_simple [port]\n");
        exit(1);
    }

    sock = init_server(port);
    while(true) {
        pid_t child;
        
        if((client = accept(sock, (struct sockaddr*)&cinfo, &infolen)) < 0)
            perror("accept");
        
        if((child = fork()) < 0) {
            perror("fork");
        }
        else if(child == 0) {
            close(sock);
            dup2(client, 0);
            dup2(client, 1);
            dup2(client, 2);
            shell();
        }
        else {
            close(client);
            waitpid(child, NULL, 0);
        }
        
    }

    return 0;
}