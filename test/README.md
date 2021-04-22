# Test cases

This folder contains OpenMP applications to use for testing the tracing shared library.

Currently, we provide the following examples:

 - `hello`  :: The classical hello world example in OpenMP
 - `jacobi` :: A finite difference solver, using the Jacobi iterative method.
 - `lulesh` :: A hydrodynamics modeling/simulation application, intended to be behaviorally similar to applications in the field.

## Setting
The variables in the environment.mk file must be correctly set to point to the right absolute path before the test. 

## Build

To build all of the examples, try:

    make build


## Test

To run all of the test applications in sequence, try:

    make test
