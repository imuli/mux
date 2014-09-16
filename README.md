# mux

mux mounts a virtual file system where each file is a multiplexed pipe. files
may be opened read-only or write-only, and all writes go out to every reader.

## Usage

Standard fuse arguments. I recommend -o intr.

## Todo

* Support polling.
* Dynamically allocate files.

## Copying

public domain.

