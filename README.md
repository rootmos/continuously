continuously
============
[![Build](https://github.com/rootmos/continuously/actions/workflows/build.yaml/badge.svg)](https://github.com/rootmos/continuously/actions/workflows/build.yaml)

A program that uses [inotify](https://man.archlinux.org/man/inotify.7) and [libgit2](https://libgit2.org/) in order to continuously execute something whenever a significant file changes.

This is a feature several modern tools already provide (e.g. [Haskell's stack](https://docs.haskellstack.org/en/stable/commands/build_command/#controlling-when-building-occurs)).
However I frequently jump between (esoteric) programming languages, tools, tasks and trains of thought, and... "uhm, how does one run the tests in this project again?"

But! for me tasks, tools and languages rarely change between (git) working directories:
so continuing the `make` tradition
(or is it too old-school to try `./configure && make install` whenever one obtains a new piece of software?)
I keep a [`.k` file](https://git.sr.ht/~rootmos/scripts/tree/master/k) in each such directory with relevant
operations that I (frequently forget how to) execute.
(I'm quite certain that this workflow emerged when having to remember relevant `kubectl` commands, if you know what I mean when managing several clusters etc.)

Therefore I keep my [nvim](https://git.sr.ht/~rootmos/dot-nvim) open
in one [tmux](https://wiki.archlinux.org/title/Tmux) pane and
in the other a `c k go` (or `c make` [sic!](https://cmake.org/)), where
```alias c='~/.local/bin/continuously --'```
and maybe `.k`:
```sh
go() {
    make build # or whatever's relevant right now
}
```
Thus whenever I [save a file](https://kinesis-ergo.com/foot-pedals/) something relevant gets built.

## Usage
```
usage: continuously [OPTION] [--] [COMMAND [ARG]...]

Run command when files change

options:
  -h  show this text
  -q  keep quiet about event and state transitions
  --  stop processing arguments
```
