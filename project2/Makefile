CXX	= g++
PROCS = np_simple np_single_proc

all: $(PROCS)

np_simple: np_simple.cpp
	$(CXX) -o $@ $^

np_single_proc: np_single_proc.cpp
	$(CXX) -o $@ $^

clean:
	rm np_simple np_single_proc
