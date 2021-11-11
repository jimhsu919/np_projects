#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <memory>
#include <utility>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;
using namespace boost::asio;
using namespace std;

boost::asio::io_service io;
vector<string> server_ip;

class Session
: public std::enable_shared_from_this<Session>
{
public:
    Session(shared_ptr<tcp::socket> sock1, shared_ptr<tcp::socket> sock2)
        : from(sock1), to(sock2){}
    
    void start() {
        sess_read();
    }
private:
    void sess_read() {
        auto self(shared_from_this());
        memset(data_, '\0', max_length);
        from->async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec , size_t length)
            {
                if(!ec) sess_write(length);
                else    sess_end();
            });
    }

    void sess_write(size_t len) {
        auto self(shared_from_this());
        async_write(*to, buffer(data_, len),
            [this, self](boost::system::error_code ec, size_t length)
            {
                if(!ec) sess_read();
                else    sess_end();
            });
    }
    
    void sess_end() {
        from->close();
        to->close();
    }

    shared_ptr<tcp::socket> from, to;
    enum { max_length = 8192 };
    char data_[max_length];
};


class worker
  : public std::enable_shared_from_this<worker>
{
public:
    worker(shared_ptr<tcp::socket> socket)
        : socket_(std::move(socket)) {}

    void start() 
    {
        sock4_worker();
    }

private:
    void sock4_worker()
    {
        auto self(shared_from_this());
        memset(data_, '\0', max_length);
        socket_->async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec) {
                    sock4_handler(data_);
                }
            });
    }

    void sock4_handler(unsigned char* bytes) {
        unsigned char vn, cd, socks4_reply[8];
        char *uid, *domain_name;
        unsigned short dstport;
        string dstip;
        vn = bytes[0];
        cd = bytes[1];
        dstport = bytes[2] << 8 | bytes[3];
        uid = (char*)bytes + 8;

        if(vn != 4) {
            cerr << "Bad sock4 request, unknow vn\n";
            return;
        }
        // need dns query 
        if(bytes[4] == 0 && bytes[5] == 0 && bytes[6] == 0 && bytes[7] != 0) {
            domain_name = uid + strlen(uid) + 1;
            tcp::resolver::query query(string(domain_name), to_string(dstport));
            tcp::resolver resolv(io);
            boost::system::error_code ec;
            resolv_iter = resolv.resolve(query, ec);
            if(ec) {
                cerr << "Domain name resolve error\n";
                return;
            }
            for(auto it = resolv_iter;; it++) {
                if(it->endpoint().address().is_v4()){
                    dstip = it->endpoint().address().to_string();
                    break;
                }
            }
            dstport = resolv_iter->endpoint().port();
        }
        else {
            dstip = to_string(bytes[4]) + "." + to_string(bytes[5]) + "." + to_string(bytes[6]) + "." + to_string(bytes[7]);
        }

        bzero(socks4_reply, sizeof(socks4_reply));
        socks4_reply[1] = firewall(dstip, cd) ? 91 : 90;
        cout << "<S_IP>:    " << socket_->remote_endpoint().address().to_string() << endl
             << "<S_PORT>:  " << socket_->remote_endpoint().port() << endl
             << "<D_IP>:    " << dstip << endl
             << "<D_PORT>:  " << dstport << endl
             << "<Command>: " << ((cd == 1) ? "CONNECT" : "BIND") << endl
             << "<Reply>:   " << ((socks4_reply[1] == 90) ? "Accept" : "Reject") << endl
             << endl;
        if(socks4_reply[1] == 91) {
            do_reply(socks4_reply);
            return;
        }
        
        auto self(shared_from_this());
        if(cd == 1) {
            do_reply(socks4_reply);
            auto server_sock = make_shared<ip::tcp::socket>(io);
            server_sock->connect(tcp::endpoint(tcp::endpoint().address().from_string(dstip), dstport));
            do_sess(server_sock, socket_);
        }
        else if(cd == 2) {
            tcp::acceptor acceptor_(io, tcp::endpoint(tcp::v4(), INADDR_ANY));
            socks4_reply[2] = acceptor_.local_endpoint().port() >> 8;
            socks4_reply[3] = acceptor_.local_endpoint().port() & 0xff;
            do_reply(socks4_reply);

            auto server_sock = make_shared<ip::tcp::socket>(io);
            acceptor_.accept(*server_sock);
            do_reply(socks4_reply);
            do_sess(server_sock, socket_);            
        }
        else {
            cerr << "Bad sock4 request, unknow cd\n";
        }

    }

    void do_sess(shared_ptr<tcp::socket> server, shared_ptr<tcp::socket> client) {
        shared_ptr<Session> uplink = make_shared<Session>(client, server);
        uplink->start();
        shared_ptr<Session> downlink = make_shared<Session>(server, client);
        downlink->start();
    }

    bool firewall(string ip, unsigned char proto) {
        string rule, type, ip_;
        ifstream fin("socks.conf");
        if(!fin) {
            cerr << "socks.conf open error\n";
            return true;
        }
        while(fin >> rule >> type >> ip_) {
            int pos;
            string sub, sub_;
            if((type == "c" && proto == 1) || (type == "b" && proto == 2)) {
                for(pos = 0; pos < 4; pos++) { 
                    sub = ip.substr(0, ip.find("."));
                    sub_ = ip_.substr(0, ip_.find("."));
                    ip = ip.substr(ip.find(".") + 1);
                    ip_ = ip_.substr(ip_.find(".") + 1);
                    if(sub_ == "*" || sub == sub_) continue;
                    else break;
                }
                if(pos == 4) return false;
            }
        }

        return true;
    }
  
    void do_reply(unsigned char* reply)
    {
        auto self(shared_from_this());
        socket_->async_send(buffer(reply, 8),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if(ec) cerr << "do_write error\n";
            });
    }

    shared_ptr<tcp::socket> socket_, server_sock;
    tcp::resolver::iterator resolv_iter;
    enum { max_length = 1024 };
    unsigned char data_[max_length];
};

class server
{
public:
    server(boost::asio::io_service& io, short port)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port))
    {
        do_accept();
    }

private:
    void do_accept()
    {
        auto socket_ = make_shared<ip::tcp::socket>(io);
        acceptor_.async_accept(*socket_,
            [this, socket_](boost::system::error_code ec) {
                if (!ec) {
                    pid_t pid;
                    io.notify_fork(io_service::fork_prepare);
                    if((pid = fork()) == -1) {
                        perror("fork");
                    }
                    else if(pid == 0) {
                        io.notify_fork(io_service::fork_child);
                        std::make_shared<worker>(std::move(socket_))->start();
                    }
                    else {
                        io.notify_fork(io_service::fork_parent);
                        socket_->close();
                    }
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: sock_server <port>\n";
        return 1;
    }

    server s(io, std::atoi(argv[1]));
    io.run();

    return 0;
}