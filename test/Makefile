include environment.mk


# List of test case folders.
TEST_CASES = hello jacobi lulesh

.PHONY: $(TEST_CASES)

all: build test

vecaddcuda: vecadd.cu
	nvcc -g $< -o $@ -lcuda

build:
	$(foreach dir, $(TEST_CASES), $(MAKE) -C $(dir);)

test:
	$(foreach dir, $(TEST_CASES), $(MAKE) -C $(dir) test;)

clean:
	$(foreach dir, $(TEST_CASES), $(MAKE) -C $(dir) clean;)
