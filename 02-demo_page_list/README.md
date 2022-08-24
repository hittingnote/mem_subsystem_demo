## Demo 2: Get page list

### Introduction

This demo shows in detail how to get the page list corresponding to the user's virtual memory region. The major steps are written in `get_pagelist_and_pin` in `demo_kern_core.c`. Although Linux kernel has provided a function (`get_user_pages`) to get the page list, what we also needs to do is to pin the obtained pages so that page swap cannot occur. When the page list is obtained, the kernel can `kmap` to these pages and access those pages. The modification to the pages in the kernel is valid to the user. When the user application terminates, the kernel needs to unpin those pages. 

### Steps to build this demo

1.Compile and load the kernel module

```bash
$ make
$ sudo make install
```

2.Run the user application

```bash
$ ./user_app
```

The output is a list of contents in the array. Each column is as follows: 

```bash
array index | virtual starting address | address offset | page index | offset in each page | virtaddr[i]
```

The amount of output depends on the size of array the user application declares. 

3.Clean the demo

```bash
$ sudo make uninstall
$ make clean
```
