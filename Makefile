build: build-debug build-prod

build-debug:
	mkdir $@
	@cd $@ && CC=gcc cmake -D XM_DEBUG=ON -D XM_DEFENSIVE=ON -D XM_DEMO_MODE=OFF ..

build-prod:
	mkdir $@
	@cd $@ && CC=gcc cmake -D XM_DEBUG=OFF -D XM_DEFENSIVE=OFF -D XM_DEMO_MODE=ON ..

dist-clean:
	@rm -Rf build-debug build-prod

.PHONY: build dist-clean
