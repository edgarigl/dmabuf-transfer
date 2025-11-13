# DMA-BUF Guest Handoff Example

This repository demonstrates how to create a DMA-BUF in one VM and transfer it to another VM.
The example shows both the case of single VM process to process transfer over UNIX sockets aswell as multi VM transfer over TCP/IP sockets.

The server creates a set of memfd allocated ranges added into a udmabuf.
For the single VM case, this udmabuf fd is transfered over a UNIX socket to the peer. The peer can then mmap the fd and use it.
For the multi VM case, the udmabuf is first "imported" and grant refs are created for all the ranges. These regfs are put into a blob and sent over the peer.
The peer then "exports" the grant refs creating a gntdev DMA-BUF that the peer can mmap use in the same manner as in the single VM.

This depends on the following linux kernel patch:
https://github.com/edgarigl/linux/commit/150962a96f5ce1a4bd4f666e7237eadf6592c8ea

## Running

Both the example server and client take the same arguments, a mandatory
address description and an option destination vmid used when transfering
dmabufs to another VM:

```
server address-description [vmid]
clirnt address-description [vmid]
```

### Single VM between processes

Run the server and client on the same OS:

```console
$ ./server unixd://unix_socket
Created dma-buf FD 8 spanning 4 ranges (16384 bytes each).
connect to //unix_socket
```
```console
$ ./client unix://unix_socket 
connect to //unix_socket
1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4
```

### Multi VM between processes

In this example run, we're running the server in domain 1 exporting a dmabuf to dom0.

domain 1:
```console
$ ./server tcpd://0.0.0.0:8000 0
Created dma-buf FD 8 spanning 4 ranges (16384 bytes each).
Waiting on connections to 0.0.0.0:8000
num_refs=16
refs[0] = 219
refs[1] = 218
refs[2] = 217
refs[3] = 216
refs[4] = 215
refs[5] = 214
refs[6] = 213
refs[7] = 212
refs[8] = 211
refs[9] = 210
refs[10] = 20f
refs[11] = 20e
refs[12] = 20d
refs[13] = 20c
refs[14] = 20b
refs[15] = 20a
```

domain 0:
```console
$ ./client tcp://10.0.3.16:8000 1
num_refs=16
refs[0] = 219
refs[1] = 218
refs[2] = 217
refs[3] = 216
refs[4] = 215
refs[5] = 214
refs[6] = 213
refs[7] = 212
refs[8] = 211
refs[9] = 210
refs[10] = 20f
refs[11] = 20e
refs[12] = 20d
refs[13] = 20c
refs[14] = 20b
refs[15] = 20a
1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 3 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4 4
```
