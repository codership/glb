GLB
===
#### (_glbd_ and _libglb_: TCP proxy daemon and load balancing library in one bottle)

Copyright (C) 2007-2013 Codership Oy <info@codership.com>

Mailing list:
https://groups.google.com/forum/?fromgroups=#!forum/codership-team


### DISCLAIMER:
This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See COPYING for license details.


### ABOUT:
_glbd_ is a simple TCP connection balancer made with scalability and
performance in mind. It was inspired by _pen_, but unlike _pen_ its
functionality is limited only to balancing generic TCP connections.

#### Features:
 - list of backend servers is configurable in runtime.
 - supports server "draining", i.e. does not allocate new connections to
   server, but does not kill existing ones, waiting for them to end
   gracefully.
 - can use epoll API provided by Linux version 2.6 and higher for ultimate
   routing performance.
 - _glbd_ is multithreaded, so it can utilize multiple CPU cores. Also, if your
   OS does not support epoll API, consider using several threads even on a
   single core machine as it will lessen poll() overhead proportionally and
   can improve overall performance by a factor of 2 or more.
 - optional watchdog module can monitor destinations and adjust routing table
   automatically.

_libglb_ is a shared library that provides 0-effort connection balancing to
any Linux application that uses standard libc `connect()` call by overloading
that function in runtime. No other program functionality is affected and no
program modification or recompilation is needed. See below for details.


### BALANCING POLICIES:
GLB (both _glbd_ and _libglb_) supports five balancing "policies":

 a) **least connected** - new connection will be directed to the server with
    least connections (corrected for server "weight"). This policy is default.

 b) **round-robin** - each new connection is routed to the next destination
    in the list in circular order.

 c) **single** - all connections are routed to a single server with the highest
    weight available. All routing will stick to that server until it fails or
    a server with a strictly higher weight is introduced.

 d) **random** - connections are distributed randomly between the servers.

 e) **source tracking** - connections originating from the same address are
    directed to the same server. For details about this policy see below.

`-T|--top` option was introduced in GLB 0.9.2. It restricts all balancing
policies to a subset of destinations with top weight. E.g. if there are servers
configured with weight 1 and 2, all balancing will happen only between servers
with weight 2 as long as at least one of them is available.


### MAXIMUM CONCURRENT CONNECTIONS:
Maximum connections that can be opened via _glbd_ simultaneously depends on the
system open files limit and is 493 for a standard limit of 1024. If needed _glbd_
will attempt to increase open files limit as much as allowed by the effective
user privileges. For unprivileged user it is normally 4096 which results
in max 2029 connections.

On Linux open files limit may be checked with `ulimit -n` and if necessary
increased in `/etc/security/limits.conf`.


### COMMAND LINE OPTIONS:
See output of the `--help` option.


### RUNTIME MANAGEMENT:
Runtime management can be done either through FIFO file, or network socket.
By default network socket is not opened; address and port to listen at must
be explicitly specified with `-c` option.

#### To add/modify/delete backend server (destination):
send server specification in the form `<IP address>:<port>[:weight]` where
weight is an integer to the daemon. Connections are distributed proportionally
to the weight. Default weight is 1. Weight of 0 means drain the server.
Negative weight means delete the server completely (all connections to that
server are closed immediately). This works both on socket connection and on
FIFO file.

#### To see the stats:
send `getstat` command to the daemon. This works only on socket connection
since it implies response.

##### Example:
(here _glbd_ is listening at 127.0.0.1:4444)
```
$ echo "192.168.0.1:3307:5" | nc -q 1 127.0.0.1 4444
OK
$ echo "192.168.0.2:3307:5" | nc -q 1 127.0.0.1 4444
OK
$ echo "getinfo" | nc -q 1 127.0.0.1 4444
Router:
----------------------------------------------------
   Address : weight usage conns
192.168.0.1:3307 : 5.000 0.000 0
192.168.0.2:3307 : 5.000 0.000 0
----------------------------------------------------
Destinations: 2, total connections: 0
```
`usage` here is some dimensionless metric of how much destination is staffed
with connections (relative to weight). Ranges from 0 (totally unused) to 1.0
(very busy). Router tries to keep `usage` equal on all destinations.


### ADDRESS CONVENTIONS:
All network addresses are specified in the form `IP|HOSTNAME:PORT:WEIGHT`.
Depending on the context some parts can be optional, in that case they can be
omitted. For example address to listen for client connections can be specified
either as `HOSTNAME:PORT` or just `PORT`. In the latter case _glbd_ will listen for
client connections on all interfaces. Backend servers can be specified as
`HOSTNAME1,HOSTNAME2,HOSTNAME3`. In that case incoming port number will be used
for `PORT` value and 1 will be used for `WEIGHT` value.


### PERFORMANCE STATISTICS:
GLB allows to query raw performance statistics through control socket using
`getstat` command. The client can use these data to obtain useful information,
e.g. average number of reads per poll() call.
```
$ echo "getstat" | nc -q 1 127.0.0.1 4444
in: 6930 out: 102728 recv: 109658 / 45 send: 109658 / 45 conns: 0 / 4 poll: 45 / 0 / 45 elapsed: 1.03428
```
Statistics line consists of fields separated by spaces for ease of parsing in
scripts. A few description fields are added to assist in human reading. Value
fields are all even and go as follows:

  2 - number of bytes received on incoming interface (client requests)
  
  4 - number of bytes sent from incoming interface (server responses)
  
  6 - number of bytes passed through `recv()` call
  
  8 - number of `recv()` calls
  
  10 - number of bytes passed through `send()` call (should be equal to p.6)
  
  12 - number of `send()` calls
  
  14 - number of created connections
  
  16 - number of concurrent connections
  
  18 - number of read-ready file descriptors returned by `poll()/epoll_wait()`
  
  20 - number of write-ready file descriptors returned by `poll()/epoll_wait()`
  
  22 - number of times `poll()/epoll_wait()` triggered
  
  24 - time elapsed since last statistics report (seconds)


All values except for 16 and 24 are totals accumulated since the last report.
In order to obtain some variable rate it must be divided by the elapsed time.
On 32-bit architectures the values are stored in 4-byte integers and can
overflow after enough time elapsed, so the first statistics report in the series
may need to be discarded.


### SOURCE TRACKING CAPABILITY:
GLB features simple source tracking capability where connections originating
from one address can be routed to the same destination, chosen randomly
among available destinations according to their weights. One limitation of such
scheme is that when the destination list changes, destination choice for new
connections changes as well while established connections naturally stay
unchanged. Also when a destination is marked unavailable, all connections that
would be routed to it will fail over to another, randomly chosen destination.
When the original target becomes available, all new connections will be routed
back to it.

In other words source tracking should be considered a best effort feature and
will work best for short-lived connections.


### WATCHDOG:
_(NOTE: this is a work in progress and neither functionality nor terminology is
       final.)_

Without the watchdog GLB can check destination availability only via the
ability to establish TCP connection to destination. For most use cases TCP
connectivity is an insufficient check. E.g. for HTTP server it is important to
be able to serve web pages and for DBMS server - to be able to execute queries.
Watchdog module implements asynchronous monitoring of destinations via backends
made to check particular service availability.

Watchdog is enabled with a `-w|--watchdog` option. Its parameter is a string
containing the backend ID string, optionally followed by a colon and backend
configuration options. For example:
```
$ glbd -w exec:"mysql.sh -utest -ptestpass" -t 2 3306 192.168.0.1 192.168.0.2
```
This initializes `exec` backend that executes external programs. In this case
it will execute `mysql.sh` script (can be found in the `files/` directory,
must be placed in `PATH`) with provided parameters to check MySQL servers
at 192.168.0.1 and 192.168.0.2 (in parallel threads). To pass server
address to a script, `exec` backend will insert `host:port` string as the first
argument after a script/command name, so the actual command that would be
executed in this case looks like:
```
"mysql.sh 192.168.0.1:3306 -utest -ptestpass"
```
Check interval is set with `-i|--interval` parameter (fractional seconds, default 1.0).


### DESTINATION DISCOVERY:
If destinations can supply information about other members of the cluster
it can be used to automatically populate watchdog destination list if
`-D|--discover` option is supplied. Currently only MySQL/Galera nodes are known
to do this, so it is a Galera-only option.


### RUNNING GLBD AS A "SERVICE":
See `README` in the `files/` directory.


### USING LIBGLB:
Using _libglb_ requires 2 environment variables to be set:

`LD_PRELOAD=<path-to-libglb>`

and

`GLB_OPTIONS='options string'`

  - allows to specify GLB options to _libglb_ the same way as command line
  parameters for _glbd_. It is limited however in that options and their values
  cannot contain whitespaces and commas and short options cannot be
  concatenated (i.e. `-qri10` should go as `-q -r -i 10`). Parsing errors and
  options unsupported by _libglb_ (like `-d`) will be silently ignored.
  In particular, watchdog option cannot be specified this way.
  (See `GLB_WATCHDOG` environment variable below)

##### Example:
```
$ LD_PRELOAD=src/.libs/libglb.so \
GLB_OPTIONS="--random 3306 192.168.0.1 192.168.0.2 192.168.0.3" \
mysql -uroot -prootpass -h127.0.0.1 -P3306

Welcome to the MySQL monitor. Commands end with ; or \g.
Your MySQL connection id is 76
Server version: 5.5.28 Source distribution, wsrep_24dev.7.r3830
...
```

#### Additional _libglb_ parameters:

In case `GLB_OPTIONS` is not sufficient (e.g. watchdog option needs to be
specified) below there is a list of additional environment variables (which
take precedence over `GLB_OPTIONS`). Note however, that all these options except
`GLB_WATCHDOG` are considered deprecated at the moment.

`GLB_WATCHDOG=<watchdog specification>`

  Interpreted the same way as `--watchdog` parameter.

`GLB_TARGETS=H1[:P1[:W1]],[H2[:P2[:W2]],...]`

  Is a comma-separated list of target servers among which the client
  connections must be distributed.

`GLB_BIND=<addr>`

  Where `<addr>` is interpreted the same way as `LISTEN_ADDR` parameter for _glbd_.
  Whenever application attempts to initiate connection to this address, the
  request will be intercepted and connection established to one of the
  servers from `GLB_TARGETS` list according to balancing rules.

`GLB_POLICY=single|random|source`

  Default libglb balancing policy is "round-robin", "single", "random" and
  "source tracking" policies can be specified with `GLB_POLICY` variable.

(The meaning of `GLB_POLICY=source` in this case is that all connections from
this client will be routed to the same random destination, and fail over to
another if the primary destination fails. Thus client-server affinity is
achieved, however load from many clients will be spread over all available
destinations.)

`GLB_CONTROL=[IP:]PORT`

  Interpreted the same way as `--control` parameter of _glbd_. Application will
  open a socket at a specified address to listen to control commands. Due
  to library functionality limited only to `connect()` call, no traffic
  statistics or connection count is maintained, so `"getstat"` command is a
  noop and `"getinfo"` only prints out a routing table.
