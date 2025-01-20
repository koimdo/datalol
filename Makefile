NPROCS?=16

CMAKE = MAKEFLAGS=-j${NPROCS} CC=${CC} CXX=${CXX} cmake
CMAKECACHE = build/CMakeCache.txt
all: ${CMAKECACHE}
	${CMAKE} --build build
	git archive --prefix=datalol/ -o "build/datalol-$(shell git describe --tags --always HEAD).tar.gz" HEAD

check: ${CMAKECACHE}
	${CMAKE} --build build --target check

clean:
	${CMAKE} --build build --target clean 2>/dev/null || true
	rm -rf build

${CMAKECACHE}:
	mkdir -p build
	cd build && ${CMAKE} -DCMAKE_BUILD_TYPE=Debug ..

.PHONY: all check clean
