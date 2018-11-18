# Extensions API

NPF provides extensions framework for easy addition of custom functionality.
An extension implements the mechanism which can be applied to the packets
or the connections using rule procedures described in a previous chapter.

An extension consists of two parts: a parser module which is a dynamic library
(.so file) supplementing the
[npfctl(8)](http://man.netbsd.org/cgi-bin/man-cgi?npfctl+8+NetBSD-current)
utility and a kernel module.  The syntax of npf.conf supports arbitrary
procedures with their parameters, as supplied by the modules.

As an example to illustrate the interface, source code of the random-block
extension will be used.  Reference:

* Parser module for npfctl: [npfext_rndblock.c](../src/libext/ext_rndblock/npfext_rndblock.c)
* Kernel module: [npf_ext_rndblock.c](../src/kern/npf_ext_rndblock.c)

The reader is assumed to have basic familiarity with the kernel interfaces.

## Parser module

The parser module is responsible for indicating which functions are
provided by the extension, parsing their parameters and constructing
a structure (object) to pass for the kernel module.

The dynamic module should have the following routines, where <extname>
represents the name of the extension:

* Initialisation routine: `int npfext_<extname>_init(void);`
* Constructor: `nl_ext_t *npfext_<extname>_construct(const char *name);`
* Parameter processor: `int npfext_<extname>_param(nl_ext_t *ext,
  const char *param, const char *val);`

Initialisation routine is called once DSO is loaded.  Any state initialisation
can be performed here.  The constructor routine is called for every rule
procedure which has an invoking call to an extension.  Consider the following
rule procedures in npf.conf:
```
procedure "test1" {
  rndblock: percentage 30.0;
}

procedure "test2" {
  rndblock: percentage 20.0;
  log: npflog0;
}
```

There will be three calls to `npfext_extname_construct()`: two with the
`name` "rndblock" and one with the name "log".  The routine should match the
name against "rndblock", ignoring the "log" case.  Note that the first two
ought to construct two separate objects having different properties.
Therefore:
```c
nl_ext_t *
npfext_rndblock_construct(const char *name)
{
	if (strcmp(name, "rndblock") != 0) {
		return NULL;
	}
	return npf_ext_construct(name);
}
```

Multiple functions can be supported by a single extension, e.g. it may
match "rndblock", "rnd-block" or another function implementing some
different functionality.

Upon object creation, parameter processing routine is invoked for every
specified function argument which is a key-value pair.  Therefore, for the
first case, `npfext_rndblock_param()` would be called with `param` value
"percentage" and `val` being "30.0".  This routine is responsible for parsing
the values, validating them and setting the extension object accordingly.
For an example, see
[npfext_rndblock_param()](../src/libext/ext_rndblock/npfext_rndblock.c#npfext_rndblock_param).
Note that a single parameter may be passed and `val` can be `NULL`.
The routine should return zero on success and error number on failure,
in which case npfctl will issue an error.  The
[libnpf(3)](http://man.netbsd.org/cgi-bin/man-cgi?libnpf+3+NetBSD-current)
library provides an interface to set attributes of various types,
e.g. `npf_ext_param_u32`.

The extension object will be passed to the kernel during the configuration
load.  The kernel module will be the consumer.

## Kernel module

The kernel module of the NPF extensions is the component which implements
the actual functionality.  It consumes the data provided by the parser module
i.e. configuration provided from the userspace.  As there can be multiple rule
procedures, there can be multiple configurations (extension objects) passed.

The kernel module should have the following:

* Module definition:
  ```c
  NPF_EXT_MODULE(npf_ext_<extname>, "");
  ```
* Module control routine:
  ```c
  static int npf_ext_<extname>_modcmd(modcmd_t cmd, void *arg);
  ```
* Register itself on module load:
  ```c
  void *npf_ext_register(const char *name, const npf_ext_ops_t *ops);
  ```
* Unregister itself on module unload with:
  ```c
  int npf_ext_unregister(void *extid);
  ```

See
[npf_ext_rndblock_modcmd()](../src/kern/npf_ext_rndblock.c#npf_ext_rndblock_modcmd)
for an example of the control routine.  A set of operations to register:
```c
static const npf_ext_ops_t npf_rndblock_ops = {
	.version	= NPFEXT_RNDBLOCK_VER,
	.ctx		= NULL,
	.ctor		= npf_ext_rndblock_ctor,
	.dtor		= npf_ext_rndblock_dtor,
	.proc		= npf_ext_rndblock
};
```

The structure has the following members:

* `.version` -- is used as a guard for the interface versioning
* `.ctx` -- an optional "context" argument passed to each routine
* `.ctor` -- constructor for each extension object received from the
npfctl dynamic module
* `.dtor` -- destructor for in-kernel extension objects
* `.proc` -- the processing routine executed for each packet

The constructor is called on configuration load.  This routine should retrieve
the data (extension object) passed by the parser module and create in-kernel
object associated with a rule procedure.  The construction shall have these
arguments:
```c
static int npf_ext_rndblock_ctor(npf_rproc_t *rp, const nvlist_t *params);
```

* `rp` -- rule procedure to associate with
* `params` -- data from the parser module, as a nvlist, see
[nvlist(3)](https://github.com/wheelsystems/nvlist).

A new object (metadata) shall be associated with the rule procedure using
`npf_rproc_assign` routine.

The destructor is called when the rule procedure is destroyed (due to the
flush of configuration or reload with procedure removed).  It shall have
these arguments:
```c
static void npf_ext_rndblock_dtor(npf_rproc_t *rp, void *meta);
```

* `rp` -- associated rule procedure
* `meta` -- metadata object

It is the responsibility of this routine to destroy `meta` object and any
other resources created in the constructor.

The processing routine is a key routine, which inspects the packet or the
connection and can perform an arbitrary action (including the modification
of the packet) or decide its destiny (pass or block).  This routine shall
have the following arguments:
```c
static void npf_ext_rndblock(npf_cache_t *npc, void *meta, const npf_match_info_t *mi, int *decision);
```

* `npc` -- structure containing information about L3/L4 headers
* `meta` -- metadata object
* `mi` -- matching rule information
* `decision` -- the current decision made by upper layer, which may be
`NPF_DECISION_BLOCK` or `NPF_DECISION_PASS`.

The extension may set `decision` accordingly.  Normally, an extension should
not override `NPF_DECISION_BLOCK`.
