build: build-debug build-prod

build-debug: CMakeLists.txt
	@test -d $@ || mkdir $@
	@cd $@ && CC=gcc cmake ..

build-prod: CMakeLists.txt
	@test -d $@ || mkdir $@
	@cd $@ && CC=gcc cmake -D XM_DEBUG=OFF -D XM_DEFENSIVE=OFF -D XM_STRINGS=OFF -D XM_OPTIMISE_FOR_SIZE=ON -D XM_LINEAR_INTERPOLATION=OFF -D XM_RAMPING=OFF ..

test:
	make -C ${BUILD_DIR} libxmize libxmtoau
	parallel -0 --lb -N1 '${BUILD_DIR}/src/libxmize {} >(${BUILD_DIR}/examples/libxmtoau {} >/dev/null || echo libxmtoau: {}) >/dev/null || echo libxmize: {}' :::: <(find ${XMDIR} -iname "*.xm" -print0)

test-debug: build-debug
	make test BUILD_DIR=./build-debug XMDIR=${XMDIR}

test-prod: build-prod
	make test BUILD_DIR=./build-prod XMDIR=${XMDIR}

dist-clean:
	@rm -Rf build-debug build-prod

.PHONY: build dist-clean test-debug test-prod test
