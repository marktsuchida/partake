<!--
This file is part of the partake project
Copyright 2020-2023 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# partake

partake is a shared-memory object broker for processes on the same machine. A
small daemon, `partaked`, owns a pool of shared memory and hands out buffers
("objects") to client processes, which pass them to each other by key instead
of copying the data.

A producer allocates an object, writes into it, and shares it; consumers open
it by key and map the same memory read-only. Objects are reference-counted by
the daemon and freed when the last holder closes them or disconnects.

**Status:** under development and not yet usable. There is no client library
yet, and the protocol may change without notice.

## Goals

- Zero-copy sharing of large buffers (e.g. images, sampled data) between local
  processes, with low per-object overhead.
- A simple, well-defined protocol (FlatBuffers over a Unix-domain socket; see
  `protocol/partake_protocol.fbs`). Clients planned for C++, C, Python, Java,
  Rust, and potentially others.
- Single-writer semantics: objects are written once by their creator and are
  immutable once shared, so a key always refers to the same content. A
  `PRIMITIVE` object policy is also available, which allows mutable sharing.
- Robust cleanup: resources held by a client are released when it disconnects,
  even if it crashes.
- Support for advanced patterns where needed: buffer reuse for streaming
  (unshare), shared read-write objects with external coordination, and vouchers
  for handing off objects without an open connection.
- Portability across Linux, macOS, and Windows, with a choice of shared-memory
  mechanisms (POSIX, System V, Win32; huge/large pages).

## Non-goals

- Communication between machines; for the time being, partake is strictly
  local. (Future mechanisms to integrate with RDMA or similar are not ruled
  out.)
- Message passing, queuing, or pub/sub: partake moves buffers, not
  notifications. Telling other processes which key to open is left to the
  application (or to whatever IPC it already uses). (We don't rule out the
  possibility of building message queues *on top of* partake.)
- Serialization or data formats: objects are untyped byte buffers. It's up to
  cooperating clients to interpret an object as a (NumPy-like)
  multi-dimentional array, an Apache Arrow RecordBatch, or whatever else.
- Persistence: objects live only as long as the daemon. (However, a "bring your
  own file to memory-map and share" feature is not ruled out.)
- Security isolation between mutually untrusting clients.
