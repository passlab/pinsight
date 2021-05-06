#!/bin/bash
lttng destroy

# copy files over to data directory
sudo cp -r ~/lttng-traces/* ~/data/