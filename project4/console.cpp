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

io_service io;

struct Param {
    string host, port, file;
} param;

vector<struct Param> params;
string sh, sp;

void parser() {
    string query;
    query = string(getenv("QUERY_STRING"));
    for(int i = 0; i < 5; i++) {
        query = query.substr(query.find("=") + 1);
        param.host = query.substr(0, query.find("&"));
        query = query.substr(query.find("=") + 1);
        param.port = query.substr(0, query.find("&"));
        query = query.substr(query.find("=") + 1);
        param.file = query.substr(0, query.find("&"));
        if(param.host.size() == 0) continue;
        params.push_back(param);
    }

    query = query.substr(query.find("=") + 1);
    sh = query.substr(0, query.find("&"));
    query = query.substr(query.find("=") + 1);
    sp = query.substr(0, query.find("&"));
}

void print_thead() {
    for(auto it = params.begin(); it != params.end(); it++)
        cout << "<th scope=\"col\">" << it->host << ":" << it->port << "</th>\n"; 
}

void print_tbody() {
    for(size_t i = 0; i < params.size(); i++)
        cout << "<td><pre id=\"s" << i << "\" class=\"mb-0\"></pre></td>\n";
}

void print_html() {
    cout << R"(Content-type: text/html

    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8" />
        <title>NP Project 3 Console</title>
        <link
        rel="stylesheet"
        href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
        integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
        crossorigin="anonymous"
        />
        <link
        href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
        rel="stylesheet"
        />
        <link
        rel="icon"
        type="image/png"
        href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
        />
        <style>
        * {
            font-family: 'Source Code Pro', monospace;
            font-size: 1rem !important;
        }
        body {
            background-color: #212529;
        }
        pre {
            color: #cccccc;
        }
        b {
            color: #01b468;
        }
        </style>
    </head>
    <body>
        <table class="table table-dark table-bordered">
        <thead>
            <tr>)" << endl;
            print_thead();
            cout << R"(
            </tr>
        </thead>
        <tbody>
            <tr>)" << endl;
            print_tbody();
            cout << R"(
            </tr>
        </tbody>
        </table>
    </body>
    </html>
    )" << endl;
}

void escape_html(string& s) {
    boost::algorithm::replace_all(s, "\n", "&NewLine;");
    boost::algorithm::replace_all(s, "\r", "");
    boost::algorithm::replace_all(s, "\'", "&apos;");
    boost::algorithm::replace_all(s, "\"", "&quot;");
    boost::algorithm::replace_all(s, "<", "&lt;");
    boost::algorithm::replace_all(s, ">", "&gt;");
}

void print_sess(string s, int id, bool iscmd) {
    escape_html(s);
    if(iscmd) {
        s = "<b>" + s + "</b>";
    }
    cout << "<script>document.getElementById('s" << id << "').innerHTML += '" << s << "';</script>" << flush;
}

class Session
: public std::enable_shared_from_this<Session>
{
public:
    Session(string ch, string cp, string file, int i)
        : resolv(io), socket_(io), domain(ch), port(cp), fin("./test_case/" + file), id(i) {}

    void start(tcp::resolver::query query) {
        if(!fin) {
            cerr << "fin error\n";
            return;
        }
        auto self = shared_from_this();
        resolv.async_resolve(query,
        [this, self](const boost::system::error_code &ec, tcp::resolver::iterator it)
        {
            if(!ec) {
                socket_.async_connect(it->endpoint(),
                [this, self](boost::system::error_code ec){
                    if(!ec) {
                        socks4_client();
                    }
                });
            }
        });
        
    }
private:
    void socks4_client() {
        unsigned char socks4_request[1024];
        bzero(socks4_request, sizeof(socks4_request));
        socks4_request[0] = 4;
        socks4_request[1] = 1;
        socks4_request[2] = stoi(port) >> 8;
        socks4_request[3] = stoi(port) & 0xff;
        socks4_request[7] = 1;
        memcpy(&socks4_request[9], domain.c_str(), domain.size());
        size_t len = domain.size() + 10;

        auto self(shared_from_this());
        socket_.async_send(buffer(socks4_request, len),
        [this, self](boost::system::error_code ec, size_t length)
            {
                if(!ec) {
                    socks4_reply();               
                }
            });
    }

    void socks4_reply() {
        auto self(shared_from_this());
        socket_.async_read_some(buffer(data_, max_length),
        [this, self](boost::system::error_code ec , size_t length) 
            {
                if(!ec) {
                    if(data_[1] != 90) {
                        print_sess("Socks4 request rejected or failed\n", id, false);
                        exit(1);
                    }

                    sess_read();
                }
            });
    }

    void sess_read() {
        auto self(shared_from_this());
        bzero(data_, sizeof(data_));
        socket_.async_read_some(buffer(data_, max_length),
        [this, self](boost::system::error_code ec , size_t length) 
            {
                if(!ec) {
                    string sess_output = string(data_);
                    print_sess(sess_output, id, false);
                    if (sess_output.find("% ") != string::npos)
                        sess_write();
                    else
                        sess_read();
                }
            });
    }
    
    void sess_write() {
        if(!getline(fin, cmd)) return;
        cmd += "\n";
        print_sess(cmd, id, true);
        auto self(shared_from_this());
        socket_.async_send(buffer(cmd, cmd.size()),
            [this, self](boost::system::error_code ec, size_t length)
            {
                if(!ec) {
                    sess_read();
                }
            });
    }

    tcp::resolver resolv;
    tcp::socket socket_;
    string cmd, domain, port;
    ifstream fin;
    int id;
    enum { max_length = 8192 };
    char data_[max_length];
};

int main() {
    parser();
    print_html();

    //start each session
    for(size_t i = 0; i < params.size(); i++) {
        tcp::resolver::query query(sh, sp);
        shared_ptr<Session> s = make_shared<Session>(params[i].host, params[i].port, params[i].file, i);
        s->start(query);
    }

    io.run();

    return 0;
}