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

struct Param {
    string host, port, file;
} param;

vector<struct Param> params;

void parser(string query) {
    for(int i = 0; i < 5; i++) {
        if(query[query.find("=") + 1] == '&') break;
        query = query.substr(query.find("=") + 1);
        param.host = query.substr(0, query.find("&"));
        query = query.substr(query.find("=") + 1);
        param.port = query.substr(0, query.find("&"));
        query = query.substr(query.find("=") + 1);
        param.file = query.substr(0, query.find("&"));
        params.push_back(param);
    }
}

void print_panel(shared_ptr<tcp::socket> sock) {
    string panel = "Content-type: text/html\r\n\r\n";
    panel += R"(
<!DOCTYPE html>
<html lang="en">
  <head>
    <title>NP Project 3 Panel</title>
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
      href="https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png"
    />
    <style>
      * {
        font-family: 'Source Code Pro', monospace;
      }
    </style>
  </head>
  <body class="bg-secondary pt-5">
    <form action="console.cgi" method="GET">
      <table class="table mx-auto bg-light" style="width: inherit">
        <thead class="thead-dark">
          <tr>
            <th scope="col">#</th>
            <th scope="col">Host</th>
            <th scope="col">Port</th>
            <th scope="col">Input File</th>
          </tr>
        </thead>
        <tbody>)";
for(int i = 0; i < 5; i++) {
    panel += R"(
          <tr>
            <th scope="row" class="align-middle">Session )";
    panel += to_string(i + 1);
    panel += R"(</th>
            <td>
              <div class="input-group">
                <select name="h)";
    panel += to_string(i);
    panel += R"(" class="custom-select">
                  <option></option>)";
    for(int j = 0; j < 12; j++) {
        panel += "<option value=\"nplinux" + to_string(j+1) + ".cs.nctu.edu.tw\">nplinux" + to_string(j + 1) + "</option>";
    }
    panel += R"(
                </select>
                <div class="input-group-append">
                  <span class="input-group-text">.cs.nctu.edu.tw</span>
                </div>
              </div>
            </td>
            <td>
              <input name="p)";
    panel += to_string(i);
    panel += R"(" type="text" class="form-control" size="5" />
            </td>
            <td>
              <select name="f)";
    panel += to_string(i);
    panel += R"(" class="custom-select">
                <option></option>
                )";
    for(int j = 0; j < 5; j++) {
        panel += "<option value=\"t" + to_string(j+1) + ".txt\">t" + to_string(j+1) + ".txt</option>";
    }
    panel += R"(
              </select>
            </td>
          </tr>
    )";
}

panel += R"(
          <tr>
            <td colspan="3"></td>
            <td>
              <button type="submit" class="btn btn-info btn-block">Run</button>
            </td>
          </tr>
        </tbody>
      </table>
    </form>
  </body>
</html>
)";
    sock->async_send(buffer(panel, panel.size()),
        [](boost::system::error_code ec, std::size_t /*length*/) {
          if(ec) cerr << "panel error\n";
        });
}

void print_thead(string& s) {
    for(auto it = params.begin(); it != params.end(); it++)
        s += "<th scope=\"col\">" + it->host + ":" + it->port + "</th>\n"; 
}

void print_tbody(string& s) {
    for(size_t i = 0; i < params.size(); i++)
        s += "<td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>\n";
}

void print_console(shared_ptr<tcp::socket> sock) {
    string console = R"(Content-type: text/html

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
            <tr>
    )";
    print_thead(console);
    console += R"(
            </tr>
        </thead>
        <tbody>
            <tr>
    )";
    print_tbody(console);
    console += R"(
            </tr>
        </tbody>
        </table>
    </body>
    </html>
    )";

    sock->async_send(buffer(console, console.size()),
        [](boost::system::error_code ec, std::size_t /*length*/) {
          if(ec) cerr << "console error\n";
        });    
}

void escape_html(string& s) {
    boost::algorithm::replace_all(s, "\n", "&NewLine;");
    boost::algorithm::replace_all(s, "\r", "");
    boost::algorithm::replace_all(s, "\'", "&apos;");
    boost::algorithm::replace_all(s, "\"", "&quot;");
    boost::algorithm::replace_all(s, "<", "&lt;");
    boost::algorithm::replace_all(s, ">", "&gt;");
}

void print_sess(shared_ptr<tcp::socket> sock, string sess, int id, bool iscmd) {
    escape_html(sess);
    if(iscmd) {
        sess = "<b>" + sess + "</b>";
    }
    sess = "<script>document.getElementById('s" + to_string(id) + "').innerHTML += '" + sess + "';</script>";
    
    sock->async_send(buffer(sess, sess.size()),
        [](boost::system::error_code ec, std::size_t /*length*/) {});  
}

class Session
: public std::enable_shared_from_this<Session>
{
public:
    Session(shared_ptr<tcp::socket> socket, string file, int id)
        : resolv(io) , sess_socket(io), html_socket(socket), fin("./test_case/" + file), id_(id){}

    void start(tcp::resolver::query query) {
        if(!fin) {
            cerr << "fin error\n";
            return;
        }
        auto self(shared_from_this());
        resolv.async_resolve(query, 
            [this, self](const boost::system::error_code &ec, tcp::resolver::iterator it)
            {
                if(!ec) {
                    auto self = shared_from_this();
                    sess_socket.async_connect(it->endpoint(),
                    [this, self](boost::system::error_code ec){
                        sess_read();
                    });
                }
            });
    }
private:
    void sess_read() {
        auto self(shared_from_this());
        memset(data_, '\0', max_length);
        sess_socket.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec , size_t length) 
            {
                if (!ec) {
                    string sess_output = string(data_);
                    print_sess(html_socket ,sess_output, id_, false);
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
        print_sess(html_socket, cmd, id_, true);
        auto self(shared_from_this());
        sess_socket.async_send(buffer(cmd, cmd.size()),
            [this, self](boost::system::error_code ec, size_t length)
            {
                if (!ec) {
                    sess_read();
                }
            });
    }

    shared_ptr<tcp::socket> html_socket;
    tcp::socket sess_socket;
    tcp::resolver resolv;
    ifstream fin;
    string cmd;
    enum { max_length = 8192 };
    char data_[max_length];
    int id_;
};


class worker
  : public std::enable_shared_from_this<worker>
{
public:
    worker(shared_ptr<tcp::socket> socket)
        : socket_(std::move(socket)) {}

    void start() 
    {
        cgi_worker();
    }

private:
    void cgi_worker()
    {
        auto self(shared_from_this());
        socket_->async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {   
                    int pos;
                    string worker, query, packet = string(data_);
                    if(packet.find(".cgi") != string::npos) {
                        pos = packet.find("/") + 1;
                        worker = packet.substr(pos, packet.find(".cgi") - pos + 4);
                        if(worker == "panel.cgi") {
                            do_write("HTTP/1.1 200 OK\r\n");
                            print_panel(socket_);
                        }
                        else if (worker == "console.cgi") {
                            do_write("HTTP/1.1 200 OK\r\n");
                            query = packet.substr(packet.find("?") + 1);
                            query = query.substr(0, query.find(" ")).c_str();
                            parser(query);
                            do_sess();
                        }
                        else {
                            do_write("HTTP/1.1 404\r\n");
                        }
                    }
                    else {
                        do_write("HTTP/1.1 404\r\n");
                    }
                }
            });
    }

    void do_sess() {
        print_console(socket_);
        for(size_t i = 0; i < params.size(); i++) {
            tcp::resolver::query query(params[i].host, params[i].port);
            shared_ptr<Session> s = make_shared<Session>(socket_, params[i].file, i);
            s->start(query);
        }
        params.clear();
    }
  
    void do_write(string s)
    {
        auto self(shared_from_this());
        socket_->async_send(buffer(s, s.size()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if(ec) cerr << "do_write error\n";
            });
    }

    shared_ptr<tcp::socket> socket_;
    enum { max_length = 1024 };
    char data_[max_length];
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
                    std::make_shared<worker>(std::move(socket_))->start();
                }
                do_accept();
            });
    }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  try {
    if (argc != 2) {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }

    server s(io, atoi(argv[1]));
    io.run();
  }
  catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
