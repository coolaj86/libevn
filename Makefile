all:
	mkdir -p build
	cd build; cmake ../source; make

install: all
	cd build; make install
