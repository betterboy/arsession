all: ar

ar: test.cpp src/ar_session.c src/mbuf.c
	g++ -std=c++11 -g -I./src -o $@ $^ -lpthread

clean:
	rm ar