# DPDK-RoutingAwareShaper

This project provides a DPDK implementation of the _Routing-Aware Shaper (RAS)_ component of the solution for network determinism that is called _Determinism as a Service (DaaS)_. The single other component of DaaS is the _DaaS Centralized Controller (DaaS-CC)_, which is developed and maintained in a different project.

Descriptions of the RAS and its performance can be found in the following two publications:
- [Lightweight Determinism in Large-Scale Networks](https://doi.org/10.1109/MCOM.003.2300555)
- [Routing-Aware Shaping for Feasible Multi-Domain Determinism](https://doi.org/10.1145/3636534.3696727)

The DPDK application that implements the RAS is called **tm10**. Once made public, the repository can be used for loading the tm10 application into the [FABRIC testbed](https://portal.fabric-testbed.net/) for execution of evaluation tests at scale (network links with rates up to 100 Gb/s).

## tm10

This README document provides instructions for building and running the DPDK application called tm10.

tm10 is the actual implementation of the RAS, inclusive of packet classification, queuing, and scheduling. 

### Preparation for installation on the FABRIC testbed

The following preliminary steps are required for installation of the tm10 application on the FABRIC testbed. Jump to the section titled _Installation of DPDK v22.11.10_ if working with a dedicated host outside the FABRIC tesbed.

- Use the [Jupyter notebook](url) included in the repository to allocated the resources required by the slice of the experiment.

- After ensuring that there is connectivity between the nodes (simply follow the FABRIC notebook), run `install.sh` from the `node_toolsv2` folder on the router node, i.e., the FABRIC VM where tm10 will run (this part is included in the FABRIC Jupyter notebook that is included in this repository):

      $ sudo ./node_toolsv2/install.sh

- Install the Mellanox Drivers. Download the `.iso` file and install it (this part is also included in the FABRIC notebook).

- Reboot the router VM, then run the following commands to install useful libraries:

      $ sudo apt-get update
      $ sudo apt-get install -y libelf-dev libssl-dev libbsd-dev libarchive-dev libfdt-dev libjansson-dev libbpf-dev

- Then execute the following for configuring HugePages:

      $ echo 100 | sudo tee /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
      $ sudo mkdir -p /mnt/huge
      $ sudo mount -t hugetlbfs nodev /mnt/huge -o pagesize=1G,mode=01777
      $ mount | grep hugetlbfs

- Note: The above configuration assumes that your kernel supports 1 GiB hugepages and that sufficient memory is available. Adjust the number of hugepages (e.g., `$ echo 100 …`) based on your system’s total RAM.

### Installation of DPDK v22.11.10

The tm10 application is built using the user-space libraries of the Data Plane Development Kit (DPDK) open-source software project.

Prior to cloning the repository, DPDK Version 22.11.10 must be installed in the target host. The installation involves the following steps:

1. In `${HOME}` (the home directory of the user account), create the directory `${HOME}/DPDK/v22.11.10`:
    
        $ mkdir DPDK
        $ cd DPDK
        $ mkdir v22.11.10 

2. Download the [`dpdk-22.11.10.tar.xz`](https://fast.dpdk.org/rel/dpdk-22.11.10.tar.xz) file

      - You can run this command on FABRIC or any other VM to download the file: `$ wget https://fast.dpdk.org/rel/dpdk-22.11.10.tar.xz`

3. Copy the `dpdk-22.11.10.tar.xz` file from `~/Downloads` (or wherever else it was received) to `~/DPDK/v22.11.10`.

4. In `~/DPDK/v22.11.10`, execute the following command to extract the whole DPDK code base:
        
        $ tar xvJf dpdk-22.11.10.tar.xz

5. Proceed with building the base version of DPDK:

        $ cd ~/DPDK/v22.11.10/dpdk-stable-22.11.10
        $ meson --reconfigure --prefix=~/dpdk22.11Install -Dexamples=l2fwd,l3fwd,vhost build
        $ cd build
        $ ninja

      - Note: If you face out-of-memory issues, check your hugepages.

6. Use the example `l3fwd` application to verify that connectivity is correctly established between the nodes of the slice: 

    - The MAC addresses and interfaces for the two ports of the NIC (e.g., `-a 0000:07:00.0 -a 0000:09:00.0` in the launch command below) and the `l3fwd_rules` files (both `l3fwd_rules_v4.cfg` and `l3fwd_rules_v6.cfg`) must be created/customized based on the placement of the router VM.  Example of `l3fwd_rules_v4.cfg` file: 
    
          R 10.0.0.0/24 1
          R 10.0.1.0/24 0


    - The ARP tables of all nodes of the FABRIC slice must be correctly populated with the necessary MAC addresses. Specifically:
      - The ARP table of a TCP sender node must include the router's ingress MAC address
      - The TCP receiver's ARP table must include the router's egress MAC address.

    - Run the example application with the following command:

           $ sudo /home/ubuntu/DPDK/v22.11.10/dpdk-stable-22.11.10/build/examples/dpdk-l3fwd -l 0-1 -n 4 -a 0000:07:00.0 -a 0000:09:00.0 -- -p 0x3 --config="(0,0,0),(1,0,1)" --rule_ipv4=/home/ubuntu/DPDK/v22.11.4/dpdk-stable-22.11.10/build/examples/l3fwd_rules_v4.cfg --rule_ipv6=/home/ubuntu/DPDK/v22.11.10/dpdk-stable-22.11.4/build/examples/l3fwd_rules_v6.cfg --eth-dest=0,02:07:4e:13:d5:7e --eth-dest=1,10:70:fd:e5:cd:f8`

    - If the application is working, and connectivity is correctly established between the sender and receiver node through this DPDK application, the DPDK installation can be considered successful.

    - Running the l3fwd application is not strictly necessary. Instead, the preliminary configuration steps for connectivity within the FABRIC slice are required. 

### Building the tm10 application

1. Step into `dpdk-stable-22.11.10`:

        $ cd ~/DPDK/v22.11.10/dpdk-stable-22.11.10

2. Download the 'DaaS/PocPhase3' folder from this repository to `dpdk-stable-22.11.10` (or start by cloning the whole repository in ).

3. Open the file `~/DPDK/v22.11.10/dpdk-stable-22.11.10/meson_options.txt` and add the following option right above the line that starts with `option('tests'…)`:
	
	    option('opocp2', type: 'string', value: '',
               description: 'Comma-separated list of DaaS PoCPhase3 applications to build by default')
	
4. Open the file `~/DPDK/v22.11.10/dpdk-stable-22.11.10/meson.build` and add the following two lines right above the line with `# build kernel modules if enabled`:

        # DaaS/RAS Modification
	    subdir('DaaS/PoCPhase3')
		
5. While still in `${HOME}/DPDK/v22.11.10/dpdk-stable-22.11.10`, run the following command:

        $ meson --prefix=~/dpdk22.11Install -Dexamples=l2fwd,l3fwd,vhost -Dopocp2=all build

	The above command must be run only once after installation.

6. Run the following command:

        $ meson --reconfigure --prefix=~/dpdk22.11Install -Dexamples=l2fwd,l3fwd,vhost build

	The above command must be run every time there is a change in the build structure (e.g., a new application is added, or a new source file is added to the code of an application).
	
7. `$ cd build`
	
8. `$ ninja`

    The above command builds the tm10 application. It must be run  every time there is a change in a source file.

    **Note:** Due to the dependencies of different setups, warnings may appear in the output of the `ninja` command. However, the build is successful as long as it does not return errors. 


### Kernel Version 

The `nohz_full` version of the kernel must be installed. In this example, we assume the kernel is `6.5.0NO-HZ-FULL`, adjust as needed. To verify the kernel version that is currently installed, run `$ uname -r`. If `nohz_full` is not shown in the kernel name, the machine must be rebooted and the correct version must be selected upon booting.


1. Check kernel support for full tickless mode:

        $ grep NO_HZ_FULL /boot/config-$(uname -r)
      
      You should see `CONFIG_NO_HZ_FULL=y`. If not, stop here and switch to a kernel with this option enabled.
      
        $ cat /sys/devices/system/cpu/nohz_full
      
      If the output of the command shows `null`, it means that no CPUs are tickless yet, and you must modify `GRUB`.

2. Set the CPU allocation. Example for a 16-core system: DPDK (tickless, isolated) --> CPUs 8–15, Housekeeping / OS work --> CPUs 0–7.

3. Edit the `GRUB` configuration:

        $ sudo nano /etc/default/grub

4. Add or replace these two lines:

        $ GRUB_CMDLINE_LINUX_DEFAULT="quiet splash nohz_full=8-15 rcu_nocbs=8-15 isolcpus=domain,managed_irq,8-15 housekeeping=on,0-7 amd_iommu=on iommu=pt default_hugepagesz=1G hugepagesz=1G hugepages=100"
      
        $ GRUB_CMDLINE_LINUX=""

5. Rebuild `GRUB`:

        $ sudo update-grub

6. Verify if `GRUB` applied your parameters:

        $ sudo grep -R "vmlinuz" /boot/grub* /boot/efi 2>/dev/null | head -20

      If you see in the following in the output: `linux /boot/vmlinuz-6.5.0NO-HZ-FULL root=/dev/vda1 ro console=tty1 console=ttyS0 **without** nohz_full=...`, proceed to Step 7 (manual fallback).

7. Manual fallback (only if Step 6 failed):

        $ sudo nano /boot/grub/grub.cfg 

8. Find every line starting with:

        $ linux   /boot/vmlinuz-6.5.0NO-HZ-FULL root=/dev/vda1 ro  console=tty1 console=ttyS0

9. Append the parameters:

        $ linux   /boot/vmlinuz-6.5.0NO-HZ-FULL root=/dev/vda1 ro console=tty1 console=ttyS0 nohz_full=8-15 rcu_nocbs=8-15 isolcpus=domain,managed_irq,8-15 housekeeping=on,0-7 amd_iommu=on iommu=pt default_hugepagesz=1G hugepagesz=1G hugepages=128


10. Save and exit.

11. `$ sudo reboot`

12. Check the parameters after login:

        $ cat /proc/cmdline

      Expect to see: `nohz_full=8-15 rcu_nocbs=8-15 isolcpus=domain,managed_irq,8-15 housekeeping=on,0-7 ...`

        $ cat /sys/devices/system/cpu/nohz_full
      
      Expect: `8-15`


### Isolation of tm10 from NIC interrupts 

Prior to operation of the tm10 application, its allocated cores must be configured for minimization of the probability that they may accidentally skip service timeslots that should normally be utilized for the transmission of packets. This involves maximizing the frequency of operation of the cores and ensuring that it does not change in time, and isolating the cores from NIC interrupts. 

To prevent the clock from scaling its frequency based on the processing load, the following scripts must be executed in each host that runs the tm10 application every time the host is rebooted: 

    $ ~/DPDK/utils/set_scaling_governor.sh
    $ ~/DPDK/utils/show_scaling_governor.sh   # "performance" should be shown

The two scripts in the above commands (`set_scaling_governor.sh` and `show_scaling_governor.sh` are natively included in the `utils` directory, which is assumed to be stored directly in the `DPDK` directory: `${HOME}/DPDK/utils`).

The following commands isolate the cores of the tm10 application from the NIC interrupts:

      $ sudo ~/DPDK/utils/set_irq_affinity_cpulist.sh 0-4 enps0f1np0
      $ sudo ~/DPDK/utils/set_irq_affinity_cpulist.sh 0-6 enps0f1np1
      $ ~/DPDK/utils/show_irq_affinity.sh enps0f1np0
      $ ~/DPDK/utils/show_irq_affinity.sh enps0f1np1

The two scripts in the above commands (`set_irq_affinity_cpulist.sh` and `show_irq_affinity.sh`) are not natively included in the `utils` directory and must be moved into it from a public repository such as `https://github.com/egobillot/mlnx-irq-tools`.

**Note:** Make sure the scripts are executable before running them (i.e., use `$ chmod +x ...`) 

**Note:** On FABRIC, the interface name and core numbers should be adjusted according to the actual placement of the VM within the FABRIC environment. 

### Operation of the tm10 application

The scripts that launch the tm10 application in the forward (`run_tm10.sh`) and reverse (`run_tm10-v2.sh`) directions are in the `${HOME}/DPDK/v22.11.10/dpdk-stable-22.11.10/DaaS/PoCPhase3/tm10/tm10_scripts` directory. 

The commands in the `run_tm10.sh` script refer to configuration (`.cfg`) files that contain interface configurations (e.g., `intf.cfg`) and scheduler configurations (e.g., `user_01_tm01_1flow.cfg`). The files are stored in `${HOME}/DPDK/v22.11.10/dpdk-stable-22.11.10/DaaS/PoCPhase3/tm10/cfg/tm10/`. 

New tm10 configuration files can be added there by modifying parameters in the existing ones. Files with `v2` or `rev` in the name are for configuration and operation of the tm10 application in the reverse direction.

The commands in the `run_tm10.sh` script also refer to local identifiers for the following items:
- network interfaces that the application uses for receiving and transmitting packets (e.g., `04:00.1`);
- identifiers of the CPU cores that are to be reserved for operation of the application;
- specification of the link rate selected for operation of the application. 

The above configuration items may have to be customized based on the local environment and target link rate.

Every time there is a change in the source code, re-build it using the `ninja` command in the `build` directory. 

  - Launch `run_tm10-v2.sh` for handling reverse traffic, if the router node is in the reverse data path of the end-to-end connection.
  - Launch `run_tm10.sh` for handling forward traffic. 

- Example:

      $ cd ~/DPDK/v22.11.10/dpdk-stable-22.11.10/DaaS/PoCPhase3/tm10/tm10_scripts
      $ sudo bash run_tm10.sh 1000 user_01_tm01_1flow.cfg 2
  
    The arguments in the command above have the following meanings: 
    - 1000 Mb/s link rate
    - `user_01_tm01_1flow.cfg` configuration file (includes the ECN marking threshold parameter, which can be adjusted).
    - 2 seconds interval for statistics reports.

- Example

      $ sudo bash run_tm10-v2.sh 1000 user_01_tm-rev.cfg 2
    
    The command above is for reverse traffic, with a single queue for TCP ACKs traveling in the reverse direction.

**Note:** It is strictly necessary to execute both scripts in `sudo` mode, otherwise access to critical resources is denied.

**Note:** Because of the `sudo` execution of the scripts, it is also strictly necessary that all paths within the scripts start with `/home/username/`, and not with `${HOME}/` or `~/`.

### Customization of `run_tm10.sh`

The following command is included in run_tm10.sh for the forwad traffic:

      /home/username/build/DaaS/PoCPhase3/dpdk-tm10 -l 12,13,14,15 -a 09:00.0 -a 07:00.0 -n 3 --file-prefix rte3 -- --speed "SPEED" --pfc 0,1,0,13,14,15,${TM10}/cfg/tm10/intf.cfg,${TM10}/cfg/tm10/CFG_FILE" --stp "INTERVAL"`

Specifically, the following parts may need to be adjusted according to the current setup (FABRIC slice or standalone host):

    -a 09:00.0 -a 07:00.0` # depending on the interfaces
    --pfc "0,1,0,...."` or `--pfc "0,0,1,...." # one option is for forward traffic, the other for reverse traffic

Queue selection (packet classification) for the traffic flows is done by looking at the source port field of the  packet header:

  - `SRC_PORT % 1024` (if the source port number of an incoming packet is 1025, the packet goes to queue 1, 1026 goes to queue 2, etc.).

  - In `iperf3`, the source port number of the first `SYN-SYNACK` packet cannot be customized; `iperf3` assigns a random port number between 32768 and 60999. Because of that, the tm10 code in `tmSched.c` maps port numbers higher than 32768 onto queue 1.

- In `iperf3`, parallel flows in a single session are assigned consecutive source port numbers that can be controlled (i.e., with option `-c 1025 -P 10` the flows get ports 1025, 1026, 1027, ..., 1034).

- For the single-queue cases, the code maps source port numbers between 30000 and 31000 onto queue 1. This way, up to 1000 flows can be tested in a single queue. This rule can be modified in the source code of tm10 (`tmSched.c`).

### Extra commands

- Kill all tm10 processes on the router node
      
      $ sudo pkill -f tm10   

- Free up some HugePages space

      $ sudo rm -f /mnt/huge/rte*map_*
      $ sudo rm -f /mnt/huge1G/rte*map_*

## Support

- Fatih Berkay Sarpkaya (fatih.sarpkaya@nokia.com)
- Andrea Francini (andrea.francini@nokia-bell-labs.com)

## License

Clear BSD License

