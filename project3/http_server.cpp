#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;
using namespace boost::asio;
using namespace std;

class worker
  : public std::enable_shared_from_this<worker>
{
public:
  worker(tcp::socket socket)
    : socket_(std::move(socket))
  {
    clearenv();
  }

  void start() 
  {
    cgi_worker();
  }

private:
  void cgi_worker()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {   
              int pos;
              string worker, packet = string(data_);
              if(packet.find(".cgi") != string::npos) {
                  pos = packet.find("/") + 1;
                  worker = packet.substr(pos, packet.find(".cgi") - pos + 4);
                      string fst = packet.substr(0, packet.find("\r\n"));
                      packet = packet.substr(packet.find("\r\n") + 2);
                      string sec = packet.substr(0, packet.find("\r\n"));
                      setenv_(fst, sec);
                      do_worker(worker);
              }
              else {
                  do_write("HTTP/1.1 404\r\n");
              }
          }
        });
  }

  void do_worker(string worker)
  {
      pid_t pid;
      if((pid = fork()) < 0) {
          do_write("HTTP/1.1 404\r\n");
          perror("fork");
      }
      else if(pid == 0) {
          do_write("HTTP/1.1 200 OK\r\n");
          dup2(socket_.native_handle(), STDIN_FILENO);
          dup2(socket_.native_handle(), STDOUT_FILENO);
          execl(worker.c_str(), worker.c_str(), NULL);
          perror("exec");
      }
      else {
          socket_.close();
      }
  }

  void setenv_(string query, string host)
  {
      setenv("REQUEST_METHOD", query.substr(0, query.find(" ")).c_str(), 1);
      query = query.substr(query.find(" ") + 1);
      setenv("REQUEST_URI", query.substr(0, query.find("?")).c_str(), 1);
      query = query.substr(query.find("?") + 1);
      setenv("QUERY_STRING", query.substr(0, query.find(" ")).c_str(), 1);
      query = query.substr(query.find(" ") + 1);
      setenv("SERVER_PROTOCOL", query.c_str(), 1);
      setenv("HTTP_HOST", host.substr(host.find(" ") + 1).c_str(), 1);
      setenv("SERVER_ADDR", socket_.local_endpoint().address().to_string().c_str(), 1);
      setenv("SERVER_PORT", to_string(socket_.local_endpoint().port()).c_str(), 1);
      setenv("REMOTE_ADDR", socket_.remote_endpoint().address().to_string().c_str(), 1);
      setenv("REMOTE_PORT", to_string(socket_.remote_endpoint().port()).c_str(), 1);
  }

  void do_write(string s)
  {
    auto self(shared_from_this());
    socket_.async_send( buffer(s, s.size()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (ec)
          {
            cerr << "do_write error\n";
          }
        });
  }

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<worker>(std::move(socket))->start();
          }
          
          do_accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }
    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}