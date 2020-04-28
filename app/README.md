# NPF-Router: a demo NPF+DPDK application

**NPF-Router** is a NPF+DPDK application provided as a Docker container.
It illustrates how NPF can be integrated with the Intel DPDK framework.
Together with _docker-compose_, it can also be used to spawn a virtual test
network for basic NPF functionality testing.

**WARNING**: This is an application for demos and testing: do not expect
compliance with the standards, security or maximum performance.

# Running

**Prerequisite**: Docker running on a Linux host.

Spin up the test network:
```shell
docker-compose up
```

(Re)load NPF configuration:
```shell
docker-compose exec npf-router npfctl reload
```

Enter the router shell:
```shell
docker-compose exec npf-router bash -i
```
