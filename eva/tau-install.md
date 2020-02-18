### Install TAU with mpicc/gcc

````
yyan7@fornax:~/tools/tau-2.29$ sudo ./configure -c++=mpicxx -cc=mpicc -fortran=mpif90 -mpi -ompt -iowrapper -bfd=download -dwarf=download -otf=download -unwind=download -prefix=/opt/tools/tau-2.29-install-mpicc
[sudo] password for yyan7: 
Unset ParaProf's cubeclasspath...
Unset Perfdmf cubeclasspath...
Downloading libdwarf...
Downloading libotf2...
Downloading libunwind...
Testing directory /opt/tools/tau-2.29-install-mpicc 
-------------------- TAU configure script ------------------
  The TAU source code has just been configured to use the
  tau root directory /home/yyan7/tools/tau-2.29.
  If you move the Tau distribution, you must run configure
  again and recompile
------------------------------------------------------------
Attempting to auto-configure system, determining architecture...
I think this is a x86_64...
NOTE: Found mpicxx in the path
Default C++ compiler will be  gcc version 7.4.0 (Ubuntu 7.4.0-1ubuntu1~18.04.1)
Setting F90 compiler based on requested: gfortran
Default Fortran compiler will be GNU gfortran
Correction: Using MPI include directory /usr/include/openmpi instead
Using MPI lib directory /usr/lib
NOTE: Using OpenMPI
Default C compiler will be  gcc version 7.4.0 (Ubuntu 7.4.0-1ubuntu1~18.04.1)
Using GNU lib dir as /usr/lib/gcc/x86_64-linux-gnu/7/
Using GNU stdc++ lib dir as /usr/lib/gcc/x86_64-linux-gnu/7/
Using LD AUDITOR.
Found an x86_64 configuration definition
LINUX_TIMERS enabled
Not found in /opt/tools/tau-2.29-install-mpicc/x86_64/libunwind-1.3.1-gcc: libunwind
Looking for /home/yyan7/tools/tau-2.29/external_dependencies/libunwind-1.3.1.tar.gz...
wget: http://www.cs.uoregon.edu/research/paracomp/tau/tauprofile/dist/libunwind-1.3.1.tar.gz ==> libunwind-1.3.1.tar.gz
--2020-02-18 16:10:05--  http://www.cs.uoregon.edu/research/paracomp/tau/tauprofile/dist/libunwind-1.3.1.tar.gz
Resolving www.cs.uoregon.edu (www.cs.uoregon.edu)... 128.223.4.25, 2607:8400:205e:40::80df:419
Connecting to www.cs.uoregon.edu (www.cs.uoregon.edu)|128.223.4.25|:80... connected.
HTTP request sent, awaiting response... 200 OK
Length: 801262 (782K) [application/x-gzip]
Saving to: ‘libunwind-1.3.1.tar.gz’

libunwind-1.3.1.tar.gz                   100%[================================================================================>] 782.48K  1.85MB/s    in 0.4s    

2020-02-18 16:10:06 (1.85 MB/s) - ‘libunwind-1.3.1.tar.gz’ saved [801262/801262]

expanding libunwind-1.3.1.tar.gz...
removing libunwind-1.3.1.tar.gz...
configuring libunwind-1.3.1... (see /home/yyan7/tools/tau-2.29/libunwind-1.3.1/configure.log for log)...
building libunwind-1.3.1... (see /home/yyan7/tools/tau-2.29/libunwind-1.3.1/build.log for log)...
installing libunwind-1.3.1 to /opt/tools/tau-2.29-install-mpicc/x86_64/libunwind-1.3.1-gcc... (see /home/yyan7/tools/tau-2.29/libunwind-1.3.1/install.log for log)...
...Success.
Checking Compiler OMPT support : 
	 Checking for version 5.0 ... no
	 Checking for version TR7 ... no
	 Checking for version TR6 ... no
Compiler does not support OMPT ... Downloading LLVM-OpenMP
Note: Building LLVM-OpenMP with gcc and g++
downloading LLVM-openmp-8.0.tar.gz...
Looking for /home/yyan7/tools/tau-2.29/external_dependencies/LLVM-openmp-8.0.tar.gz...
wget: http://tau.uoregon.edu/LLVM-openmp-8.0.tar.gz ==> LLVM-openmp-8.0.tar.gz
--2020-02-18 16:10:42--  http://tau.uoregon.edu/LLVM-openmp-8.0.tar.gz
Resolving tau.uoregon.edu (tau.uoregon.edu)... 128.223.202.29
Connecting to tau.uoregon.edu (tau.uoregon.edu)|128.223.202.29|:80... connected.
HTTP request sent, awaiting response... 303 See Other
Location: http://www.cs.uoregon.edu/research/tau/LLVM-openmp-8.0.tar.gz [following]
--2020-02-18 16:10:42--  http://www.cs.uoregon.edu/research/tau/LLVM-openmp-8.0.tar.gz
Resolving www.cs.uoregon.edu (www.cs.uoregon.edu)... 128.223.4.25, 2607:8400:205e:40::80df:419
Connecting to www.cs.uoregon.edu (www.cs.uoregon.edu)|128.223.4.25|:80... connected.
HTTP request sent, awaiting response... 200 OK
Length: 1476482 (1.4M) [application/x-gzip]
Saving to: ‘LLVM-openmp-8.0.tar.gz’

LLVM-openmp-8.0.tar.gz                   100%[================================================================================>]   1.41M  2.39MB/s    in 0.6s    

2020-02-18 16:10:43 (2.39 MB/s) - ‘LLVM-openmp-8.0.tar.gz’ saved [1476482/1476482]

expanding LLVM-openmp-8.0.tar.gz...
removing LLVM-openmp-8.0.tar.gz...
building LLVM-openmp-8.0 (see /home/yyan7/tools/tau-2.29/LLVM-openmp-8.0/build-g++/build.log for log)...
cmake -DCMAKE_DISABLE_FIND_PACKAGE_CUDA=true cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_INSTALL_PREFIX=/home/yyan7/tools/tau-2.29/LLVM-openmp-8.0/build-g++ -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC ..
make clean
make libomp-needed-headers
make
make install
cp ./include/omp.h ./include/ompt.h ./include/omp-tools.h /home/yyan7/tools/tau-2.29/include/.
...Success.
Using OMPT 5
***********************************************************************
Checking for thread-safe MPI interface... yes
Checking for MPI 3 const interface... yes
Checking for OpenMPI 3 const interface..... no
Checking for MPI_Type_hindexed const interface... not needed
Checking for MPI-2 interface... yes
Checking for MPI-2 Grequest interface... yes
Checking for MPI-2 MPIO_Request interface... no
Checking for MPI_Datarep_conversion_function interface... yes
Checking for Comm_create_errhandler interface... yes
Checking if MPI_Info_set takes const char * instead of char * args... yes
MPI F_STATUSES_IGNORE types are defined...
MPI-2 MPI*attr_functions are defined...
MPI-2 MPI_File functions are defined...
MPI-2 MPI_Type_dup and MPI_Exscan are defined...
MPI-2 MPI_Add_error* functions are defined...
MPI_Status f2c/c2f found...
NOTE: MPI library does not have a threaded _r suffix 
Checking for standard C++ library <string>... yes
Check if valid to declare strsignal with <string.h>... yes
Checking if C++ compiler supports Std Runtime Type Information... yes
Checking if open takes O_LARGEFILE... yes
Checking for weak symbols... yes

Looking for /home/yyan7/tools/tau-2.29/external_dependencies/binutils-2.23.2.tar.gz...
wget: http://www.cs.uoregon.edu/research/paracomp/tau/tauprofile/dist/binutils-2.23.2.tar.gz ==> binutils-2.23.2.tar.gz
--2020-02-18 16:11:35--  http://www.cs.uoregon.edu/research/paracomp/tau/tauprofile/dist/binutils-2.23.2.tar.gz
Resolving www.cs.uoregon.edu (www.cs.uoregon.edu)... 128.223.4.25, 2607:8400:205e:40::80df:419
Connecting to www.cs.uoregon.edu (www.cs.uoregon.edu)|128.223.4.25|:80... connected.
HTTP request sent, awaiting response... 200 OK
Length: 36112394 (34M) [application/x-gzip]
Saving to: ‘binutils-2.23.2.tar.gz’

binutils-2.23.2.tar.gz                   100%[================================================================================>]  34.44M  12.1MB/s    in 2.8s    

2020-02-18 16:11:38 (12.1 MB/s) - ‘binutils-2.23.2.tar.gz’ saved [36112394/36112394]

Configuring binutils (see /dev/shm/tau_bfdbuild_34569/binutils-2.23.2/tau_configure.log for details)...
Building (see /dev/shm/tau_bfdbuild_34569/binutils-2.23.2/tau_build.log for details)...
Installing (see /dev/shm/tau_bfdbuild_34569/binutils-2.23.2/tau_build.log for details)...
Checking for bfd.h... yes
Checking for elf-bfd.h... yes
Checking for linking dependencies for libbfd... -lbfd -liberty -lz -ldl
Checking for demangle.h... yes
Checking for shared library compatible libbfd... yes
Checking for ss_allocator support... yes
Checking for librt linkage support ... yes
Checking for Basic librt Real Time Clock Signal support... yes
Smallest Sane Clock Signal Resolution = 1 microseconds
Checking for tr1/unordered_map... yes
Checking for thread local storage support... yes
NOT using GOMP wrapper (OMPT)
Checking OMP NESTED support...yes.
Not found in /opt/tools/tau-2.29-install-mpicc/x86_64/otf2-gcc: libotf2
Looking for /home/yyan7/tools/tau-2.29/external_dependencies/otf2.tgz...
wget: http://www.cs.uoregon.edu/research/tau/otf2.tgz ==> otf2.tgz
--2020-02-18 16:12:10--  http://www.cs.uoregon.edu/research/tau/otf2.tgz
Resolving www.cs.uoregon.edu (www.cs.uoregon.edu)... 128.223.4.25, 2607:8400:205e:40::80df:419
Connecting to www.cs.uoregon.edu (www.cs.uoregon.edu)|128.223.4.25|:80... connected.
HTTP request sent, awaiting response... 200 OK
Length: 5885771 (5.6M) [application/x-gzip]
Saving to: ‘otf2.tgz’

otf2.tgz                                 100%[================================================================================>]   5.61M  4.87MB/s    in 1.2s    

2020-02-18 16:12:12 (4.87 MB/s) - ‘otf2.tgz’ saved [5885771/5885771]

expanding otf2.tgz...
removing otf2.tgz...
configuring otf2... (see /home/yyan7/tools/tau-2.29/otf2-2.1/configure.log for log)...
building otf2... (see /home/yyan7/tools/tau-2.29/otf2-2.1/build.log for log)...
installing otf2 to /opt/tools/tau-2.29-install-mpicc/x86_64/otf2-gcc... (see /home/yyan7/tools/tau-2.29/otf2-2.1/install.log for log)...
...Success.
OTF Library implicitly set to /opt/tools/tau-2.29-install-mpicc/x86_64/otf2-gcc/lib
OTF Header directory implicitly set to /opt/tools/tau-2.29-install-mpicc/x86_64/otf2-gcc/include
Not found in /opt/tools/tau-2.29-install-mpicc/x86_64/libdwarf-gcc: libdwarf
Looking for /home/yyan7/tools/tau-2.29/external_dependencies/libdwarf-20181024.tar.gz...
wget: http://www.cs.uoregon.edu/research/paracomp/tau/tauprofile/dist/libdwarf-20181024.tar.gz ==> libdwarf-20181024.tar.gz
--2020-02-18 16:13:24--  http://www.cs.uoregon.edu/research/paracomp/tau/tauprofile/dist/libdwarf-20181024.tar.gz
Resolving www.cs.uoregon.edu (www.cs.uoregon.edu)... 128.223.4.25, 2607:8400:205e:40::80df:419
Connecting to www.cs.uoregon.edu (www.cs.uoregon.edu)|128.223.4.25|:80... connected.
HTTP request sent, awaiting response... 200 OK
Length: 2192118 (2.1M) [application/x-gzip]
Saving to: ‘libdwarf-20181024.tar.gz’

libdwarf-20181024.tar.gz                 100%[================================================================================>]   2.09M  3.21MB/s    in 0.7s    

2020-02-18 16:13:25 (3.21 MB/s) - ‘libdwarf-20181024.tar.gz’ saved [2192118/2192118]

expanding libdwarf-20181024.tar.gz...
removing libdwarf-20181024.tar.gz...
configuring libdwarf... (see /home/yyan7/tools/tau-2.29/libdwarf-20181024/configure.log for log)...
Found /usr/include/libelf.h
TAU: Using CFLAGS= -I/opt/tools/tau-2.29-install-mpicc/x86_64/libelf-devel-0.158-6.1.x86_64/include to compile libdwarf
building libdwarf... (see /home/yyan7/tools/tau-2.29/libdwarf-20181024/build.log for log)...
installing libdwarf to /opt/tools/tau-2.29-install-mpicc/x86_64/libdwarf-gcc... (see /home/yyan7/tools/tau-2.29/libdwarf-20181024/install.log for log)...
...Success.
Using libdwarf at /opt/tools/tau-2.29-install-mpicc/x86_64/libdwarf-gcc
Adjust ParaProf to use CubeReader...
Adjust PerfDmf to use CubeReader...
***********************************************************************
Installing utilities...
====================================================
 Copy /home/yyan7/tools/tau-2.29/tools/src/contrib/CubeReader.jar to /opt/tools/tau-2.29-install-mpicc/x86_64/lib
====================================================
... done
***********************************************************************
TAU: Copying libomp.so to /opt/tools/tau-2.29-install-mpicc/x86_64/lib/shared-ompt-v5-mpi-openmp ...
ompt_dir=
NOTE: Saving configuration environment to /home/yyan7/tools/tau-2.29/.configure_env/8587ed537dafa9a3c109085987c631ed
NOTE: Enabled Profiling. Compiling with -DPROFILING_ON
NOTE: Building POSIX I/O wrapper
NOTE: GNU gfortran compiler specific options used
NOTE: Using fixes for GNU 4.6+ compiler
NOTE: GNU g++ options used
NOTE: Using LD AUDITOR
NOTE: TAU is adding -lm to the link line. If you do not want this (e.g., -mkl) please use -nolm while configuring.
NOTE: Using Linux TSC Counters for low overhead wallclock time
NOTE: Using OMPT OpenMP options for OMPT 5.0 specification
NOTE: Using the TAU MPI Profiling Interface
NOTE: Using the TAU MPI_Init_thread Profiling Interface Wrapper
NOTE: Using the -DTAU_MPICH3_CONST=const to compile programs
NOTE: Using TAU's MPI-2 extensions
NOTE: Using TAU's MPI-2 Grequest extensions
NOTE: Using TAU's MPI-2 Datarep_conversion extensions
NOTE: Using TAU's MPI-2 Error handler extensions
NOTE: Using TAU's MPI-2 MPI_Info_set const char * interface
NOTE: Using TAU's MPI-2 Attr extensions
NOTE: Using TAU's MPI-2 File extensions
NOTE: Using TAU's MPI-2 Type and Exscan extensions
NOTE: Using TAU's MPI-2 Add_error extensions
NOTE: Using the O_LARGEFILE flag to open
NOTE: Using Weak Symbol support
NOTE: Using BFD support
NOTE: Using ELF support in BFD
NOTE: Using DEMANGLE support
NOTE: Using BFD Shared Linking support
NOTE: Compiler supports Sampling Allocator
NOTE: Using tr1 hash map
NOTE: __thread Using Thread Local Storage (TLS) ***
NOTE: Enabling Stack Unwinding support
NOTE: Using libunwind Unwinder.
NOTE: Using OpenMP Threads ***
NOTE: Using OTF2 library to build tau2otf2 trace converter ***
NOTE: Using DWARF support from libdwarf
NOTE: Using GNU's OpenMP options ***
NOTE: Using OMPT OpenMP options for OMPT 5.0 specification
Script to modify Makefiles created.
Applying script to Makefile, please wait, this may take a while...
 
***********************************************************************
Configuring TAU Build scripts...
Modifying Makefiles in the examples subdirectory...
Setting enduring example Makefiles.
TAU: Copying include directory to /opt/tools/tau-2.29-install-mpicc
TAU: Copying man directory to /opt/tools/tau-2.29-install-mpicc
TAU: Copying examples directory to /opt/tools/tau-2.29-install-mpicc

Configuration complete!
   Please add  /opt/tools/tau-2.29-install-mpicc/x86_64/bin  to your path
   Type "make install" to begin compilation
yyan7@fornax:~/tools/tau-2.29$

````

### Install with clang/clang++
````
sudo ./configure -c++=`which clang++` -cc=`which clang` -arch=x86_64 -ompt=download -bfd=download -dwarf=download -otf=download -unwind=download -prefix=/opt/tools/tau-2.29-install-clang

````
