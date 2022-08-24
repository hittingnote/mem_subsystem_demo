## Demo 3: Scatter-gather list

### Introduction

This demo shows how to construct the scatter-gather list from the page list, and DMA-mapped the scatter-gather list to the RDMA NIC. It follows Demo 2 in page list obtaining and adds scatter-gather list construction. Although Linux kernel has provided `sg_alloc_table_from_pages` to build scatter-gather list directly from the page list and squash each contiguous pages into a single scatter-gather element, it does not consider the max segment size of the RDMA NIC. Therefore, this demo build the scatter-gather list from scratch to ensure each scatter-gather element does not exceed the maximum segment size. 

When the userspace application starts, it initializes the buffer, and passes the virtual address of the buffer and its size to the kernel. The kernel build the scatter-gather list, DMA-mapped the scatter-gather list, and perform RDMA communication. Finally, the buffer in the server is populated with the messages originally stored in the client buffer. 

### Prerequisite

This demo requires to have the OFED kernel. This demo is written based on `mlnx-ofed-kernel-5.0`. Readers can modify the demo if they use other OFED kernels. 

### Steps to build this demo

1.Copy this demo into the OFED kernel. You may need to modify the Makefile and some codes. This modification takes little effort. Then compile the OFED kernel.

2.Install the OFED kernel

```bash
$ sudo /etc/init.d/openibd restart
```

3.Run the application code

```bash
$ make user_app
$ ./user_app -d [dev_name] -p [tcp_port] -i [ibv_port] -x [sgid_index] [servername]
```

If a servername is specified at last, this program will run as a client connecting to the specified server. Otherwise, it will start as a server

4. Clean the demo

```bash
$ make clean
```
