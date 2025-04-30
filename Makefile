build: build-debug build-prod

build-debug: CMakeLists.txt
	@test -d $@ || mkdir $@
	@cd $@ && CC=gcc cmake ..

build-prod: CMakeLists.txt
	@test -d $@ || mkdir $@
	@cd $@ && CC=gcc cmake -D XM_DEBUG=OFF -D XM_DEFENSIVE=OFF -D XM_STRINGS=OFF -D XM_OPTIMISE_FOR_SIZE=ON -D XM_LINEAR_INTERPOLATION=OFF -D XM_RAMPING=OFF ..

test:
	parallel -0 --ungroup -N1 '${XMTOAU} {} >/dev/null || echo {}' :::: <(find ${XMDIR} -iname "*.xm" -print0)

test-debug: build-debug
	make -C $< xmtoau
	make test XMTOAU=./build-debug/examples/xmtoau XMDIR=${XMDIR}

test-prod: build-prod
	make -C $< xmtoau
	make test XMTOAU=./build-prod/examples/xmtoau XMDIR=${XMDIR}

dist-clean:
	@rm -Rf build-debug build-prod

.PHONY: build dist-clean test-debug test-prod test
