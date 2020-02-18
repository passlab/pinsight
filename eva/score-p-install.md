#### Install to use system default gcc compiler
````
$ ../scorep-6.0/configure --prefix=/opt/tools/scorep-6.0-install-gcc --with-libcudart=/usr/local/cuda

...

Configure command:
  ../scorep-6.0/configure       '--prefix=/opt/tools/scorep-6.0-install-gcc' \
                                '--with-libcudart=/usr/local/cuda'

Configuration summary:
  Score-P 6.0:
    Platform:                   linux (auto detected)
    Cross compiling:            no (auto detected)
    Machine name:               Linux
    otf2 support:               yes, using internal
    opari2 support:             yes, using internal
    cubew support:              yes, using internal
    cubelib support:            yes, using internal

    Score-P (backend):
      C99 compiler used:        gcc
      Link mode:                static=yes, shared=no
      Pthread support:          yes, using gcc -pthread 
      compiler constructor:     yes, using attribute syntax without arguments
      Platform:                 linux (provided)
      TLS support:              yes, using __thread
      PAPI support:             yes
      metric perf support:      yes
      Unwinding support:        no, missing libunwind support
        libunwind support:      no
      Sampling support:         no
      getrusage support:        yes
      RUSAGE_THREAD support:    yes, using -D_GNU_SOURCE
      dlfcn support:            yes, using -ldl
      OpenCL support:           yes
        libOpenCL support:      yes, using -lOpenCL
      I/O Recording features:
        POSIX I/O support:      yes
        POSIX asynchronous I/O support:
                                yes, using -lrt
      OTF2 features:
        SIONlib support:        no
      CUDA support:             yes
        libcudart support:      yes, using -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -R/usr/local/cuda/lib64 -lcudart
        libcuda support:        yes, using -lcuda
        libcupti support:       yes, using -I/usr/local/cuda/include -I/usr/local/cuda/extras/CUPTI/include -L/usr/local/cuda/extras/CUPTI/lib64 -R/usr/local/cuda/extras/CUPTI/lib64 -lcupti
        CUPTI async support:    yes
        CUDA version >= 6.0:    yes
      OpenACC support:          no
      Mount point extraction:   yes
      OpenMP support:           yes, using -fopenmp
        OpenMP pomp_tpd:        yes
        OpenMP ancestry:        yes
      PDT support:              no
      Cobi support:             no
      Timer support:            yes, providing gettimeofday, clock_gettime(CLOCK_MONOTONIC_RAW), tsc (X86_64)
        Timer LDFLAGS:          -lm
      libbfd support:           no
      compiler instrumentation: yes, using nm
      Online access support:    yes
      Memory tracking support:  yes
      Compiler wrappers:        scorep-gcc scorep-g++ scorep-gfortran scorep-mpicc scorep-mpicxx scorep-mpif77 scorep-mpif90 scorep-oshcc scorep-oshfort scorep-nvcc 
      User library wrappers support:
                                no

    Score-P (GCC plug-in):
      GCC plug-in support:      no, missing plug-in headers, please install

    Score-P (libwrap):
      Library wrapper support:  no, llvm-config not found

    Score-P (MPI backend):
      Link mode:                static=yes, shared=no
      C99 compiler works:       yes, using mpicc
      C++ compiler works:       yes, using mpicxx
      F77 compiler works:       yes, using mpif77
      F90 compiler works:       yes, using mpif90
      Library used for MPI:     
      TLS support:              yes, using __thread
      PAPI support:             yes
      metric perf support:      yes
      Unwinding support:        no, missing libunwind support
        libunwind support:      no
      Sampling support:         no
      getrusage support:        yes
      RUSAGE_THREAD support:    yes, using -D_GNU_SOURCE
      dlfcn support:            yes, using -ldl
      OpenCL support:           yes
        libOpenCL support:      yes, using -lOpenCL
        Platform:               linux (provided)
      I/O Recording features:
        POSIX I/O support:      yes
        POSIX asynchronous I/O support:
                                yes, using -lrt
      CUDA support:             yes
        libcudart support:      yes, using -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -R/usr/local/cuda/lib64 -lcudart
        libcuda support:        yes, using -lcuda
        libcupti support:       yes, using -I/usr/local/cuda/include -I/usr/local/cuda/extras/CUPTI/include -L/usr/local/cuda/extras/CUPTI/lib64 -R/usr/local/cuda/extras/CUPTI/lib64 -lcupti
        CUPTI async support:    yes
        CUDA version >= 6.0:    yes
      OpenACC support:          no
      Mount point extraction:   yes
      OpenMP support:           yes, using -fopenmp
        OpenMP pomp_tpd:        yes
        OpenMP ancestry:        yes
      PDT MPI instrumentation:  yes, if PDT available
      Timer support:            yes, providing gettimeofday, clock_gettime(CLOCK_MONOTONIC_RAW), tsc (X86_64)
        Timer LDFLAGS:          -lm
      libbfd support:           no
      compiler instrumentation: yes, using nm
      Online access support:    yes
      Memory tracking support:  yes
      User library wrappers support:
                                no

    Score-P (score):
      C compiler used:          gcc 
      C++ compiler used:        g++
      cube c++ library support: yes, using internal

    Score-P (SHMEM backend):
      Link mode:                static=yes, shared=no
      C99 compiler works:       yes, using oshcc
      C++ compiler works:       yes, using oshcc
      F77 compiler works:       no
      F90 compiler works:       yes, using oshfort
      Library used for SHMEM:   
      TLS support:              yes, using __thread
      PAPI support:             yes
      metric perf support:      yes
      Unwinding support:        no, missing libunwind support
        libunwind support:      no
      Sampling support:         no
      getrusage support:        yes
      RUSAGE_THREAD support:    yes, using -D_GNU_SOURCE
      dlfcn support:            yes, using -ldl
      OpenCL support:           yes
        libOpenCL support:      yes, using -lOpenCL
        Platform:               linux (provided)
      I/O Recording features:
        POSIX I/O support:      yes
        POSIX asynchronous I/O support:
                                yes, using -lrt
      CUDA support:             yes
        libcudart support:      yes, using -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -R/usr/local/cuda/lib64 -lcudart
        libcuda support:        yes, using -lcuda
        libcupti support:       yes, using -I/usr/local/cuda/include -I/usr/local/cuda/extras/CUPTI/include -L/usr/local/cuda/extras/CUPTI/lib64 -R/usr/local/cuda/extras/CUPTI/lib64 -lcupti
        CUPTI async support:    yes
        CUDA version >= 6.0:    yes
      OpenACC support:          no
      Mount point extraction:   yes
      OpenMP support:           yes, using -fopenmp
        OpenMP pomp_tpd:        yes
        OpenMP ancestry:        yes
      PDT SHMEM instrumentation:
                                yes, if PDT available
      intercepting SHMEM calls: yes, using SHMEM profiling interface
      libbfd support:           no
      compiler instrumentation: yes, using nm
      Memory tracking support:  yes
      User library wrappers support:
                                no

  CubeLib 4.4.4:
    Cube installed into:        /opt/tools/scorep-6.0-install-gcc
    Platform:                   linux (auto detected)
    Cross compiling:            no (auto detected)
    cubelib-config:             /opt/tools/scorep-6.0-install-gcc/bin
    For detailed information:   "/opt/tools/scorep-6.0-install-gcc/bin/cubelib-config --help"
    Examples:                   /opt/tools/scorep-6.0-install-gcc/share/doc/cubelib/example

    CubeLib (frontend):
      POSIX socket:             yes
      Networking:               yes
      Cube C++ Library:         yes
      Cube Tools:               yes
      Frontend zlib compression :
                                readonly
        zlib headers :          
        zlib library :            -lz
        Notice:                 Front end part of cube (c++ cube library, tools and GUI) can read compressed cube files, but
                                write only uncompressed cube files.
        zlib compression flags :
                                 -DFRONTEND_CUBE_COMPRESSED_READONLY=yes 
      Use R for cube_dump:      no
        Notice:                 R, Rscript, RInside and Rcpp are necessary
      Data loading strategy :   Keep all in memory, load on demand
      C++ compiler used:        g++
       Compiler flags used:      

  CubeW 4.4.3:
    Cube installed into:        /opt/tools/scorep-6.0-install-gcc
    Platform:                   linux (auto detected)
    Cross compiling:            no (auto detected)
    cubew-config:               /opt/tools/scorep-6.0-install-gcc/bin
    For detailed information:   "/opt/tools/scorep-6.0-install-gcc/bin/cubew-config --help"
    Examples:                   /opt/tools/scorep-6.0-install-gcc/share/doc/cubew/example

    CubeW (backend):
      zlib compression :        yes
        zlib headers :          
        zlib library :            -lz
        Notice:                 Cube (c-writer library) produces compressed cube report.(enabled via environment variable
                                CUBEW_ZLIB_COMPRESSION=true)
        zlib compression flags :
                                 -DBACKEND_CUBE_COMPRESSED=yes 
      Advanced memory handling :
                                no
      Internal memory tracking :
                                no
      Internal memory tracing : no
      C99 compiler used:        gcc
      C++ compiler used:        g++
       Compiler flags used:     -g -O2
       Compiler cxxflags used:  -g -O2

  OPARI2 2.0.5:
    Platform:                   linux (provided)
    Cross compiling:            no (provided)

  OTF2 2.2:
    Platform:                   linux (provided)
    Cross compiling:            no (provided)

    OTF2 (backend):
      C99 compiler used:        gcc
      SIONlib support:          no, missing sionconfig
      Entropy sources:          clock_gettime getpid sysinfo gethostid
      Python bindings support:  no, missing builtins module
      Running tests:            no
        Parallel tests:         yes
        
 ````
     
 ````
     make -j16;
     sudo make install
 ````

### Install with clang compiler
````
Configure command:
  ../scorep-6.0/configure       '--prefix=/opt/tools/scorep-6.0-install-clang' \
                                '--with-libcudart=/usr/local/cuda' \
                                '--with-nocross-compiler-suite=clang'

Configuration summary:
  Score-P 6.0:
    Platform:                   linux (auto detected)
    Cross compiling:            no (auto detected)
    Machine name:               Linux
    otf2 support:               yes, using internal
    opari2 support:             yes, using internal
    cubew support:              yes, using internal
    cubelib support:            yes, using internal

    Score-P (backend):
      C99 compiler used:        clang
      Link mode:                static=yes, shared=no
      Pthread support:          yes, using clang -pthread 
      compiler constructor:     yes, using attribute syntax without arguments
      Platform:                 linux (provided)
      TLS support:              yes, using __thread
      PAPI support:             yes
      metric perf support:      yes
      Unwinding support:        no, missing libunwind support
        libunwind support:      no
      Sampling support:         no
      getrusage support:        yes
      RUSAGE_THREAD support:    yes, using -D_GNU_SOURCE
      dlfcn support:            yes, using -ldl
      OpenCL support:           yes
        libOpenCL support:      yes, using -lOpenCL
      I/O Recording features:
        POSIX I/O support:      yes
        POSIX asynchronous I/O support:
                                yes, using -lrt
      OTF2 features:
        SIONlib support:        no
      CUDA support:             yes
        libcudart support:      yes, using -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -R/usr/local/cuda/lib64 -lcudart
        libcuda support:        yes, using -lcuda
        libcupti support:       yes, using -I/usr/local/cuda/include -I/usr/local/cuda/extras/CUPTI/include -L/usr/local/cuda/extras/CUPTI/lib64 -R/usr/local/cuda/extras/CUPTI/lib64 -lcupti
        CUPTI async support:    yes
        CUDA version >= 6.0:    yes
      OpenACC support:          no
      Mount point extraction:   yes
      OpenMP support:           yes, using -fopenmp
        OpenMP pomp_tpd:        yes
        OpenMP ancestry:        yes
      PDT support:              no
      Cobi support:             no
      Timer support:            yes, providing gettimeofday, clock_gettime(CLOCK_MONOTONIC_RAW), tsc (X86_64)
        Timer LDFLAGS:          -lm
      libbfd support:           no
      compiler instrumentation: yes, using nm
      Online access support:    yes
      Memory tracking support:  yes
      Compiler wrappers:        scorep-clang scorep-clang++ scorep-mpicc scorep-mpicxx scorep-oshcc scorep-nvcc 
      User library wrappers support:
                                yes

    Score-P (GCC plug-in):
      GCC plug-in support:      no, only GNU compilers supported

    Score-P (libwrap):
      Library wrapper support:  yes, using /opt/llvm/llvm-9.0.0-install/bin/llvm-config
      C compiler used:          /opt/llvm/llvm-9.0.0-install/bin/clang
      C++ compiler used:        /opt/llvm/llvm-9.0.0-install/bin/clang++

    Score-P (MPI backend):
      Link mode:                static=yes, shared=no
      C99 compiler works:       yes, using mpicc
      C++ compiler works:       yes, using mpicxx
      F77 compiler works:       yes, using mpif77
      F90 compiler works:       yes, using mpif90
      Library used for MPI:     
      TLS support:              yes, using __thread
      PAPI support:             yes
      metric perf support:      yes
      Unwinding support:        no, missing libunwind support
        libunwind support:      no
      Sampling support:         no
      getrusage support:        yes
      RUSAGE_THREAD support:    yes, using -D_GNU_SOURCE
      dlfcn support:            yes, using -ldl
      OpenCL support:           yes
        libOpenCL support:      yes, using -lOpenCL
        Platform:               linux (provided)
      I/O Recording features:
        POSIX I/O support:      yes
        POSIX asynchronous I/O support:
                                yes, using -lrt
      CUDA support:             yes
        libcudart support:      yes, using -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -R/usr/local/cuda/lib64 -lcudart
        libcuda support:        yes, using -lcuda
        libcupti support:       yes, using -I/usr/local/cuda/include -I/usr/local/cuda/extras/CUPTI/include -L/usr/local/cuda/extras/CUPTI/lib64 -R/usr/local/cuda/extras/CUPTI/lib64 -lcupti
        CUPTI async support:    yes
        CUDA version >= 6.0:    yes
      OpenACC support:          no
      Mount point extraction:   yes
      OpenMP support:           yes, using -fopenmp
        OpenMP pomp_tpd:        yes
        OpenMP ancestry:        yes
      PDT MPI instrumentation:  yes, if PDT available
      Timer support:            yes, providing gettimeofday, clock_gettime(CLOCK_MONOTONIC_RAW), tsc (X86_64)
        Timer LDFLAGS:          -lm
      libbfd support:           no
      compiler instrumentation: yes, using nm
      Online access support:    yes
      Memory tracking support:  yes
      User library wrappers support:
                                yes

    Score-P (score):
      C compiler used:          clang 
      C++ compiler used:        clang++
      cube c++ library support: yes, using internal

    Score-P (SHMEM backend):
      Link mode:                static=yes, shared=no
      C99 compiler works:       yes, using oshcc
      C++ compiler works:       yes, using oshcc
      F77 compiler works:       no
      F90 compiler works:       yes, using oshfort
      Library used for SHMEM:   
      TLS support:              yes, using __thread
      PAPI support:             yes
      metric perf support:      yes
      Unwinding support:        no, missing libunwind support
        libunwind support:      no
      Sampling support:         no
      getrusage support:        yes
      RUSAGE_THREAD support:    yes, using -D_GNU_SOURCE
      dlfcn support:            yes, using -ldl
      OpenCL support:           yes
        libOpenCL support:      yes, using -lOpenCL
        Platform:               linux (provided)
      I/O Recording features:
        POSIX I/O support:      yes
        POSIX asynchronous I/O support:
                                yes, using -lrt
      CUDA support:             yes
        libcudart support:      yes, using -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -R/usr/local/cuda/lib64 -lcudart
        libcuda support:        yes, using -lcuda
        libcupti support:       yes, using -I/usr/local/cuda/include -I/usr/local/cuda/extras/CUPTI/include -L/usr/local/cuda/extras/CUPTI/lib64 -R/usr/local/cuda/extras/CUPTI/lib64 -lcupti
        CUPTI async support:    yes
        CUDA version >= 6.0:    yes
      OpenACC support:          no
      Mount point extraction:   yes
      OpenMP support:           yes, using -fopenmp
        OpenMP pomp_tpd:        yes
        OpenMP ancestry:        yes
      PDT SHMEM instrumentation:
                                yes, if PDT available
      intercepting SHMEM calls: yes, using SHMEM profiling interface
      libbfd support:           no
      compiler instrumentation: yes, using nm
      Memory tracking support:  yes
      User library wrappers support:
                                yes

  CubeLib 4.4.4:
    Cube installed into:        /opt/tools/scorep-6.0-install-clang
    Platform:                   linux (auto detected)
    Cross compiling:            no (auto detected)
    cubelib-config:             /opt/tools/scorep-6.0-install-clang/bin
    For detailed information:   "/opt/tools/scorep-6.0-install-clang/bin/cubelib-config --help"
    Examples:                   /opt/tools/scorep-6.0-install-clang/share/doc/cubelib/example

    CubeLib (frontend):
      POSIX socket:             yes
      Networking:               yes
      Cube C++ Library:         yes
      Cube Tools:               yes
      Frontend zlib compression :
                                readonly
        zlib headers :          
        zlib library :            -lz
        Notice:                 Front end part of cube (c++ cube library, tools and GUI) can read compressed cube files, but
                                write only uncompressed cube files.
        zlib compression flags :
                                 -DFRONTEND_CUBE_COMPRESSED_READONLY=yes 
      Use R for cube_dump:      no
        Notice:                 R, Rscript, RInside and Rcpp are necessary
      Data loading strategy :   Keep all in memory, load on demand
      C++ compiler used:        clang++
       Compiler flags used:      

  CubeW 4.4.3:
    Cube installed into:        /opt/tools/scorep-6.0-install-clang
    Platform:                   linux (auto detected)
    Cross compiling:            no (auto detected)
    cubew-config:               /opt/tools/scorep-6.0-install-clang/bin
    For detailed information:   "/opt/tools/scorep-6.0-install-clang/bin/cubew-config --help"
    Examples:                   /opt/tools/scorep-6.0-install-clang/share/doc/cubew/example

    CubeW (backend):
      zlib compression :        yes
        zlib headers :          
        zlib library :            -lz
        Notice:                 Cube (c-writer library) produces compressed cube report.(enabled via environment variable
                                CUBEW_ZLIB_COMPRESSION=true)
        zlib compression flags :
                                 -DBACKEND_CUBE_COMPRESSED=yes 
      Advanced memory handling :
                                no
      Internal memory tracking :
                                no
      Internal memory tracing : no
      C99 compiler used:        clang
      C++ compiler used:        clang++
       Compiler flags used:     -g -O2
       Compiler cxxflags used:  -g -O2

  OPARI2 2.0.5:
    Platform:                   linux (provided)
    Cross compiling:            no (provided)

  OTF2 2.2:
    Platform:                   linux (provided)
    Cross compiling:            no (provided)

    OTF2 (backend):
      C99 compiler used:        clang
      SIONlib support:          no, missing sionconfig
      Entropy sources:          clock_gettime getpid sysinfo gethostid
      Python bindings support:  no, missing builtins module
      Running tests:            no
        Parallel tests:         yes

````
````
make -j16
sudo make install
````
