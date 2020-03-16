default: latm_split.exe

latm_split.exe: latm_split.o
	g++ -static -static-libgcc -static-libstdc++ -o $@ $<
	strip $@

latm_split.o: latm_split.cpp
	g++ -O2 --std=c++11 -c latm_split.cpp
