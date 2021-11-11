#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <dirent.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <list>
#include <vector>
#include <map>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace std;

const char *welcome = "****************************************\n"
                      "** Welcome to the information server. **\n"
                      "****************************************\n";

struct pipe {
    list<string> cmd;
    int prior;
    short ptype;
}oncall;

struct numpipe {
    int prior;
    int fd[2];
}npipe;

struct userinfo {
    int id;
    string name;
    string ip;
    map<string, string> envset;
    vector<struct numpipe> npipe;
}uinfo;

struct sendinfo {
    int sock;
    map<int, int> sendto;
}sinfo;

int user;
string command;
vector<string> files;
list<string> cmd_;
list<int> outpipe;
vector<pid_t> wait_;
map<int, sendinfo> sendbox;
map<int, userinfo> users;
map<int, userinfo>::iterator iter;


void recycle(int signo) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void broadcast(string m) {
    for(iter = users.begin(); iter != users.end(); iter++)
        write(iter->first, m.c_str(), m.length());
}

void remove_user(int fd) {
    close(fd);
    for(auto it = users[fd].npipe.begin(); it != users[fd].npipe.end(); it++) {
        close(it->fd[0]);
        close(it->fd[1]);
    }
    for(auto it = sendbox[users[fd].id].sendto.begin(); it != sendbox[users[fd].id].sendto.end(); it++)
        close(it->second);
    sendbox.erase(sendbox.find(users[fd].id));
    for(auto it = sendbox.begin(); it != sendbox.end(); it++) {
        if(it->second.sendto.find(users[fd].id) != it->second.sendto.end()) {
            close(it->second.sendto[users[fd].id]);
            it->second.sendto.erase(it->second.sendto.find(users[fd].id));
        }
    }
    string logout = "*** User '" + users.find(fd)->second.name + "' left. ***\n";
    users.erase(users.find(fd));
    broadcast(logout);
}

void add_user(int fd, struct sockaddr_in cinfo) {
    uinfo = {};
    uinfo.name = "(no name)";
    uinfo.ip = string(inet_ntoa(cinfo.sin_addr)) + ":" + to_string(ntohs(cinfo.sin_port));
    uinfo.envset["PATH"] = "bin:.";
    sinfo.sock = fd;

    write(fd, welcome, strlen(welcome));
    for(int i = 1; i <= 30; i++) {
        for(iter = users.begin(); iter != users.end(); iter++) {
            if(iter->second.id == i) break;
        }
        if(iter == users.end()) {
            uinfo.id = i;
            users[fd] = uinfo;
            sendbox[i] = sinfo;
            break;
        }    
    }
    string login = "*** User '(no name)' entered from " + uinfo.ip + ". ***\n";
    broadcast(login);
    write(fd, "% ", 2);
}

void init_shell(int fd) {
    user = fd;
    command.clear();
    outpipe.clear();
    npipe = {};
    oncall = {};
    cmd_.clear();
    wait_.clear();
    clearenv();
    for(auto it = users[user].envset.begin(); it != users[user].envset.end(); it++) 
        setenv(it->first.c_str(), it->second.c_str(), 1);
}

bool isbuilt_in(string c) {
    string message;
    char *env;
    char *cstr = new char[c.length() + 1];
    strcpy(cstr, c.c_str());
    char *exec = strtok(cstr, " ");
    if(string(exec) == "exit")
        remove_user(user);
    else if(string(exec) == "printenv") {
        char *name = strtok(NULL, " ");
        if(name == NULL) {
            char *env = *environ;
            for (int i = 1; env; i++) {
                write(user, env, strlen(env));
                write(user, "\n", 1);
                env = *(environ + i);
            }
            return true;
        }
        env = getenv(name);
        if (env != NULL) {
            write(user, getenv(name), strlen(getenv(name)));
            write(user, "\n", 1);
        }
        else
            write(user, "\n", 1);
    }
    else if(string(exec) == "setenv") {
        char *name = strtok(NULL, " ");
        char *value = strtok(NULL, " ");
        users[user].envset[string(name)] = string(value);
    }
    else if(string(exec) == "unsetenv") {
        char *name = strtok(NULL, " ");
        users[user].envset.erase(string(name));
    }
    else if(string(exec) == "who") {
        message = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
        write(user, message.c_str(), message.length());
        for(auto it = sendbox.begin(); it != sendbox.end(); it++) {
            iter = users.find(it->second.sock);
            if(it->first == users[user].id)
                message = to_string(iter->second.id) + "\t" + iter->second.name + "\t" + iter->second.ip + "\t" + "<-me\n";
            else
                message = to_string(iter->second.id) + "\t" + iter->second.name + "\t" + iter->second.ip + "\n";
            write(user, message.c_str(), message.length());
        }
    }
    else if(string(exec) == "yell") {
        char *msg = strtok(NULL, "");
        message = "*** " + users[user].name + " yelled ***: " + string(msg) + "\n";
        broadcast(message);
    }
    else if(string(exec) == "name") {
        char *name = strtok(NULL, " ");
        for(iter = users.begin(); iter != users.end(); iter++)
            if(iter->second.name == string(name) && (iter->first != user)) break;
        if(iter == users.end()) {
            users[user].name = string(name);
            message = "*** User from " + users[user].ip + " is named '" +  users[user].name + "'. ***\n";
            broadcast(message);
        }
        else {
            message = "*** User '" + string(name) + "' already exists. ***\n";
            write(user, message.c_str(), message.length());
        }
    }
    else if(string(exec) == "tell") {
        char *id = strtok(NULL, " ");
        char *msg = strtok(NULL, "");
        if(sendbox.find(atoi(id)) != sendbox.end()) {
            message = "*** " + users[user].name + " told you ***: " + string(msg) + "\n";
            write(sendbox[atoi(id)].sock, message.c_str(), message.length());
        }
        else {
            message = "*** Error: user #" + string(id) + " does not exist yet. ***\n";
            write(user, message.c_str(), message.length());
        }
    }
    else 
        return false;

    free(cstr);
    return true;
}

void parser(string cmd) {
    //check if blank
    if(cmd.back() == 13) cmd.pop_back();
    if(cmd.empty() || cmd.find_first_not_of(' ') == std::string::npos)
        return;
    
    int pos;
    string cut;
    while((pos = cmd.find("|")) != string::npos) {
        cut = cmd.substr(0, pos);
        cmd_.push_back(cut);
        cmd.erase(0, pos + 1);
    }
    
    //deal with the last segment
    long num;
    pos = cmd.find("!");
    if((pos != string::npos) && (isdigit(cmd[pos+1]))) {
        cut = cmd.substr(0, pos);
        cmd_.push_back(cut);
        cmd.erase(0, pos + 1);

        if(isdigit(cmd.c_str()[0])) {
            oncall.prior = atoi(cmd.c_str());
            oncall.cmd = cmd_;
            oncall.ptype = 2;
        }
    }
    else if(isdigit(cmd.c_str()[0])) {
        oncall.prior = atoi(cmd.c_str());
        oncall.cmd = cmd_;
        oncall.ptype = 1;
    }
    else {
        if(isbuilt_in(cmd))
           return;
        cmd_.push_back(cmd);
        oncall.cmd = cmd_;
        oncall.ptype = 0;
    }
}

string user_pipeto(string c, short *t, int *i) {
    string msg;
    int id, pos = c.find(">");
    if((pos != string::npos) && isdigit(c[pos+1])) {
        id = atoi(c.substr(pos+1).c_str());
        if(sendbox.find(id) == sendbox.end()) {
            *t = 4;
            msg = "*** Error: user #" + to_string(id) + " does not exist yet. ***\n";
            write(user, msg.c_str(), msg.length());
            c = c.substr(0, pos);
        }
        else if(sendbox[users[user].id].sendto.find(id) != sendbox[users[user].id].sendto.end()) {
            *t = 4;
            msg = "*** Error: the pipe #" + to_string(users[user].id) + "->#" + to_string(id) + " already exists. ***\n";
            write(user, msg.c_str(), msg.length());
            c = c.substr(0, pos);
        }
        else {
            *t = 3;
            *i = id;
            msg = "*** " + users[user].name + " (#" + to_string(users[user].id) + ") just piped '" + command + "' to "
                         + users[sendbox[id].sock].name + " (#" + to_string(id) + ") ***\n";
            broadcast(msg);
            c = c.substr(0, pos);;
        }
    }
    
    return c;
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
    for(auto it = users[user].npipe.begin(); it != users[user].npipe.end(); it++) {
        close(it->fd[0]);
        close(it->fd[1]);
    }
}

void end_pipe(string c, short t, int prior) {
    pid_t child;
    int out_r, std_w[2], id;
    char *args[100];
    bool newpipe;
    c = user_pipeto(c, &t, &id);
    string file = cutcmd(c, args);
    out_r = outpipe.empty() ? -1 : outpipe.front();
    
    if(t == 1 || t == 2) {
        newpipe = true;
        for(auto it = users[user].npipe.begin(); it != users[user].npipe.end(); it++) {
            if(it->prior == prior) {
                std_w[0] = it->fd[0];
                std_w[1] = it->fd[1];
                npipe = *it;
                users[user].npipe.erase(it);
                newpipe = false;
                break;
            }
        }
        if(newpipe) { 
            pipe(std_w);
            npipe.fd[0] = std_w[0];
            npipe.fd[1] = std_w[1];
            npipe.prior = prior;
        }
    }
    else if(t == 3) {
        pipe(std_w);
        sendbox[users[user].id].sendto[id] = std_w[0];
    }
    
    while((child = fork()) == -1) {
        waitpid(-1, NULL, 0);
    }
    if(child == 0) {
        //stdin
        close_pipe();
        int devnull = open("/dev/null", O_RDWR);
        if(out_r == 0) 
            dup2(devnull, 0);
        else if(out_r > 0)
            dup2(out_r, 0);

        //stdout
        if(!file.empty()) {
            mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            int fd = open(file.c_str(), O_WRONLY | O_CREAT, mode);
            if(fd != -1) dup2(fd, 1);
            else         perror("Error open");
        }
        else if(t == 1 || t == 2 || t == 3) {
            close(std_w[0]);
            dup2(std_w[1], 1);
        }
        else if(t == 4) {
            dup2(devnull, 1);
            dup2(devnull, 2);
        }
        else 
            dup2(user, 1);
        
        //stderr
        if(t == 2) dup2(std_w[1], 2);
        else       dup2(user, 2);

        signal(SIGPIPE, SIG_IGN);
        execvp(*args, args);
        if(errno == ENOENT)
            fprintf(stderr, "Unknown command: [%s].\n", *args);
        else
            perror("execvp");
        ::exit(1);
    }
    
    wait_.push_back(child);
    if(t == 1 || t == 2)
        users[user].npipe.push_back(npipe);
    else if(t == 3)
        close(std_w[1]);
    else {
        for(auto it = wait_.begin(); it != wait_.end(); it++)
            waitpid(*it, NULL, 0); 
    }
    if(out_r > 0) {
        close(outpipe.front());
        outpipe.pop_front();
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
        //stdin
        close_pipe();
        if(out_r == 0) {
            int devnull = open("/dev/null", O_RDONLY);
            dup2(devnull, 0);
        }
        else if(out_r > 0) {
            dup2(out_r, 0);
        }

        //stdour, stderr
        dup2(user, 2);
        close(out_w[0]);
        dup2(out_w[1], 1);

        signal(SIGPIPE, SIG_IGN);
        execvp(*args, args);
        if(errno == ENOENT)
            fprintf(stderr, "Unknown command: [%s].\n", *args);
        ::exit(1);
    }
    else {
        close(out_w[1]);
        outpipe.push_back(out_w[0]);
        wait_.push_back(child);
    }

    if(out_r > 0) {
        close(outpipe.front());
        outpipe.pop_front();
    }
}

void user_pipein_cut(string c, int pos) {
    oncall.cmd.pop_front();
    c.erase(pos, 1);
    while(isdigit(c[pos]))
        c.erase(pos, 1);
    oncall.cmd.push_front(c);
}

void user_pipein(string c) {
    string msg, sub;
    int id, pos = c.find("<");
    if((pos != string::npos) && isdigit(c[pos+1])) {
        id = atoi(c.substr(pos+1).c_str());
        if(sendbox.find(id) == sendbox.end()) {
            msg = "*** Error: user #" + to_string(id) + " does not exist yet. ***\n";
            write(user, msg.c_str(), msg.length());
            outpipe.push_back(0);
            user_pipein_cut(c, pos);
        }
        else if(sendbox[id].sendto.find(users[user].id) == sendbox[id].sendto.end()) {
            msg = "*** Error: the pipe #" + to_string(id) + "->#" + to_string(users[user].id) + " does not exist yet. ***\n";
            write(user, msg.c_str(), msg.length());
            outpipe.push_back(0);
            user_pipein_cut(c, pos);
        }
        else {
            msg = "*** " + users[user].name + " (#" + to_string(users[user].id) + ") just received from " 
                         + users[sendbox[id].sock].name + " (#" + to_string(id) + ") by '" + command + "' ***\n";
            broadcast(msg);
            outpipe.push_back(sendbox[id].sendto[users[user].id]);
            sendbox[id].sendto.erase(sendbox[id].sendto.find(users[user].id));
            user_pipein_cut(c, pos);
        }
    }
}
void number_pipein(string c) {
    for(auto it = users[user].npipe.begin(); it != users[user].npipe.end(); it++) {
        it->prior--;
        if(it->prior == 0) {
            close(it->fd[1]);
            outpipe.push_back(it->fd[0]);
            users[user].npipe.erase(it);
            it--;
        }
    }    
}

void start_pipe(int) {
    string cmd;
    if(oncall.cmd.empty())
        return;
    number_pipein(oncall.cmd.front());
    user_pipein(oncall.cmd.front());

    // save last cmd
    cmd = oncall.cmd.back();
    oncall.cmd.pop_back();
    
    for(auto it = oncall.cmd.begin(); it != oncall.cmd.end(); it++)
        run_pipe(*it);

    //end the pipe
    end_pipe(cmd, oncall.ptype, oncall.prior);
}

void shell(int fd) {
    dup2(0, 1023);
    dup2(fd, 0);

    init_shell(fd);
    getline(cin, command);
    if(command.empty()) {
        remove_user(fd);
        dup2(1023, 0);
        return;
    }
    parser(command);
    start_pipe(fd);

    write(fd, "% ", 2);
    dup2(1023, 0);
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
    struct sockaddr_in cinfo;
	socklen_t infolen = sizeof(cinfo);
    struct pollfd mpoll, cpoll[30];
    signal(SIGCHLD, recycle);
    
    if(argc == 2) 
        port = atoi(argv[1]);
    else {
        printf("Usage: ./np_simple [port]\n");
        ::exit(1);
    }
    
    sock = init_server(port);
    while(true) {
        bzero(cpoll, sizeof(cpoll));
        bzero(&mpoll, sizeof(mpoll));
        
        mpoll.fd = sock;
        mpoll.events = POLLIN;
        if((poll(&mpoll, 1, 0) > 0) && users.size() <= 30) {
            client = accept(sock, (struct sockaddr*)&cinfo, &infolen);
            add_user(client, cinfo);
        }

        int n = 0;
        for(iter = users.begin(); iter != users.end(); iter++) {
            cpoll[n].fd = iter->first;
            cpoll[n].events = POLLIN;
            n++;
        }
        
        if(poll(cpoll, users.size(), 0) > 0)
            for(int i = 0; i < users.size(); i++)
                if(cpoll[i].revents & POLLIN) 
                    shell(cpoll[i].fd);
    }

    return 0;
}