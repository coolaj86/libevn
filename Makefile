all:
	mkdir -p build
	cd build; cmake ../source; make

clean:
	make clean -C build

install: all
	cd build; make install
