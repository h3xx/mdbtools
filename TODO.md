TODO
----

### file format:

- export VBA script
- re-examine KKD records for form design (OLE streams?)
- write support (understood, not coded)

### libmdb:

- Complete the list of datatypes
- Straighten out which functions in libmdb are meant to be used and which
  ones should be static.
- Create an API reference for libmdb (maybe some man pages).
- Sargs need to support all datatypes
- Add support for index scanning when using sargs (partial)
- write support

### utils:

- need program to unpack VBA script to file (see prole)
- Access forms to glade converter ?
- need --version flag (done using -M flag on mdb-ver)

### SQL Engine:

- Joins
- insert/updates
- bogus column name in where clause not caught

### ODBC:

- many unimplemented funtions
