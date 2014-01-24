# Snabb Switch

Check out the [Snabb Switch wiki](https://github.com/SnabbCo/snabbswitch/wiki) to learn about the project.

This Snabb Switch branch supports a full userspace networking stack
provided by [rump kernels](http://rumpkernel.org/).  The example
Snabb Switch app configures the stack as a simple IPv6 router.
Extending the app to support other features offered by rump kernel,
for example firewalls and IPSec, is a simple matter of configuration.
See the file apps/rumpkernel/configrouter.sh for how to configure the
rump kernel networking stack.

Building
--------

After updating git submodules, type `make`.

Testing
-------

`./snabbswitch -t apps/rumpkernel/rumpkernel`

Note, `snabbswitch` requires either root or `cap_ipc_lock` and available
huge pages (`/proc/sys/vm/nr_huge_pages`).

TODO
----

Works, but lots of room for improvements.

* avoid building unnecessary components (build time optimization)
* various small and large runtime performance optimizations, should
  result in many-x performance improvements
* testing could be vastly more comprehensive ... ;-)
