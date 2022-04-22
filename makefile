all: client-phase1 client-phase2 client-phase3 client-phase4 client-phase5

client-phase1: client-phase1.cpp
	g++ -g client-phase1.cpp -lssl -lcrypto -pthread -std=c++17 -o client-phase1 2>/dev/null

client-phase2: client-phase2.cpp
	g++ -g client-phase2.cpp -lssl -lcrypto -pthread -std=c++17 -o client-phase2 2>/dev/null

client-phase3: client-phase3.cpp
	g++ -g client-phase3.cpp -lssl -lcrypto -pthread -std=c++17 -o client-phase3 2>/dev/null

client-phase4: client-phase4.cpp
	g++ -g client-phase4.cpp -lssl -lcrypto -pthread -std=c++17 -o client-phase4 2>/dev/null

client-phase5: client-phase5.cpp
	g++ -g client-phase5.cpp -lssl -lcrypto -pthread -std=c++17 -o client-phase5 2>/dev/null

.PHONY: clean

clean:
	rm client-phase1
	rm client-phase2
	rm client-phase3
	rm client-phase4
	rm client-phase5
	rm -rf output
