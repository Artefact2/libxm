build:
	@mkdir -p build
	@cd build && cmake ..

dist-clean:
	@rm -Rf build

.PHONY: build dist-clean
