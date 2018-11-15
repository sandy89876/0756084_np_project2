all:np_single_proc
	g++ -std=c++11 np_single_proc.cpp -o np_single_proc
	g++ np_simple.cpp -o np_simple
	g++ -std=c++11 multi_proc.cpp -o np_multi_proc