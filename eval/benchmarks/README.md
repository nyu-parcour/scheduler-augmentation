The Makefile contains two main targets:
the compiled test binaries and the results of running those tests.
To build just the binaries, run `make suite` or `make bin/<testname>`.
To run the tests, run `make run` or `make result/<testname>`.
One minor difference between the current version and the prior version is that
`make result/<testname>` produces a single result file with both the execution times and the logs.
The first _n_ lines are the thread and timings where _n_ = the number of thread counts the program is run with.
The remaining lines are logs.
