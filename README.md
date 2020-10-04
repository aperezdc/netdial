# netdial

This is `netdial`, a small opinionated C utility library to ease some of the
tedious parts of programming with the BSD sockets API while providing sane
defaults.


## Features

* Support for Unix and IP (TCP, UDP) sockets.
* Usage of [address strings](#address-strings) to describe sockets.
* Sane defaults for socket file descriptors:
  - Non-blocking (`O_NONBLOCK`).
  - Close on exec (`O_CLOEXEC`).
* Uses modern best practices internally: `getaddrinfo` + `getnameinfo`,
  `accept4` where available, etc.

### Goals

* Providing a simplified, convenience API for creating, initiating and
  accepting socket connections.
* Encouraging good network programming practices (e.g. non-blocking
  file descriptors as default).

### Non-Goals

* Completely replacing the BSD sockets API.
* Supporting `SOCK_DGRAM` for Unix sockets.
* Supporting protocol families other than IP or Unix sockets.


## Address Strings

Socket addresses are represented as strings of the form
`<type>:<node>[:<service>]`, for example `tcp6:perezdecastro.org:www-http`.

The `<type>` field is mandatory and determines the address family and
connection type. It must be one of `unix`, `unixp`, `tcp`, `udp`, `tcp4`,
`udp4`, `tcp6`, or `udp6`.

### Unix Socket Addresses

For `unix` and `unixp` addresses the `<node>` field must be the socket path
and the `<service>` field must be omitted. The `unixp` type choses a
`SOCK_SEQPACKET` instead of `SOCK_STREAM` as the protocol. Unix sockets with
protocol `SOCK_DGRAM` are not supported (see [non-goals](#non-goals) above).

### IP Socket Addresses
 
For `tcp` and `udp` sockets the `<node>` field is the address where to listen
or to  connect to. The unversioned `<type>` names will choose either IPv4 or
v6 depending on name resolution and what is supported by your system, while
the versioned ones can be used to explicitly choose the IP version to use.
When specifying IP addresses directly, IPv6 addresses must be specified in
between square brackets. IPv6 zone names (and indexes) are supported with the
usual syntax, using a percent sign as separator.

Note that the `<node>` field may be left empty, in which case the address
string represents “any address”, which is equivalent to `0.0.0.0` and `::` for
IPv4 and v6 addresses, respectively. This is particularly convenient when
passing addresses to [netannounce()](#netannounce) for creating listening
sockets.


## API Reference

### netdial

```c
int netdial(const char *address, int flags);
```

Creates a socket connected to `address` (see [Address
Strings](#address-strings)), with the given `flags` (see [Socket
Flags](#socket-flags)).

Returns the socket file descriptor. On error, returns `-1` and sets the
`errno` variable appropriately.

### netannounce

```c
int netannounce(const char *address, int flags, int backlog);
```

Creates a socket listening at `address` (see [Address
Strings](#address-strings)), with the given `flags` (see [Socket
Flags](#socket-flags)). The `backlog` argument defines the maximum amount of
pending connections to queue unaccepted e.g. using [netaccept()](#netaccept).

Returns the socket file descriptor. On error, returns `-1` and sets the
`errno` variable appropriately.

### netaccept

```c
int netaccept(int fd, int flags, char **remoteaddress);
```

Takes the next connection in the queue of pending connections for the `fd`
socket, creates a new socket file descriptor for it with the given `flags`
(see [Socket Flags](#socket-flags)), and returns it.

The `fd` socket must be bound and listening for connections as created by
[netannounce()](#netannounce), and use a connection oriented protocol (`unix`,
`unixp`, `tcp4`, `tcp6`).

If the `remoteaddress` argument is not `NULL`, it is set to the address of the
remote peer. This is the same address returned by [netaddress()](#netaddress)
when used with `NDremote`. The caller is responsible of calling `free()` on
the returned value.

Returns the socket file descriptor for the accepted socket connection. On
error, returns `-1` and sets the `errno` variable appropriately.

### nethangup

```c
int nethangup(int fd, int how);
enum { NDclose, NDread, NDwrite,  NDrdwr }; /* how */
```

Hangs up the `fd` socket connection, partially or completely depending on the
value of the `how` argument:

* `NDclose`: Completely closes the socket, which cannot be used afterwards.
* `NDread`: Half-closes the socket for reading; writing data and manipulating
  socket state is still possible.
* `NDwrite`: Half-closes the socket for writing; reading data and manipulating
  socket state is still possible.
* `NDrdwr`: Closes the socket for data transfer; only manipulating socket
  state is possible.

### netaddress

```c
int netaddress(int fd, int kind, char **address);
enum { NDlocal, NDremote }; /* kind */
```

Obtain an address associated with the `fd` socket, depending on the value of
the `kind` argument:

* `NDlocal`: Local socket address.
* `NDremote`: Remote peer socket address.

The `address` argument must not be `NULL` and will be used to store the
requested address. The caller is responsible of calling `free()` on the
returned value.

Returns `0` on success. On error, returns `-1` and sets the `errno` variable
appropriately.

### Socket Flags

```c
enum {
    NDdefault,

    /* Common socket flags. */
    NDblocking,
    NDexeckeep,
    NDdebug,
    NDreuseaddr,
    NDreuseport,

    /* UDP socket flags. */
    NDbroadcast,

    /* TCP socket flags. */
    NDkeepalive,

    /* Unix socket flags. */
    NDpasscred,
    NDpassec,
};
```

Functions that create socket file descriptors receive a `flags` argument,
which is a *bitwise or* (C operator `|`) of the following values:

* `NDdefault`: Use default options for the socket (non-blocking,
  close-on-exec).
* `NDblocking`: Do no set the socket in non-blocking mode; reading or writing
  may block.
* `NDexeckeep`: Do not set the close-on-exec flag; the socket will be usable
  after the program calls `exec*()`.
* `NDdebug`: Enable socket debugging.
* `NDreuseaddr`: Set the `SO_REUSEADDR` socket option.
* `NDreuseport`: Set the `SO_REUSEPORT` socket option.
* `NDbroadcast`: For UDP sockets, allow sending data to broadcast addresses.
* `NDkeepalive`: For TCP sockets, enable sensing keep-alive messages.
* `NDpasscred`: For Unix sockets, enable receiving the `SCM_CREDENTIALS`
  control message.
* `NDpassec`: For Unix sockets, enable receiving the `SCM_SECURITY` control
  message.
