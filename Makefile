build-debug: build
	@cd build && cmake -D XM_DEBUG=ON -D XM_DEFENSIVE=ON ..

build-demo: build
	@cd build && cmake -D XM_DEBUG=OFF -D XM_DEFENSIVE=OFF -D XM_DEMO_MODE=ON ..

build:
	@mkdir -p build

dist-clean:
	@rm -Rf build

.PHONY: build dist-clean
