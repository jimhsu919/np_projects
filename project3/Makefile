CXX=g++
CXXFLAGS=-std=c++14 -Wall -pedantic -pthread -lboost_system
CXX_INCLUDE_DIRS=/usr/local/include
CXX_INCLUDE_PARAMS=$(addprefix -I , $(CXX_INCLUDE_DIRS))
CXX_LIB_DIRS=/usr/local/lib
CXX_LIB_PARAMS=$(addprefix -L , $(CXX_LIB_DIRS))
PROCS = http_server console.cgi

part1: $(PROCS)

http_server: http_server.cpp
	$(CXX) -o $@ $^ $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS)

console.cgi: console.cpp
	$(CXX) -o $@ $^ $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS)

part2: cgi_server.cpp
	$(CXX) cgi_server.cpp -o cgi_server -lws2_32 -lwsock32 -std=c++14

clean:
	rm -f http_server console.cgi