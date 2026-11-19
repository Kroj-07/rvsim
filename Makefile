rvsim: rvsim.cpp
	g++ -std=c++17 -O2 -Wall -Wextra -o rvsim rvsim.cpp
tests:
	./build_tests.sh
clean:
	rm -f rvsim
