# Clang/LLVM providing the OMPT-capable OpenMP runtime.  Single source of truth
# for the whole test tree -- override per machine on the command line:
#     make CLANG=clang-21
CLANG ?= clang-22

# Derive the LLVM prefix from the compiler binary instead of hardcoding
# /usr/lib/llvm-NN.  The hardcoded form has broken on every toolchain bump, and
# it breaks silently: the tests LD_PRELOAD $(OMP_LIB_PATH)/libomp.so, so a stale
# prefix means preloading nothing (or the wrong runtime) rather than an error.
#
# NOT `$(CLANG) -print-file-name=libomp.so`: that resolves to the DISTRO libomp
# (/usr/lib/x86_64-linux-gnu, package libomp5) rather than the one shipped with
# this clang.  Both export ompt_start_tool, so the mismatch does not fail loudly
# -- it just silently runs OMPT against a different libomp than the compiler.
CLANG_PREFIX := $(shell dirname $(shell dirname $(shell readlink -f $(shell which $(CLANG)))))

# libomp.so path
export OMP_LIB_PATH ?= $(CLANG_PREFIX)/lib
# omp.h and omp-tools.h path
export OMP_BASE_PATH ?= $(CLANG_PREFIX)
export CLANG
# path of libpinsight.so, default ../build
export PINSIGHT_LIB_PATH = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../build)

# Fail early and legibly rather than preloading a nonexistent libomp.
check-omp:
	@test -n "$(CLANG_PREFIX)" && test -e "$(OMP_LIB_PATH)/libomp.so" || { \
	    echo "environment.mk: no libomp.so for CLANG=$(CLANG)"; \
	    echo "  resolved prefix: '$(CLANG_PREFIX)'"; \
	    echo "  expected:        $(OMP_LIB_PATH)/libomp.so"; \
	    echo "  install it (e.g. apt install $(CLANG) libomp-\$${CLANG##*-}-dev)"; \
	    echo "  or override:     make CLANG=<your-clang>"; exit 1; }
.PHONY: check-omp
