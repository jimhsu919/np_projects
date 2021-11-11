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

using namespace std;

#define PIPEBUF 10000

extern char **environ;

struct numpipe {
    list<string> cmd;
    int prior;
    short ptype;
}npipe, oncall;

struct nbuffer {
    string buf;
    int prior;
}numbuf;


char buf[PIPEBUF];
bool pass;
string onbuf, savebuf;
vector<string> files;
list<string> cmd_;
vector<struct nbuffer> npipe_;
vector<struct nbuffer>::iterator it;
queue<pid_t> wait_;
queue<int> outpipe;

bool isexec(const char* file) {
    struct stat st;
    if(stat(file, &st) < 0)
        return false;
    if(S_ISDIR(st.st_mode))
        return false;
    if((st.st_mode & S_IEXEC) != 0)
        return true;

    return false;
}

void exec_list() {
    files.clear();
    struct dirent *exec;
    DIR *dr;
    char *env = getenv("PATH");
    char *path = new char[strlen(env)];
    strcpy(path, env);
    char *p = strtok(path, ":");
    while(p != NULL) {
        dr = opendir(p);
        while(exec = readdir(dr)) {
            if(isexec((string(p) + "/" + string(exec->d_name)).c_str()))
                files.push_back(string(exec->d_name));
        }
        p = strtok(NULL, ":");
    }
    free(path);
}

void init() {
    pass = false;
    onbuf.clear();
    savebuf.clear();
    outpipe = {};
    wait_ = {};
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
        cout << string(getenv(name)) << endl;
        return true;
    }
    else if(string(exec) == "setenv") {
        char *name = strtok(NULL, " ");
        char *value = strtok(NULL, " ");
        setenv(name, value, 1);
        exec_list();
        return true;
    }

    free(cstr);
    return false;
}

void parser(string cmd) {
    //check if blank
    if(cmd.empty() || cmd.find_first_not_of(' ') == std::string::npos){
        pass = true;
        return;
    }
    
    //push oncall number pipe to onbuf
    for(it = npipe_.begin(); it != npipe_.end(); it++) {
        it->prior -= 1;
        if(it->prior == 0) {
            onbuf += it->buf;
            npipe_.erase(it);
            it--;
        }
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

char* cutcmd(string c, char **args) {
    char *arg;
    char *cstr = new char[c.length() + 1];
    strcpy(cstr, c.c_str());
    arg = (strtok(cstr, " "));
    while(arg != NULL) {
        if(!strcmp(arg, ">")) {
            arg = strtok(NULL, " ");
            break;
        }
        *args++ = arg;
        arg = strtok(NULL, " ");
    }
    *args = NULL;
    return arg;
}

bool iscmd(string c) {
    char *cstr = new char[c.length() + 1];
    strcpy(cstr, c.c_str());
    char *name = strtok(cstr, " ");
    for(int i = 0; i < files.size(); i++) {
        if(string(name) == files[i])
            return true;
    }

    fprintf(stderr, "Unknown command: [%s].\n", name);
    free(cstr);
    return false;
}

void flush_pipe(int out) {
    if(out != -1) {
        close(outpipe.front());
        outpipe.pop();
        waitpid(wait_.front(), NULL, 0);
        wait_.pop();
    }
}

void end_pipe(string c, short t) {
    pid_t child;
    int out_r, out_s[2], err_s[2];
    char *args[100], *file;
    file = cutcmd(c, args);
    out_r = outpipe.empty() ? -1 : outpipe.front();

    if(t == 1 || t == 2) pipe(out_s);
    if(t == 2)           pipe(err_s);

    if((child = fork()) == -1) {
        perror("Error fork");
        exit(1);
    }
    else if(child == 0) {
        if(out_r != -1)
            dup2(out_r, 0);
        
        if(file != NULL) {
            mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            int fd = open(file, O_WRONLY | O_CREAT, mode);
            if(fd != -1) dup2(fd, 1);
            else         perror("Error open");
        }
        if(t == 1 || t == 2) {
            close(out_s[0]);
            dup2(out_s[1], 1);
        }
        if(t == 2) {
            close(err_s[0]);
            dup2(err_s[1], 2);
        }
        if(!iscmd(c)) 
            exit(1);
        signal(SIGPIPE, SIG_IGN);
        execvp(*args, args);
    }

    if(t == 2) {
        close(err_s[1]);
        while(read(err_s[0], buf, PIPEBUF) != 0) {
            savebuf += string(buf);
            bzero(buf, strlen(buf));
        }
        close(err_s[0]);
    }

    if(t == 1 || t == 2) {
        close(out_s[1]);
        while((read(out_s[0], buf, PIPEBUF)) != 0) {
            savebuf += string(buf);
            bzero(buf, strlen(buf));
        }
        close(out_s[0]);
    }

    flush_pipe(out_r);
    waitpid(child, NULL, 0);
}


void run_pipe(string c) {
    pid_t child;
    int out_r, out_w[2];
    char *args[100];
    cutcmd(c, args);
    out_r = outpipe.empty() ? -1 : outpipe.front();

    pipe(out_w);
    if((child = fork()) == -1) {
        perror("Error fork");
        exit(1);
    }
    else if(child == 0) {
        if(out_r != -1)
            dup2(out_r, 0);
        close(out_w[0]);
        dup2(out_w[1], 1);
        if(!iscmd(c))
            exit(1);
        signal(SIGPIPE, SIG_IGN);
        execvp(*args, args);
    }
    else {
        close(out_w[1]);
        outpipe.push(out_w[0]);
        wait_.push(child); 
    }

    flush_pipe(out_r);
}

void init_pipein(string c) {
    pid_t child;
    int out[2];

    pipe(out);
    if((child = fork()) == -1) {
        perror("Error fork");
        exit(1);
    }
    else if(child == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, 0);
        close(out[0]);
        write(out[1], c.c_str(), c.length());
        exit(0);
    }
    else {
        close(out[1]);
        outpipe.push(out[0]);
        wait_.push(child);
    }
}

void start_pipe() {
    string cmd_last;
    
    if(!onbuf.empty())
        init_pipein(onbuf);

    // save last cmd
    cmd_last = oncall.cmd.back();
    oncall.cmd.pop_back();
    
    for(auto it = oncall.cmd.begin(); it != oncall.cmd.end(); it++)
        run_pipe(*it);

    //end the pipe
    end_pipe(cmd_last, oncall.ptype);

    //save number pipe output
    if(!savebuf.empty()) { 
        numbuf.buf = savebuf;
        numbuf.prior = oncall.prior;
        npipe_.push_back(numbuf);
    }
}

int main() {
    setenv("PATH", "bin:.", 1);
    exec_list();

    //shell
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

    return 0;
}