#!/usr/bin/env python3
"""
run_warpx_scaled.py
WarpX PICMI Python simulation script scaled to maximize GPU memory usage on 4 GPUs.
Target: 40GB A100-PCIE-40GB. Expected usage ~34 GB per GPU.
"""

import os
import sys
from pywarpx import picmi
from pywarpx.callbacks import callfromafterstep

# Physical constants
c = picmi.constants.c
q_e = picmi.constants.q_e

# Read environment variables for scaling (with high-memory A100 defaults)
# Local NZ per GPU: 384 cells
nz_local = int(os.environ.get("WARPX_NZ_LOCAL", 384))
# Particles Per Cell along each dimension: 2 (2*2*2 = 8 PPC)
ppc_dim = int(os.environ.get("WARPX_PPC_DIM", 2))
# Number of time steps to run
max_steps = int(os.environ.get("WARPX_NSTEP", 10))

# Overall Grid Resolution (for 4 GPUs parallelized via MPI, we split along Z)
nx = 256
ny = 256
nz = nz_local * 4

# Physical domain size (scaled along Z with grid size)
xmin = -30e-06
xmax = 30e-06
ymin = -30e-06
ymax = 30e-06
zmin = -56e-06
# Adjust zmax dynamically so cell sizes (dx, dy, dz) remain constant
# Baseline nz = 256 goes from -56um to 12um (length = 68um)
# dz_baseline = 68um / 256 = 0.265625um
dz = 68e-06 / 256.0
zmax = zmin + nz * dz

# Domain decomposition parameters
max_grid_size = 64
blocking_factor = 32

print(f"=========================================================")
print(f"WarpX 3D PICMI Scaled Simulation Init:")
print(f"  Local Grid per GPU:  {nx} x {ny} x {nz_local}")
print(f"  Total Grid size:     {nx} x {ny} x {nz} ({nx*ny*nz:,} cells)")
print(f"  Particles per cell:  {ppc_dim**3} ({ppc_dim}x{ppc_dim}x{ppc_dim})")
print(f"  Steps:               {max_steps}")
print(f"=========================================================")

# Create Cartesian 3D Grid
grid = picmi.Cartesian3DGrid(
    number_of_cells=[nx, ny, nz],
    lower_bound=[xmin, ymin, zmin],
    upper_bound=[xmax, ymax, zmax],
    lower_boundary_conditions=["periodic", "periodic", "dirichlet"],
    upper_boundary_conditions=["periodic", "periodic", "dirichlet"],
    lower_boundary_conditions_particles=["periodic", "periodic", "absorbing"],
    upper_boundary_conditions_particles=["periodic", "periodic", "absorbing"],
    moving_window_velocity=[0.0, 0.0, c],
    warpx_max_grid_size=max_grid_size,
    warpx_blocking_factor=blocking_factor,
)

# Plasma electrons species
plasma_density = 2e23
plasma_xmin = -20e-06
plasma_ymin = -20e-06
plasma_zmin = 0.0
plasma_xmax = 20e-06
plasma_ymax = 20e-06
plasma_zmax = None

uniform_distribution = picmi.UniformDistribution(
    density=plasma_density,
    lower_bound=[plasma_xmin, plasma_ymin, plasma_zmin],
    upper_bound=[plasma_xmax, plasma_ymax, plasma_zmax],
    fill_in=True,
)

electrons = picmi.Species(
    particle_type="electron",
    name="electrons",
    initial_distribution=uniform_distribution,
    warpx_add_int_attributes={"regionofinterest": "(z>12.0e-6) * (z<13.0e-6)"},
    warpx_add_real_attributes={"initialenergy": "ux*ux + uy*uy + uz*uz"},
)

# Beam electrons species
q_tot = 1e-12
x_m = 0.0
y_m = 0.0
z_m = -28e-06
x_rms = 0.5e-06
y_rms = 0.5e-06
z_rms = 0.5e-06
ux_m = 0.0
uy_m = 0.0
uz_m = 500.0
ux_th = 2.0
uy_th = 2.0
uz_th = 50.0

gaussian_bunch_distribution = picmi.GaussianBunchDistribution(
    n_physical_particles=q_tot / q_e,
    rms_bunch_size=[x_rms, y_rms, z_rms],
    rms_velocity=[c * ux_th, c * uy_th, c * uz_th],
    centroid_position=[x_m, y_m, z_m],
    centroid_velocity=[c * ux_m, c * uy_m, c * uz_m],
)

beam = picmi.Species(
    particle_type="electron",
    name="beam",
    initial_distribution=gaussian_bunch_distribution,
)

# Laser setup
e_max = 16e12
position_z = 9e-06
profile_t_peak = 30.0e-15
profile_focal_distance = 100e-06

laser = picmi.GaussianLaser(
    wavelength=0.8e-06,
    waist=5e-06,
    duration=15e-15,
    focal_position=[0, 0, profile_focal_distance + position_z],
    centroid_position=[0, 0, position_z - c * profile_t_peak],
    propagation_direction=[0, 0, 1],
    polarization_direction=[0, 1, 0],
    E0=e_max,
    fill_in=False,
)

laser_antenna = picmi.LaserAntenna(
    position=[0.0, 0.0, position_z], normal_vector=[0, 0, 1]
)

# Yee Electromagnetic solver
solver = picmi.ElectromagneticSolver(
    grid=grid, 
    method="Yee", 
    cfl=1.0, 
    divE_cleaning=0
)

# Define simulation environment
sim = picmi.Simulation(
    solver=solver,
    max_steps=max_steps,
    verbose=1,
    particle_shape="cubic",
    warpx_use_filter=1,
    warpx_serialize_initial_conditions=1,
    warpx_do_dynamic_scheduling=0,
)

# Add elements to simulation
sim.add_species(
    electrons, 
    layout=picmi.GriddedLayout(grid=grid, n_macroparticle_per_cell=[ppc_dim, ppc_dim, ppc_dim])
)
sim.add_species(
    beam, 
    layout=picmi.PseudoRandomLayout(grid=grid, n_macroparticles=100)
)
sim.add_laser(laser, injection_method=laser_antenna)

# Diagnostics (only every 100 steps to reduce CPU/IO overhead during benchmark)
field_diag = picmi.FieldDiagnostic(
    name="diag1",
    grid=grid,
    period=100,
    data_list=["E", "B"]
)
sim.add_diagnostic(field_diag)

# Whitelisted PInsight step callbacks
@callfromafterstep
def my_simple_callback():
    """Whitelisted Python callback to trace step metrics."""
    step = sim.extension.warpx.getistep(0)
    print(f"[Callback] Finished Step {step}")

# Write inputs and initialize C++ WarpX engine
sim.write_input_file(file_name="inputs_3d_scaled")
sim.initialize_inputs()
sim.initialize_warpx()

# Advance simulation
sim.step(max_steps)
print("WarpX simulation step completed successfully!")
