import sys
import os

# Query max energy counter value for RAPL domains/subdomains.
def get_max_energy_uj(rapl_domain: int, rapl_subdomain=None):
    rapl_domain_filename = "/sys/class/powercap/intel-rapl:{}/max_energy_range_uj"
    rapl_subdomain_filename = "/sys/class/powercap/intel-rapl:{}:{}/max_energy_range_uj"
    if rapl_subdomain is not None:
        target = rapl_subdomain_filename.format(rapl_domain, rapl_subdomain)
        with open(target, "r") as f:
            max_val = int(f.read().strip())
            return max_val
    else:
        target = rapl_domain_filename.format(rapl_domain)
        with open(target, "r") as f:
            max_val = int(f.read().strip())
            return max_val


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage:\n  python3 get_max_energy.py PKG_ID [SUBDOMAIN_ID]")
        exit(1)

    pkg_id = int(sys.argv[1])
    if len(sys.argv) > 2:
        subdomain_id = int(sys.argv[2])
        print(get_max_energy_uj(pkg_id, subdomain_id))
    else:
        print(get_max_energy_uj(pkg_id))
