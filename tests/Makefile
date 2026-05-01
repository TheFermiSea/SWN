CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -I../inc

.PHONY: all clean

all: test_chaos
	./test_chaos

test_chaos: test_chaos.cpp ../inc/MathHelpers.h ../inc/LorenzAttractor.h ../inc/RosslerAttractor.h ../inc/ChaosModulator.h
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f test_chaos
