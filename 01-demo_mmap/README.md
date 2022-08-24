## Demo 1: Memory Map

### Introduction

This demo shows how to memory-map a kernel space memory to the user. The steps are very simple. Before entering our self-defined `mmap` function, the kernel finds for the user application the first virtual memory region that has not be allocated/mapped yet and fits into the size requested by the user. When the kernel enters our self-defined `mmap`, it finds the physical address corresponding to `buffer`, and map the physical address to the user's memory region by calling `remap_pfn_range`. Note that `page >> PAGE_SHIFT` is the page number corresponding to the physical address `page`. Then, the physical memory is successfully mapped into the userspace. 

### Steps to build this demo

1.Compile and install the kernel module

```bash
$ make
$ sudo make install
```

2.Run the application
```bash
$ ./user_app
```

You will see the output, which is the same as `buffer` in the kernel code:

```bash
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
```

3.Clean the demo

```bash
$ sudo make uninstall
$ make clean
```

