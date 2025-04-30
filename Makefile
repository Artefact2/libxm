build: build-debug build-prod

build-debug: CMakeLists.txt
	@test -d $@ || mkdir $@
	@cd $@ && CC=gcc cmake ..

build-prod: CMakeLists.txt
	@test -d $@ || mkdir $@
	@cd $@ && CC=gcc cmake -D XM_DEBUG=OFF -D XM_DEFENSIVE=OFF -D XM_STRINGS=OFF -D XM_OPTIMISE_FOR_SIZE=ON -D XM_LINEAR_INTERPOLATION=OFF -D XM_RAMPING=OFF ..

dist-clean:
	@rm -Rf build-debug build-prod

.PHONY: build dist-clean
