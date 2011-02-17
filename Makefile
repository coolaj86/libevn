all:
	mkdir -p build
	cd build; cmake ../source -DCMAKE_BUILD_TYPE=; make

debug:
	mkdir -p build
	cd build; cmake ../source -DCMAKE_BUILD_TYPE=Debug; make

clean:
	make clean -C build

install: all
	cd build; make install
