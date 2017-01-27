# Reliable &amp; Rapid UDP Transfer #

## Overview

In this program, a UDP reliable file transfer protocoliis implemented
on application-level with 3 different timeout mechanism.

## Specification

The UDP reliable file transfer protocol includes a sender and a receiver.
Sender keeps sending the content of a file to the receiver.
Receiver keeps receiving the packets from sender and output to another file.
To cope with the packet loss and re-ordering in the network, sender and receiver
should detect the packet loss event and re-ordering event (by using the timeout method)
and deal with it (re-transmit the packet).

Note1: The receiver should NOT read the specific file locally.
Note2: For SIGALRM timeout method, you may need to use siginterrupt to allow SIGALRM signal
          to interrupt a system call.
Note3: The command to generate a random file with a specified size

```
dd if=/dev/urandom of=<Output File Name> bs=<File Size> count=1
```

  For example, the following command generates a file named "a_big_file" with file size=5MB
```
dd if=/dev/urandom of=a_big_file bs=5MB count=1
```

## Environment: Simulated Network Scenario

Use tool 'tc' to simulate packet loss.
'tc' is a program to control the traffic in kernel.
With this tool, you can set network parameters on a NIC.

* Add a rule which setting the packet loss rate and packet re-ording.
   Usage: sudo tc qdisc add dev <Device> root netem loss <Packet Loss Rate> delay <Delay time> <Variation> distribution normal

   For example, following rule can be used to set the packet loss rate with '5%', delay time with 7ms~13ms on the device 'lo'.

```bash
sudo tc qdisc add dev lo root netem loss 5% delay 10ms 3ms distribution normal
```

* Delete all rules on a device
   Usage: sudo tc qdisc del dev <Device> root

   For example, the following command can delete all rules setting on the device 'lo'.

```bash
sudo tc qdisc del dev lo root
```

