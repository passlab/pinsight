# The batch run script for Lulesh on carina
# Run this script in the parent folder of this script and all the output and traces will
# be stored in traces folder
# To use this script, you need to create two folders for PInsight named lib-wo_energy
# and lib-w_energy. The folders should contains libpinsight.so files that are build
# with energy collection disabled and enabled. 
# This script also assume that you build LULESH with both g++ and clang++ and have 
# the executable properly named

for num_threads in 2 4 8 16 32 48 56
do
	export OMP_NUM_THREADS=${num_threads}
	LULESH/lulesh2.0-clang++-7.0.0 >> traces/lulesh2.0-clang++-7.0.0-notracing.txt 2>&1
	LULESH/lulesh2.0-g++-5.4 >> traces/lulesh2.0-g++-5.4-notracing.txt 2>&1
	scripts/trace.sh traces/lulesh2.0-g++-5.4-${num_threads}threads-wo_energy /home/yanyh/tools/pinsight/lib-wo_energy/libpinsight.so /home/yanyh/tools/llvm-openmp-install ${num_threads} LULESH/lulesh2.0-g++-5.4 >>traces/lulesh2.0-g++-5.4-wo_energy.txt 2>&1
	scripts/trace.sh traces/lulesh2.0-g++-5.4-${num_threads}threads-w_energy /home/yanyh/tools/pinsight/lib-w_energy/libpinsight.so /home/yanyh/tools/llvm-openmp-install ${num_threads} LULESH/lulesh2.0-g++-5.4 >>traces/lulesh2.0-g++-5.4-w_energy.txt 2>&1
	scripts/trace.sh traces/lulesh2.0-clang++-7.0.0-${num_threads}threads-wo_energy /home/yanyh/tools/pinsight/lib-wo_energy/libpinsight.so /home/yanyh/tools/llvm-openmp-install ${num_threads} LULESH/lulesh2.0-clang++-7.0.0 >>traces/lulesh2.0-clang++-7.0.0-wo_energy.txt 2>&1
	scripts/trace.sh traces/lulesh2.0-clang++-7.0.0-${num_threads}threads-w_energy /home/yanyh/tools/pinsight/lib-w_energy/libpinsight.so /home/yanyh/tools/llvm-openmp-install ${num_threads} LULESH/lulesh2.0-clang++-7.0.0 >>traces/lulesh2.0-clang++-7.0.0-w_energy.txt 2>&1
done
