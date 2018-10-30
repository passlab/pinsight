# OpenMP OMPT Event Tracing and Visualization

## Visuomp shared library

The core of this project is a shared library called `visuomp`, which generates [LTTng][lttng] trace data for OpenMP [OMPT][ompt] events.

   [lttng]: https://lttng.org
   [ompt]: https://www.openmp.org/wp-content/uploads/ompt-tr.pdf

### Install

To be able to build/run the OMPT event tracing library, you will need to install several dependencies.

For Ubuntu 16.04 systems:

    sudo apt-get install build-essential cmake git

LTTng has some special [setup instructions][lttng-install].

   [lttng-install]: https://lttng.org/docs/v2.10/#doc-installing-lttng

    # After following the LTTng setup instructions:
    sudo apt-get install lttng-tools lttng-modules-dkms liblttng-ust-dev

You will also need to build the [LLVM OpenMP runtime][llvm-openmp].

An example of that build process (taken from our `Dockerfile`), building to `/opt/openmp-install`:

    git clone https://github.com/llvm-mirror/openmp.git && \
    cd openmp && \
    git remote update && \
    cd .. && \
    mkdir -p openmp-build && \
    mkdir -p openmp-install && \
    cd openmp-build && \
    cmake -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=../openmp-install \
      -DOPENMP_ENABLE_LIBOMPTARGET=off \
      -DLIBOMP_OMPT_SUPPORT=on \
      ../openmp

    cd openmp && make && make install

   [llvm-openmp]: https://github.com/llvm-mirror/openmp


### Build

To build the main `visuomp` shared library, use the GNU `Makefile` provided in the top-level directory of this repo:

    make

The instructions above will result in `libvisuomp.so` being located at `lib/libvisuomp.so`.


### Run

#### Tracing script

In the `scripts/` directory, a script called `trace.sh` is provided.
This script helps make generating LTTng traces for OpenMP programs easier.

Example using the `jacobi` application:

    trace.sh /tmp/ompt-jacobi \
      /opt/openmp-install/lib/libomp.so \
      /opt/pinsight/lib/libvisuomp.so \
      ./jacobi 2048 2048


#### Manual LTTng tracing session

Since we're using LTTng, we have to set up a [tracing session][lttng-tracing-session], and enable some [event rules][lttng-event-rules] before starting tracing:

   [lttng-tracing-session]: https://lttng.org/docs/v2.10/#doc-tracing-session
   [lttng-event-rules]: https://lttng.org/docs/v2.10/#doc-event

    # Create a userspace session.
    lttng create ompt-tracing-session --output=/tmp/ompt-trace

    # Create and enable event rules.
    lttng enable-event --userspace lttng_visuomp:'*'

    # Start LTTng tracing.
    lttng start

Once the session has started, LTTng's daemon will be ready to accept the traces our application generates.

In order to generate traces about OMPT events, our `libvisuomp.so` library needs to be preloaded by the application.
We also need to specify which OpenMP runtime to use.

In order to ensure the 2 shared libraries are loaded correctly, we use the `LD_PRELOAD` environment variable, like so:

    # Run an OpenMP application with our shared libraries.
    LD_PRELOAD=/opt/openmp-install/lib/libomp.so:/opt/pinsight/lib/libvisuomp.so ./your_application

Once the application has run, we can tell LTTng to stop tracing:

    # Stop LTTng tracing.
    lttng stop
    lttng destroy

The resulting trace files will be located at `/tmp/ompt-trace`, and can be loaded into TraceCompass or Babeltrace for analysis.


-----

### Screenshot

#### Lulesh tracing and visualization with Tracecompass
 ![Lulesh tracing and visualization with Tracecompass](doc/OMPT_LTTng_TraceCompass.png). The picture was created in Tracecompass using [Data driven analysis](
 http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Data_driven_analysis). To generate visualizations like this yourself, look at the `tracecompass/` folder in this repository.


### Analyses considered

 1. Overhead analysis.
 1. Load balancing analysis.
 1. Offline analysis for configuring power usage and frequency (perhaps binary-based?).
