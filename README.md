Mutilate
========

Mutilate is a memcached load generator designed for high request
rates, good tail-latency measurements, and realistic request stream
generation.

Requirements
============

1. A C++0x compiler
2. scons
3. libevent
4. gengetopt
5. zeromq (optional)

Mutilate has only been thoroughly tested on Ubuntu 11.10.  We'll flesh
out compatibility over time.

Building
========

    sudo apt-get install scons libevent-dev gengetopt libzmq3-dev
    scons

**for building DPDK version:**

1. Install DPDK

    export DPDK_INSTALL_DIR="$HOME/dpdk-build/"
    sudo apt update
    sudo apt-get install -y meson ninja-build python3-pip python3-pyelftools
    wget http://fast.dpdk.org/rel/dpdk-22.11.2.tar.xz
    tar -xf dpdk-22.11.2.tar.xz
    cd dpdk-stable-22.11.2/
    meson build
    cd build
    meson configure --prefix $DPDK_INSTALL_DIR
    ninja
    ninja install
    echo $DPDK_INSTALL_DIR/lib/x86_64-linux-gnu/ | sudo tee /etc/ld.so.conf.d/dpdk_libs.conf
    sudo ldconfig
    echo export PKG_CONFIG_PATH=$DPDK_INSTALL_DIR/lib/x86_64-linux-gnu/pkgconfig | tee -a ~/.bashrc

2. Use scons to build

    DPDK=/users/farbod/dpdk-build/ scons


Basic Usage
===========

Type './mutilate -h' for a full list of command-line options.  At
minimum, a server must be specified.

    $ ./mutilate -s localhost
    #type       avg     min     1st     5th    10th    90th    95th    99th
    read       52.4    41.0    43.1    45.2    48.1    55.8    56.6    71.5
    update      0.0     0.0     0.0     0.0     0.0     0.0     0.0     0.0
    op_q        1.5     1.0     1.0     1.1     1.1     1.9     2.0     2.0
    
    Total QPS = 18416.6 (92083 / 5.0s)
    
    Misses = 0 (0.0%)
    
    RX   22744501 bytes :    4.3 MB/s
    TX    3315024 bytes :    0.6 MB/s

Mutilate reports the latency (average, minimum, and various
percentiles) for get and set commands, as well as achieved QPS and
network goodput.

To achieve high request rate, you must configure mutilate to use
multiple threads, multiple connections, connection pipelining, or
remote agents.

    $ ./mutilate -s zephyr2-10g -T 24 -c 8
    #type       avg     min     1st     5th    10th    90th    95th    99th
    read      598.8    86.0   437.2   466.6   482.6   977.0  1075.8  1170.6
    update      0.0     0.0     0.0     0.0     0.0     0.0     0.0     0.0
    op_q        1.5     1.0     1.0     1.1     1.1     1.9     1.9     2.0
    
    Total QPS = 318710.8 (1593559 / 5.0s)
    
    Misses = 0 (0.0%)
    
    RX  393609073 bytes :   75.1 MB/s
    TX   57374136 bytes :   10.9 MB/s

Suggested Usage
===============

Real deployments of memcached often handle the requests of dozens,
hundreds, or thousands of front-end clients simultaneously.  However,
by default, mutilate establishes one connection per server and meters
requests one at a time (it waits for a reply before sending the next
request).  This artificially limits throughput (i.e. queries per
second), as the round-trip network latency is almost certainly far
longer than the time it takes for the memcached server to process one
request.

In order to get reasonable benchmark results with mutilate, it needs
to be configured to more accurately portray a realistic client
workload.  In general, this means ensuring that (1) there are a large
number of client connections, (2) there is the potential for a large
number of outstanding requests, and (3) the memcached server saturates
and experiences queuing delay far before mutilate does. I suggest the
following guidelines:

1. Establish on the order of 100 connections per memcached _server_
thread.
2. Don't exceed more than about 16 connections per mutilate thread.
3. Use multiple mutilate agents in order to achieve (1) and (2).
4. Do not use more mutilate threads than hardware cores/threads.
5. Use -Q to configure the "master" agent to take latency samples at
slow, a constant rate.

Here's an example:

    memcached_server$ memcached -t 4 -c 32768
    agent1$ mutilate -T 16 -A
    agent2$ mutilate -T 16 -A
    agent3$ mutilate -T 16 -A
    agent4$ mutilate -T 16 -A
    agent5$ mutilate -T 16 -A
    agent6$ mutilate -T 16 -A
    agent7$ mutilate -T 16 -A
    agent8$ mutilate -T 16 -A
    master$ mutilate -s memcached_server --loadonly
    master$ mutilate -s memcached_server --noload \
        -B -T 16 -Q 1000 -D 4 -C 4 \
        -a agent1 -a agent2 -a agent3 -a agent4 \
        -a agent5 -a agent6 -a agent7 -a agent8 \
        -c 4 -q 200000

This will create 8*16*4 = 512 connections total, which is about 128
per memcached server thread.  This ought to be enough outstanding
requests to cause server-side queuing delay, and no possibility of
client-side queuing delay adulterating the latency measurements.

Command-line Options
====================

    mutilate 0.1
    
    Usage: mutilate -s server[:port] [options]
    
    "High-performance" memcached benchmarking tool
    
      -h, --help                    Print help and exit
          --version                 Print version and exit
      -v, --verbose                 Verbosity. Repeat for more verbose.
          --quiet                   Disable log messages.
    
    Basic options:
      -s, --server=STRING           Memcached server hostname[:port].  Repeat to
                                      specify multiple servers.
          --binary                  Use binary memcached protocol instead of ASCII.
      -q, --qps=INT                 Target aggregate QPS. 0 = peak QPS.
                                      (default=`0')
      -t, --time=INT                Maximum time to run (seconds).  (default=`5')
      -K, --keysize=STRING          Length of memcached keys (distribution).
                                      (default=`30')
      -V, --valuesize=STRING        Length of memcached values (distribution).
                                      (default=`200')
      -G, --getcount=STRING         Number of gets in multiget (distribution).
                                      (default=`1')
      -r, --records=INT             Number of memcached records to use.  If
                                      multiple memcached servers are given, this
                                      number is divided by the number of servers.
                                      (default=`10000')
      -u, --update=FLOAT            Ratio of set:get commands.  (default=`0.0')
    
    Advanced options:
      -U, --username=STRING         Username to use for SASL authentication.
      -P, --password=STRING         Password to use for SASL authentication.
      -T, --threads=INT             Number of threads to spawn.  (default=`1')
          --affinity                Set CPU affinity for threads, round-robin
      -c, --connections=INT         Connections to establish per server.
                                      (default=`1')
      -n, --numreqperconn=STRING    Number of requests per connection
                                      (distribution). Once a connection reaches
                                      this number, it is torn down and re-started.
                                      This option facilitates experiments involving
                                      ongoing opening and closing of connections to
                                      the memcached server; it is passed to all
                                      agents and ignored on the master which takes
                                      samples. Default behaviour is to use the same
                                      connection for all requests.  (default=`0')
      -d, --depth=INT               Maximum depth to pipeline requests. Agents may
                                      locally override this setting.  (default=`1')
      -R, --roundrobin              Assign threads to servers in round-robin
                                      fashion.  By default, each thread connects to
                                      every server.
      -i, --iadist=STRING           Inter-arrival distribution (distribution).
                                      Note: The distribution will automatically be
                                      adjusted to match the QPS given by --qps.
                                      (default=`exponential')
      -S, --skip                    Skip transmissions if previous requests are
                                      late.  This harms the long-term QPS average,
                                      but reduces spikes in QPS after long latency
                                      requests.
          --moderate                Enforce a minimum delay of ~1/lambda between
                                      requests.
          --noload                  Skip database loading.
          --loadonly                Load database and then exit.
      -B, --blocking                Use blocking epoll().  May increase latency.
          --no_nodelay              Don't use TCP_NODELAY.
      -w, --warmup=INT              Warmup time before starting measurement.
      -W, --wait=INT                Time to wait after startup to start
                                      measurement.
          --save=STRING             Record latency samples to given file.
          --search=N:X              Search for the QPS where N-order statistic <
                                      Xus.  (i.e. --search 95:1000 means find the
                                      QPS where 95% of requests are faster than
                                      1000us).
          --scan=min:max:step       Scan latency across QPS rates from min to max.
          --report-stats=interval   Report statistics every interval seconds. By
                                      default, report at the end of the benchmark.
          --qps-function=STRING     Adjust the target QPS during the benchmark
                                      according to the given function.
          --qps-warmup=time[:rate]  Warmup at given rate or rate QPS_function(t=0)
                                      for the given number of seconds.
          --stop-latency=N:X        Stop scanning when N-order statistic > Xus.
                                      (i.e. --stop-latency 95:1000 means stop
                                      scanning when 95% of requests are slower than
                                      1000us).
          --scan-search=STRING      Scan latency and search for the QPS. (i.e.
                                      --scan-search 95:1000,20:10:100,500:50 means
                                      scan from 20KQPS to 100KQPS in
                                      10KQPS steps and then in 500KQPS steps while
                                      95% of requests are faster than
                                      1000us, then go back one step, and do 50KPQS
                                      steps).
          --src-port=STRING         A list of IP source ports to use in order to
                                      achieve desired RSS queue distribution.
          --popularity=STRING       Key popularity distribution.
                                      (default=`uniform')
          --agent-sampling          each agent will sample connection stats and if
                                      `save` argument has been given it is also is
                                      applied to agents  (default=off)
    
    Agent-mode options:
      -A, --agentmode               Run client in agent mode.
      -a, --agent=host              Enlist remote agent.
      -p, --agent_port=STRING       Agent port.  (default=`5556')
      -l, --lambda_mul=INT          Lambda multiplier.  Increases share of QPS for
                                      this client.  (default=`1')
      -C, --measure_connections=INT Master client connections per server, overrides
                                      --connections.
      -Q, --measure_qps=INT         Explicitly set master client QPS, spread across
                                      threads and connections.
      -D, --measure_depth=INT       Set master client connection depth.
    
    DPDK options:
          --my-mac=STRING           The MAC address of the agent.
          --my-ip=STRING            The IP address of the agent.
          --server-mac=STRING       The MAC address of the server.
          --cpu-core=INT            The CPU core to run the agent on.
    
    The --measure_* options aid in taking latency measurements of the
    memcached server without incurring significant client-side queuing
    delay.  --measure_connections allows the master to override the
    --connections option.  --measure_depth allows the master to operate as
    an "open-loop" client while other agents continue as a regular
    closed-loop clients.  --measure_qps lets you modulate the QPS the
    master queries at independent of other clients.  This theoretically
    normalizes the baseline queuing delay you expect to see across a wide
    range of --qps values.
    
    Some options take a 'distribution' as an argument.
    Distributions are specified by <distribution>[:<param1>[,...]].
    Parameters are not required.  The following distributions are supported:
    
       [fixed:]<value>              Always generates <value>.
       uniform:<max>                Uniform distribution between 0 and <max>.
       normal:<mean>,<sd>           Normal distribution.
       exponential:<lambda>         Exponential distribution.
       pareto:<loc>,<scale>,<shape> Generalized Pareto distribution.
       gev:<loc>,<scale>,<shape>    Generalized Extreme Value distribution.
    
       To recreate the Facebook "ETC" request stream from [1], the
       following hard-coded distributions are also provided:
    
       fb_value   = a hard-coded discrete and GPareto PDF of value sizes
       fb_key     = "gev:30.7984,8.20449,0.078688", key-size distribution
       fb_ia      = "pareto:0.0,16.0292,0.154971", inter-arrival time dist.
    
    [1] Berk Atikoglu et al., Workload Analysis of a Large-Scale Key-Value Store,
        SIGMETRICS 2012
    
    The following QPS functions are supported:
    
       triangle:min:max:period:maxhold          Triangle wave.
       qtriangle:min:max:period:step            Quantized triangle wave.
       sin_noise:min:max:period:namp:nupdate    Sine wave plus noise.
    
    The following popularity distributions are supported:
       uniform                      Uniform distribution.
       zipf:<theta>                 Zipf distribution.
    
